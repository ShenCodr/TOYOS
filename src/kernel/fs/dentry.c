#include "mod.h"

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
    if (path == NULL || name == NULL || path[0] != '/')
        return NULL;

    inode_t *ip = inode_get(ROOT_INODE);
    char *rest = path;
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
