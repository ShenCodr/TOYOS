#include "mod.h"

/*
	将ELF文件中的segment放入内存中制定位置
	inode逻辑区域: [seg_start, seg_start + len)
	进程地址空间: [va_start, va_start + len), 对应的物理页是存在的
*/
static void load_segment(inode_t *ip, pgtbl_t pgtbl,
	uint64 seg_start, uint64 va_start, uint32 len)
{
	assert(va_start % PGSIZE == 0, "load_segment: va aligned!");

	pte_t *pte;
	uint64 pa;
	uint32 read_len, cut_len;

	for (read_len = 0; read_len < len; read_len += PGSIZE)
	{
		/* 获取物理内存地址 */
		pte = vm_getpte(pgtbl, va_start + read_len, false);
		assert(pte != NULL && (*pte & PTE_V) && (*pte & PTE_U),
			"load_segment: invalid pte!");
		pa = PTE_TO_PA(*pte);
		assert(pa != 0, "load_segment: invalid pa!");

		/* 读入segment的一部分 */
		cut_len = MIN(len - read_len, PGSIZE);
		if (inode_read_data(ip, (uint32)seg_start + read_len, cut_len, (void*)pa, false) != cut_len)
			panic("load_segment: read fail!");
	}
}

static int segment_perm(uint32 flags)
{
	int perm = 0;
	if (flags & ELF_PROG_FLAG_READ)
		perm |= PTE_R;
	if (flags & ELF_PROG_FLAG_WRITE)
		perm |= PTE_W;
	if (flags & ELF_PROG_FLAG_EXEC)
		perm |= PTE_X;
	return perm;
}

/* 将程序的代码区和数据区读入用户堆中, 返回new_heap_top */
static uint64 prepare_heap(pgtbl_t new_pgtbl, inode_t *ip, elf_header_t *eh)
{
	program_header_t ph;
	uint64 new_heap_top = USER_BASE, old_heap_top = USER_BASE;
	bool loaded = false;
	bool entry_loaded = false;

	for (uint32 off = eh->ph_off; off < eh->ph_off + eh->ph_ent_num * sizeof(ph); off += sizeof(ph))
	{
		// 读入一个program header
		if (inode_read_data(ip, off, sizeof(ph), &ph, false) != sizeof(ph))
			return -1;

		// 判断是否有必要载入
		if (ph.type != ELF_PROG_LOAD)
			continue;

		// program header参数的合法性检查
		if (ph.mem_size < ph.file_size)
			return -1;
		if (ph.va + ph.mem_size < ph.va)
			return -1;
		if (ph.va < USER_BASE || ph.va + ph.mem_size > MMAP_BEGIN)
			return -1;
		if (ph.va < old_heap_top)
			return -1;
		if (ph.file_size > 0xfffffffful || ph.mem_size > 0xfffffffful)
			return -1;
		if (ph.off + ph.file_size < ph.off ||
			ph.off + ph.file_size > ip->disk_info.size)
			return -1;
		if (ph.va % PGSIZE != 0)
			return -1;
		int perm = segment_perm(ph.flags);
		if (perm == 0)
			return -1;

		// 用户堆生长
		new_heap_top = uvm_heap_grow(new_pgtbl, old_heap_top,
						(uint32)(ph.va + ph.mem_size - old_heap_top), perm);
		if (new_heap_top != ph.va + ph.mem_size)
			return -1;
		old_heap_top = new_heap_top;

		// segment读入
		load_segment(ip, new_pgtbl, ph.off, ph.va, ph.file_size);
		loaded = true;
		if (eh->entry >= ph.va && eh->entry < ph.va + ph.mem_size)
			entry_loaded = true;
	}

	if (!loaded || !entry_loaded)
		return -1;
	return new_heap_top;
}

/* 准备栈空间用于存储输入参数(4KB), 设置arg_count, 返回sp */
static uint64 prepare_stack(pgtbl_t new_pgtbl, char **argv, int *arg_count)
{
	uint64 ustack_page;
	uint64 sp = TRAPFRAME, sp_base = TRAPFRAME - PGSIZE;
	uint64 sp_list[ELF_MAXARGS + 1];
	uint32 argc, arg_len;

	ustack_page = (uint64)pmem_alloc(false);
	vm_mappages(new_pgtbl, sp_base, ustack_page, PGSIZE, PTE_R | PTE_W | PTE_U);

	for (argc = 0; argv[argc] != NULL; argc++)
	{
		if (argc >= ELF_MAXARGS)
			return -1;

		arg_len = strlen(argv[argc]) + 1;
		sp -= ALIGN_UP(arg_len, 16);
		if (sp < sp_base)
			return -1;

		uvm_copyout(new_pgtbl, sp, (uint64)argv[argc], arg_len);

		sp_list[argc] = sp;
	}
	sp_list[argc] = 0;

	arg_len = (argc + 1) * sizeof(uint64);
	sp -= ALIGN_UP(arg_len, 16);
	if (sp < sp_base)
		return -1;

	uvm_copyout(new_pgtbl, sp, (uint64)sp_list, arg_len);

	*arg_count = argc;

	return sp;
}

/*
	执行ELF文件
	输入路径和参数
	成功返回argc, 失败返回-1
*/
int proc_exec(char *path, char **argv)
{
	proc_t *p = myproc();
	if (p == NULL || path == NULL || argv == NULL)
		return -1;

	inode_t *ip = path_to_inode(path);
	if (ip == NULL)
		return -1;

	elf_header_t eh;
	inode_lock(ip);
	bool valid = ip->disk_info.type == INODE_TYPE_DATA &&
		ip->disk_info.size >= sizeof(elf_header_t) &&
		inode_read_data(ip, 0, sizeof(eh), &eh, false) == sizeof(eh);
	if (valid) {
		uint64 ph_end = eh.ph_off +
			(uint64)eh.ph_ent_num * eh.ph_ent_size;
		valid = eh.magic == ELF_MAGIC &&
			eh.eh_size == sizeof(elf_header_t) &&
			eh.ph_ent_size == sizeof(program_header_t) &&
			eh.ph_ent_num != 0 &&
			ph_end >= eh.ph_off && ph_end <= ip->disk_info.size &&
			eh.entry >= USER_BASE && eh.entry < MMAP_BEGIN;
	}

	trapframe_t *new_tf = NULL;
	pgtbl_t new_pgtbl = NULL;
	uint64 new_heap_top = (uint64)-1;
	int argc = 0;
	uint64 new_sp = (uint64)-1;
	if (valid) {
		new_tf = (trapframe_t *)pmem_alloc(false);
		new_pgtbl = proc_pgtbl_init((uint64)new_tf);
		new_heap_top = prepare_heap(new_pgtbl, ip, &eh);
	}
	inode_unlock(ip);
	inode_put(ip);

	if (!valid || new_pgtbl == NULL || new_heap_top == (uint64)-1) {
		if (new_pgtbl != NULL)
			uvm_destroy_pgtbl(new_pgtbl);
		return -1;
	}

	new_sp = prepare_stack(new_pgtbl, argv, &argc);
	if (new_sp == (uint64)-1) {
		uvm_destroy_pgtbl(new_pgtbl);
		return -1;
	}

	/* 所有新地址空间资源准备完成后，再一次性替换旧空间。 */
	pgtbl_t old_pgtbl = p->pgtbl;
	trapframe_t *old_tf = p->tf;
	mmap_region_t *old_mmap = p->mmap;
	p->pgtbl = new_pgtbl;
	p->tf = new_tf;
	p->heap_top = new_heap_top;
	p->ustack_npage = 1;
	p->mmap = NULL;

	p->tf->a0 = argc;
	p->tf->a1 = new_sp;
	p->tf->user_to_kern_epc = eh.entry;
	p->tf->sp = new_sp;

	char *base = path;
	for (char *scan = path; *scan != 0; scan++)
		if (*scan == '/')
			base = scan + 1;
	memset(p->name, 0, sizeof(p->name));
	uint32 name_len = strlen(base);
	if (name_len >= sizeof(p->name))
		name_len = sizeof(p->name) - 1;
	memmove(p->name, base, name_len);

	uvm_destroy_pgtbl(old_pgtbl);
	while (old_mmap != NULL) {
		mmap_region_t *next = old_mmap->next;
		mmap_region_free(old_mmap);
		old_mmap = next;
	}
	(void)old_tf;
	return argc;
}
