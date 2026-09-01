#include "mod.h"
#include "../proc/method.h"

extern super_block_t sb;

static bool valid_dentry_name(char *name)
{
    if (name == NULL || name[0] == 0)
        return false;
    return strlen(name) < MAXLEN_FILENAME;
}

uint32 dentry_search(inode_t *ip, char *name)
{
    assert(ip != NULL, "dentry_search: NULL inode");
    assert(sleeplock_holding(&ip->slk), "dentry_search: slk");
    assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_search: not dir");
    if (!valid_dentry_name(name) || ip->disk_info.index[0] == 0)
        return INVALID_INODE_NUM;

    buffer_t *buf = buffer_get(ip->disk_info.index[0]);
    dentry_t *de = (dentry_t *)buf->data;
    uint32 result = INVALID_INODE_NUM;
    for (uint32 i = 0; i < DENTRY_PER_BLOCK; i++) {
        if (de[i].name[0] != 0 &&
            strncmp(de[i].name, name, MAXLEN_FILENAME) == 0) {
            result = de[i].inode_num;
            break;
        }
    }
    buffer_put(buf);
    return result;
}

uint32 dentry_create(inode_t *ip, uint32 inode_num, char *name)
{
    assert(ip != NULL, "dentry_create: NULL inode");
    assert(sleeplock_holding(&ip->slk), "dentry_create: slk");
    assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_create: not dir");
    if (!valid_dentry_name(name) || inode_num >= sb.total_inodes)
        return INVALID_INODE_NUM;
    if (dentry_search(ip, name) != INVALID_INODE_NUM)
        return INVALID_INODE_NUM;

    uint32 block_num = ip->disk_info.index[0];
    bool new_block = false;
    if (block_num == 0) {
        block_num = bitmap_alloc_block();
        if (block_num == (uint32)-1)
            return INVALID_INODE_NUM;
        ip->disk_info.index[0] = block_num;
        new_block = true;
    }

    buffer_t *buf = buffer_get(block_num);
    if (new_block)
        memset(buf->data, 0, BLOCK_SIZE);
    dentry_t *de = (dentry_t *)buf->data;
    uint32 offset = INVALID_INODE_NUM;
    for (uint32 i = 0; i < DENTRY_PER_BLOCK; i++) {
        if (de[i].name[0] == 0) {
            offset = i * sizeof(dentry_t);
            memset(&de[i], 0, sizeof(dentry_t));
            memmove(de[i].name, name, strlen(name) + 1);
            de[i].inode_num = inode_num;
            break;
        }
    }

    if (offset == INVALID_INODE_NUM) {
        buffer_put(buf);
        if (new_block) {
            bitmap_free_block(block_num);
            ip->disk_info.index[0] = 0;
        }
        return INVALID_INODE_NUM;
    }

    buffer_write(buf);
    buffer_put(buf);
    ip->disk_info.size += sizeof(dentry_t);
    inode_rw(ip, true);
    return offset;
}

uint32 dentry_delete(inode_t *ip, char *name)
{
    assert(ip != NULL, "dentry_delete: NULL inode");
    assert(sleeplock_holding(&ip->slk), "dentry_delete: slk");
    assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_delete: not dir");
    if (!valid_dentry_name(name) ||
        strncmp(name, ".", MAXLEN_FILENAME) == 0 ||
        strncmp(name, "..", MAXLEN_FILENAME) == 0 ||
        ip->disk_info.index[0] == 0)
        return INVALID_INODE_NUM;

    buffer_t *buf = buffer_get(ip->disk_info.index[0]);
    dentry_t *de = (dentry_t *)buf->data;
    uint32 result = INVALID_INODE_NUM;
    for (uint32 i = 0; i < DENTRY_PER_BLOCK; i++) {
        if (de[i].name[0] != 0 &&
            strncmp(de[i].name, name, MAXLEN_FILENAME) == 0) {
            result = de[i].inode_num;
            memset(&de[i], 0, sizeof(dentry_t));
            break;
        }
    }

    if (result != INVALID_INODE_NUM) {
        buffer_write(buf);
        assert(ip->disk_info.size >= sizeof(dentry_t),
               "dentry_delete: invalid size");
        ip->disk_info.size -= sizeof(dentry_t);
        inode_rw(ip, true);
    }
    buffer_put(buf);
    return result;
}

void dentry_print(inode_t *ip)
{
    assert(sleeplock_holding(&ip->slk), "dentry_print: slk!");
    assert(ip->disk_info.type == INODE_TYPE_DIR, "dentry_print: not dir!");
    if (ip->disk_info.index[0] == 0)
        panic("dentry_print: invalid index[0]!");

    printf("inode_num = %d, dentries:\n", ip->inode_num);
    buffer_t *buf = buffer_get(ip->disk_info.index[0]);
    dentry_t *de = (dentry_t *)buf->data;
    for (uint32 i = 0; i < DENTRY_PER_BLOCK; i++) {
        if (de[i].name[0] != 0)
            printf("dentry: offset = %d, inode_num = %d, name = %s\n",
                   i * sizeof(dentry_t), de[i].inode_num, de[i].name);
    }
    buffer_put(buf);
    printf("\n");
}

static char* get_element(char *path, char *name)
{
    while (*path == '/')
        path++;
    if (*path == 0) {
        name[0] = 0;
        return NULL;
    }

    char *start = path;
    while (*path != '/' && *path != 0)
        path++;
    int len = path - start;
    len = MIN(len, MAXLEN_FILENAME - 1);
    memmove(name, start, len);
    name[len] = 0;
    while (*path == '/')
        path++;
    return path;
}

static inode_t* __path_to_inode(char *path, char *name, bool find_parent_inode)
{
    if (path == NULL || name == NULL || path[0] == 0)
        return NULL;

    proc_t *p = myproc();
    inode_t *ip;
    char *rest = path;

    if (path[0] == '/') {
        ip = inode_get(ROOT_INODE);
    } else {
        if (p == NULL || p->cwd == NULL)
            return NULL;
        ip = inode_dup(p->cwd);
    }

    while (true) {
        rest = get_element(rest, name);
        if (name[0] == 0) {
            if (find_parent_inode) {
                inode_put(ip);
                return NULL;
            }
            return ip;
        }

        if (find_parent_inode && (rest == NULL || *rest == 0))
            return ip;

        inode_lock(ip);
        if (ip->disk_info.type != INODE_TYPE_DIR) {
            inode_unlock(ip);
            inode_put(ip);
            return NULL;
        }
        uint32 inode_num = dentry_search(ip, name);
        inode_unlock(ip);
        if (inode_num == INVALID_INODE_NUM) {
            inode_put(ip);
            return NULL;
        }

        inode_t *next = inode_get(inode_num);
        inode_put(ip);
        ip = next;
    }
}

inode_t* path_to_inode(char *path)
{
    char name[MAXLEN_FILENAME];
    return __path_to_inode(path, name, false);
}

inode_t* path_to_parent_inode(char *path, char *name)
{
    return __path_to_inode(path, name, true);
}

uint32 dentry_search_2(inode_t *ip, uint32 inode_num, char *name)
{
    assert(ip != NULL, "dentry_search_2: NULL inode");
    assert(sleeplock_holding(&ip->slk), "dentry_search_2: slk");
    assert(ip->disk_info.type == INODE_TYPE_DIR,
           "dentry_search_2: not dir");
    if (name == NULL || ip->disk_info.index[0] == 0)
        return INVALID_INODE_NUM;

    buffer_t *buf = buffer_get(ip->disk_info.index[0]);
    dentry_t *de = (dentry_t *)buf->data;
    uint32 result = INVALID_INODE_NUM;
    for (uint32 i = 0; i < DENTRY_PER_BLOCK; i++) {
        if (de[i].name[0] == 0 || de[i].inode_num != inode_num)
            continue;
        uint32 name_len = strlen(de[i].name);
        if (name_len >= MAXLEN_FILENAME)
            break;
        memmove(name, de[i].name, name_len + 1);
        result = name_len;
        break;
    }
    buffer_put(buf);
    return result;
}

uint32 dentry_transmit(inode_t *ip, uint64 dst, uint32 len, bool is_user_dst)
{
    assert(ip != NULL, "dentry_transmit: NULL inode");
    assert(sleeplock_holding(&ip->slk), "dentry_transmit: slk");
    assert(ip->disk_info.type == INODE_TYPE_DIR,
           "dentry_transmit: not dir");
    if (len == 0 || ip->disk_info.index[0] == 0)
        return 0;

    proc_t *p = myproc();
    buffer_t *buf = buffer_get(ip->disk_info.index[0]);
    dentry_t *de = (dentry_t *)buf->data;
    uint32 copied = 0;
    for (uint32 i = 0; i < DENTRY_PER_BLOCK; i++) {
        if (de[i].name[0] == 0)
            continue;
        if (len - copied < sizeof(dentry_t))
            break;
        if (is_user_dst)
            uvm_copyout(p->pgtbl, dst + copied,
                        (uint64)&de[i], sizeof(dentry_t));
        else
            memmove((void *)(dst + copied), &de[i], sizeof(dentry_t));
        copied += sizeof(dentry_t);
    }
    buffer_put(buf);
    return copied;
}

uint32 inode_to_path(inode_t *ip, char *path, uint32 len)
{
    if (ip == NULL || path == NULL || len < 2)
        return (uint32)-1;

    uint32 pos = len - 1;
    path[pos] = 0;
    inode_t *cur = inode_dup(ip);
    uint32 depth = 0;

    while (cur->inode_num != ROOT_INODE) {
        if (depth++ >= sb.total_inodes) {
            inode_put(cur);
            return (uint32)-1;
        }
        char name[MAXLEN_FILENAME];
        inode_lock(cur);
        if (cur->disk_info.type != INODE_TYPE_DIR) {
            inode_unlock(cur);
            inode_put(cur);
            return (uint32)-1;
        }
        uint32 parent_num = dentry_search(cur, "..");
        inode_unlock(cur);
        if (parent_num == INVALID_INODE_NUM || parent_num >= sb.total_inodes) {
            inode_put(cur);
            return (uint32)-1;
        }

        inode_t *parent = inode_get(parent_num);
        inode_lock(parent);
        uint32 name_len = dentry_search_2(parent, cur->inode_num, name);
        inode_unlock(parent);
        if (name_len == INVALID_INODE_NUM || name_len == 0 ||
            name_len + 1 > pos) {
            inode_put(parent);
            inode_put(cur);
            return (uint32)-1;
        }

        pos -= name_len;
        memmove(path + pos, name, name_len);
        --pos;
        path[pos] = '/';
        inode_put(cur);
        cur = parent;
    }

    inode_put(cur);
    if (pos == len - 1) {
        --pos;
        path[pos] = '/';
    }
    return pos;
}

inode_t* path_create_inode(char *path, uint16 type, uint16 major, uint16 minor)
{
    char name[MAXLEN_FILENAME];
    inode_t *parent = path_to_parent_inode(path, name);
    if (parent == NULL || !valid_dentry_name(name) ||
        strncmp(name, ".", MAXLEN_FILENAME) == 0 ||
        strncmp(name, "..", MAXLEN_FILENAME) == 0)
        return NULL;

    inode_lock(parent);
    bool exists = dentry_search(parent, name) != INVALID_INODE_NUM;
    inode_unlock(parent);
    if (exists) {
        inode_put(parent);
        return NULL;
    }

    inode_t *ip = inode_create(type, major, minor);
    if (ip == NULL) {
        inode_put(parent);
        return NULL;
    }

    if (type == INODE_TYPE_DIR) {
        inode_lock(ip);
        bool ok = dentry_create(ip, ip->inode_num, ".") != INVALID_INODE_NUM;
        ok = ok && dentry_create(ip, parent->inode_num, "..") != INVALID_INODE_NUM;
        if (ok) {
            ip->disk_info.nlink = 2;
            inode_rw(ip, true);
        }
        inode_unlock(ip);
        if (!ok) {
            inode_lock(ip);
            ip->disk_info.nlink = 0;
            inode_rw(ip, true);
            inode_unlock(ip);
            inode_put(ip);
            inode_put(parent);
            return NULL;
        }
    }

    inode_lock(parent);
    uint32 offset = dentry_create(parent, ip->inode_num, name);
    if (offset == INVALID_INODE_NUM) {
        inode_unlock(parent);
        inode_put(parent);
        inode_lock(ip);
        ip->disk_info.nlink = 0;
        inode_rw(ip, true);
        inode_unlock(ip);
        inode_put(ip);
        return NULL;
    }
    if (type == INODE_TYPE_DIR) {
        parent->disk_info.nlink++;
        inode_rw(parent, true);
    }
    inode_unlock(parent);
    inode_put(parent);
    return ip;
}

uint32 path_link(char *old_path, char *new_path)
{
    inode_t *old_ip = path_to_inode(old_path);
    if (old_ip == NULL)
        return (uint32)-1;

    /* 先确认旧目标不是目录，避免 parent == old_ip 时发生锁重入。 */
    inode_lock(old_ip);
    bool old_is_dir = old_ip->disk_info.type == INODE_TYPE_DIR;
    bool old_has_link = old_ip->disk_info.nlink != 0;
    inode_unlock(old_ip);
    if (old_is_dir || !old_has_link) {
        inode_put(old_ip);
        return (uint32)-1;
    }

    char name[MAXLEN_FILENAME];
    inode_t *parent = path_to_parent_inode(new_path, name);
    if (parent == NULL || !valid_dentry_name(name) ||
        strncmp(name, ".", MAXLEN_FILENAME) == 0 ||
        strncmp(name, "..", MAXLEN_FILENAME) == 0) {
        inode_put(old_ip);
        return (uint32)-1;
    }

    inode_lock(parent);
    if (dentry_search(parent, name) != INVALID_INODE_NUM) {
        inode_unlock(parent);
        inode_put(parent);
        inode_put(old_ip);
        return (uint32)-1;
    }

    inode_lock(old_ip);
    if (old_ip->disk_info.type == INODE_TYPE_DIR ||
        old_ip->disk_info.nlink == 0) {
        inode_unlock(old_ip);
        inode_unlock(parent);
        inode_put(parent);
        inode_put(old_ip);
        return (uint32)-1;
    }
    old_ip->disk_info.nlink++;
    inode_rw(old_ip, true);
    inode_unlock(old_ip);

    if (dentry_create(parent, old_ip->inode_num, name) == INVALID_INODE_NUM) {
        inode_lock(old_ip);
        old_ip->disk_info.nlink--;
        inode_rw(old_ip, true);
        inode_unlock(old_ip);
        inode_unlock(parent);
        inode_put(parent);
        inode_put(old_ip);
        return (uint32)-1;
    }

    inode_unlock(parent);
    inode_put(parent);
    inode_put(old_ip);
    return 0;
}

uint32 path_unlink(char *path)
{
    char name[MAXLEN_FILENAME];
    inode_t *parent = path_to_parent_inode(path, name);
    if (parent == NULL || !valid_dentry_name(name) ||
        strncmp(name, ".", MAXLEN_FILENAME) == 0 ||
        strncmp(name, "..", MAXLEN_FILENAME) == 0)
        return (uint32)-1;

    inode_lock(parent);
    uint32 inode_num = dentry_search(parent, name);
    if (inode_num == INVALID_INODE_NUM) {
        inode_unlock(parent);
        inode_put(parent);
        return (uint32)-1;
    }

    inode_t *ip = inode_get(inode_num);
    inode_lock(ip);
    bool is_dir = ip->disk_info.type == INODE_TYPE_DIR;
    if (is_dir && ip->disk_info.size != 2 * sizeof(dentry_t)) {
        inode_unlock(ip);
        inode_unlock(parent);
        inode_put(ip);
        inode_put(parent);
        return (uint32)-1;
    }

    if (dentry_delete(parent, name) == INVALID_INODE_NUM) {
        inode_unlock(ip);
        inode_unlock(parent);
        inode_put(ip);
        inode_put(parent);
        return (uint32)-1;
    }

    if (is_dir) {
        if (parent->disk_info.nlink > 0)
            parent->disk_info.nlink--;
        inode_rw(parent, true);
        ip->disk_info.nlink = 0;
    } else if (ip->disk_info.nlink > 0) {
        ip->disk_info.nlink--;
    }
    inode_rw(ip, true);

    inode_unlock(ip);
    inode_unlock(parent);
    inode_put(ip);
    inode_put(parent);
    return 0;
}
