#include "mod.h"

// 内核页表
static pgtbl_t kernel_pgtbl;

// 根据pagetable,找到va对应的pte
// 若设置alloc=true 则在PTE无效时尝试申请一个物理页
// 成功返回PTE, 失败返回NULL
// 提示：使用 VA_TO_VPN + PTE_TO_PA + PA_TO_PTE
pte_t *vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    // Sv39 有三级页表,其中level 0 的 PTE 是最终要返回的映射项。
    for (int level = 2; level > 0; level--)
    {
        // 利用 VA_TO_VPN 宏找到当前级别对应的 PTE
        pte_t *pte = &pgtbl[VA_TO_VPN(va, level)];

        if (*pte & PTE_V)
        {
            
            //中间级 PTE 应当指向下一级页表，
            // 因而 R/W/X 三个权限位都应为 0
            if (!PTE_CHECK(*pte))
                panic("vm_getpte: intermediate pte is a leaf");

            // 从当前 PTE 中取出下一级页表的物理地址
            pgtbl = (pgtbl_t)PTE_TO_PA(*pte);
        }
        else
        {
            // 没有下一级页表：
            //调用者不允许分配时，当前虚拟地址没有对应 PTE
            if (!alloc)
                return NULL;

            // 申请一个内核物理页作为下一级页表
            pgtbl = (pgtbl_t)pmem_alloc(true);

            //在当前 PTE 记录下一级页表的物理地址， 并设置 PTE_V 表示该 PTE 有效。
            *pte = PA_TO_PTE(pgtbl) | PTE_V;
        }
    }

    // 返回 level 0 中、va 对应的最终 PTE 地址
    return &pgtbl[VA_TO_VPN(va, 0)];
}

// 在pgtbl中建立 [va, va + len) -> [pa, pa + len) 的映射
// 本质是找到va在页表对应位置的pte并修改它
// 检查: va pa 应当是 page-aligned, len(字节数) > 0, va + len <= VA_MAX
// 注意: perm 应该如何使用
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    // 虚拟地址必须从页边界开始
    if (va % PGSIZE != 0) panic("vm_mappages: va is not page aligned");
    // 物理地址同上
    if (pa % PGSIZE != 0) panic("vm_mappages: pa is not page aligned");
    if (len == 0) panic("vm_mappages: len is zero");
    // 映射范围不能超过 Sv39 规定的最大虚拟地址
    if (va + len > VA_MAX)
        panic("vm_mappages: va exceeds VA_MAX");
    // last 是映射范围包含的最后一个虚拟页起始地址
    uint64 last = (va + len - 1) & ~(PGSIZE - 1);

    for (;;)
    {
        // 找到当前虚拟页对应的末级 PTE 必要时自动创建中间页表
        pte_t *pte = vm_getpte(pgtbl, va, true);
        if (pte == NULL)
            panic("vm_mappages: vm_getpte failed");
        // 写入物理页号 权限位和有效位
        *pte = PA_TO_PTE(pa) | perm | PTE_V;
        if (va == last)
            break;
        // 继续映射下一对虚拟页和物理页
        va += PGSIZE;
        pa += PGSIZE;
    }
}

// 解除pgtbl中[va, va+len)区域的映射
// 如果freeit == true则释放对应物理页, 默认是用户的物理页
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    // 虚拟地址必须从页边界开始
    if (va % PGSIZE != 0) panic("vm_unmappages: va is not page aligned");
    if (len == 0) panic("vm_unmappages: len is zero");

    // 解映射范围不能超过最大虚拟地址
    if (va + len > VA_MAX)
        panic("vm_unmappages: va exceeds VA_MAX");

    // last 是要处理的最后一个虚拟页起始地址
    uint64 last = (va + len - 1) & ~(PGSIZE - 1);

    for (;;)
    {
        // 只查询 PTE 不允许在解映射时创建新的页表
        pte_t *pte = vm_getpte(pgtbl, va, false);
        if (pte == NULL)
            panic("vm_unmappages: pte not found");

        // 无效 PTE 说明该虚拟页本来就没有映射
        if (!(*pte & PTE_V))
            panic("vm_unmappages: pte is not valid");

        // 末级 PTE 指向普通物理页
        if (PTE_CHECK(*pte))
            panic("vm_unmappages: pte is not a leaf");
        // 保存物理页地址
        uint64 pa = PTE_TO_PA(*pte);
        // 清零 PTE 解除虚拟页到物理页的映射
        *pte = 0;

        //freeit 为 true 时释放对应的用户物理页
        if (freeit)
            pmem_free(pa, false);

        if (va == last)
            break;
        va += PGSIZE;
    }
}

// 完成UART、CLINT、PLIC、内核代码区、内核数据区、可分配区域的页表映射
// 相当于部分填充kernel_pgtbl
void kvm_init()
{
    // 申请并清零一页内核物理页作为顶级内核页表
    kernel_pgtbl = (pgtbl_t)pmem_alloc(true);

    // 映射 UART 寄存器所在的一页
    vm_mappages(kernel_pgtbl, UART_BASE, UART_BASE, PGSIZE, PTE_R | PTE_W);

    // 映射 CLINT 寄存器区域
    vm_mappages(kernel_pgtbl, CLINT_BASE, CLINT_BASE, 0x10000, PTE_R | PTE_W);

    // 映射 PLIC 寄存器区域
    vm_mappages(kernel_pgtbl, PLIC_BASE, PLIC_BASE, 0x4000000, PTE_R | PTE_W);

    // 映射内核代码区为可读可执行
    vm_mappages(kernel_pgtbl, KERNEL_BASE, KERNEL_BASE,
                (uint64)KERNEL_DATA - KERNEL_BASE, PTE_R | PTE_X);

    // 映射内核数据区为可读可写
    vm_mappages(kernel_pgtbl, (uint64)KERNEL_DATA, (uint64)KERNEL_DATA,
                (uint64)ALLOC_BEGIN - (uint64)KERNEL_DATA, PTE_R | PTE_W);

    // 映射可分配物理页区域为可读可写
    vm_mappages(kernel_pgtbl, (uint64)ALLOC_BEGIN, (uint64)ALLOC_BEGIN,
                (uint64)ALLOC_END - (uint64)ALLOC_BEGIN, PTE_R | PTE_W);
}

// 每个CPU都需要调用, 从不使用页表切换到使用内核页表
// 切换后需要刷新TLB里面的缓存
void kvm_inithart()
{
    w_satp(MAKE_SATP(kernel_pgtbl));
    sfence_vma();
}

// 输出页表内容(for debug)
void vm_print(pgtbl_t pgtbl)
{
    // 顶级页表，次级页表，低级页表
    pgtbl_t pgtbl_2 = pgtbl, pgtbl_1 = NULL, pgtbl_0 = NULL;
    pte_t pte;

    printf("level-2 pgtbl: pa = %p\n", pgtbl_2);
    for (int i = 0; i < PGSIZE / sizeof(pte_t); i++)
    {
        pte = pgtbl_2[i];
        if (!((pte)&PTE_V))
            continue;
        assert(PTE_CHECK(pte), "vm_print: pte check fail (1)");
        pgtbl_1 = (pgtbl_t)PTE_TO_PA(pte);
        printf(".. level-1 pgtbl %d: pa = %p\n", i, pgtbl_1);

        for (int j = 0; j < PGSIZE / sizeof(pte_t); j++)
        {
            pte = pgtbl_1[j];
            if (!((pte)&PTE_V))
                continue;
            assert(PTE_CHECK(pte), "vm_print: pte check fail (2)");
            pgtbl_0 = (pgtbl_t)PTE_TO_PA(pte);
            printf(".. .. level-0 pgtbl %d: pa = %p\n", j, pgtbl_0);

            for (int k = 0; k < PGSIZE / sizeof(pte_t); k++)
            {
                pte = pgtbl_0[k];
                if (!((pte)&PTE_V))
                    continue;
                assert(!PTE_CHECK(pte), "vm_print: pte check fail (3)");
                printf(".. .. .. physical page %d: pa = %p flags = %d\n", k, (uint64)PTE_TO_PA(pte), (int)PTE_FLAGS(pte));
            }
        }
    }
}
