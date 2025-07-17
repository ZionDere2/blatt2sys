#include "../lib/operations.h"
#include <stddef.h>
#include <string.h>


static int find_inode_by_path(file_system *fs, const char *path)
{
    if (!path || path[0] != '/') return -1;
    if (strcmp(path, "/") == 0) return fs->root_node;

    int curr = fs->root_node;
    char temp[strlen(path) + 1];
    strcpy(temp, path + 1); // skip initial slash

    char *seg = strtok(temp, "/");
    while (seg) {
        int next = -1;
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            int c = fs->inodes[curr].direct_blocks[i];
            if (c != -1 && strncmp(fs->inodes[c].name, seg, NAME_MAX_LENGTH) == 0) {
                next = c;
                break;
            }
        }
        if (next == -1) return -1;
        curr = next;
        seg = strtok(NULL, "/");
    }
    return curr;
}

// Searches for a free data block index
static int find_free_block(file_system *fs)
{
    for (int i = 0; i < fs->s_block->num_blocks; ++i) {
        if (fs->free_list[i]) return i;
    }
    return -1;
}

// Connects a child inode to a parent directory
static int add_child_inode(file_system *fs, int parent, int child)
{
    for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
        if (fs->inodes[parent].direct_blocks[i] == -1) {
            fs->inodes[parent].direct_blocks[i] = child;
            return 0;
        }
    }
    return -1; // No space left
}

// Splits a path into the parent and final component
static int split_path(const char *path, char *parent_out, const char **name_out)
{
    if (!path || path[0] != '/') return -1;

    const char *slash = strrchr(path, '/');
    if (!slash) return -1;

    *name_out = slash + 1;
    if (**name_out == '\0') return -1;

    if (slash == path) {
        strcpy(parent_out, "/");
    } else {
        size_t len = (size_t)(slash - path);
        memcpy(parent_out, path, len);
        parent_out[len] = '\0';
    }
    return 0;
}

// Makes a new directory under a given absolute path
int fs_mkdir(file_system *fs, char *path)
{
    if (!fs || !path || path[0] != '/') return -1;

    char parent_path[strlen(path) + 1];
    const char *dir_name;
    if (split_path(path, parent_path, &dir_name) != 0) return -1;

    int parent_idx = find_inode_by_path(fs, parent_path);
    if (parent_idx < 0 || fs->inodes[parent_idx].n_type != directory) return -1;

    for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
        int ch = fs->inodes[parent_idx].direct_blocks[i];
        if (ch != -1 && strncmp(fs->inodes[ch].name, dir_name, NAME_MAX_LENGTH) == 0) {
            return -1; // already exists
        }
    }

    int free_i = find_free_inode(fs);
    if (free_i < 0) return -1;

    inode_init(&fs->inodes[free_i]);
    fs->inodes[free_i].n_type = directory;
    strncpy(fs->inodes[free_i].name, dir_name, NAME_MAX_LENGTH);
    fs->inodes[free_i].parent = parent_idx;

    if (add_child_inode(fs, parent_idx, free_i) != 0) return -1;

    return 0;
}

// Creates a new regular file
int fs_mkfile(file_system *fs, char *path_and_name)
{
    if (!fs || !path_and_name || path_and_name[0] != '/') return -1;

    char parent_path[strlen(path_and_name) + 1];
    const char *filename;
    if (split_path(path_and_name, parent_path, &filename) != 0) return -1;

    int parent_idx = find_inode_by_path(fs, parent_path);
    if (parent_idx < 0 || fs->inodes[parent_idx].n_type != directory) return -1;

    for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
        int ch = fs->inodes[parent_idx].direct_blocks[i];
        if (ch != -1 && strncmp(fs->inodes[ch].name, filename, NAME_MAX_LENGTH) == 0) {
            return -2; // duplicate
        }
    }

    int free_i = find_free_inode(fs);
    if (free_i < 0) return -1;

    inode_init(&fs->inodes[free_i]);
    fs->inodes[free_i].n_type = reg_file;
    strncpy(fs->inodes[free_i].name, filename, NAME_MAX_LENGTH);
    fs->inodes[free_i].parent = parent_idx;

    if (add_child_inode(fs, parent_idx, free_i) != 0) return -1;

    return 0;
}

// Copies an inode recursively
static int copy_inode_recursive(file_system *fs, int src_idx, int dst_parent, const char *name)
{
    int new_idx = find_free_inode(fs);
    if (new_idx < 0) return -1;

    inode_init(&fs->inodes[new_idx]);
    fs->inodes[new_idx].n_type = fs->inodes[src_idx].n_type;
    strncpy(fs->inodes[new_idx].name, name, NAME_MAX_LENGTH);
    fs->inodes[new_idx].parent = dst_parent;
    if (add_child_inode(fs, dst_parent, new_idx) != 0) return -1;

    if (fs->inodes[src_idx].n_type == reg_file) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
            int b = fs->inodes[src_idx].direct_blocks[i];
            if (b != -1) {
                int nb = find_free_block(fs);
                if (nb < 0) return -1;
                fs->free_list[nb] = 0;
                fs->s_block->free_blocks--;
                fs->data_blocks[nb].size = fs->data_blocks[b].size;
                memcpy(fs->data_blocks[nb].block, fs->data_blocks[b].block, fs->data_blocks[b].size);
                fs->inodes[new_idx].direct_blocks[i] = nb;
            }
        }
        fs->inodes[new_idx].size = fs->inodes[src_idx].size;
    } else if (fs->inodes[src_idx].n_type == directory) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
            int child = fs->inodes[src_idx].direct_blocks[i];
            if (child != -1) {
                copy_inode_recursive(fs, child, new_idx, fs->inodes[child].name);
            }
        }
    }
    return 0;
}

int fs_cp(file_system *fs, char *src_path, char *dst_path_and_name)
{
    if (!fs || !src_path || !dst_path_and_name) return -1;
    int src_idx = find_inode_by_path(fs, src_path);
    if (src_idx < 0) return -1;

    char parent_path[strlen(dst_path_and_name) + 1];
    const char *name;
    if (split_path(dst_path_and_name, parent_path, &name) != 0) return -1;
    int dst_parent = find_inode_by_path(fs, parent_path);
    if (dst_parent < 0 || fs->inodes[dst_parent].n_type != directory) return -1;
    for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
        int ch = fs->inodes[dst_parent].direct_blocks[i];
        if (ch != -1 && strncmp(fs->inodes[ch].name, name, NAME_MAX_LENGTH) == 0)
            return -2;
    }
    return copy_inode_recursive(fs, src_idx, dst_parent, name);
}

char *fs_list(file_system *fs, char *path)
{
    int idx = find_inode_by_path(fs, path);
    if (idx < 0 || fs->inodes[idx].n_type != directory) return NULL;
    size_t alloc = DIRECT_BLOCKS_COUNT * (NAME_MAX_LENGTH + 5);
    char *out = malloc(alloc);
    if (!out) return NULL;
    out[0] = '\0';
    for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
        int child = fs->inodes[idx].direct_blocks[i];
        if (child != -1) {
            const char *prefix = fs->inodes[child].n_type == directory ? "DIR " : "FIL ";
            strncat(out, prefix, alloc - strlen(out) - 1);
            strncat(out, fs->inodes[child].name, alloc - strlen(out) - 1);
            strncat(out, "\n", alloc - strlen(out) - 1);
        }
    }
    return out;
}

int fs_writef(file_system *fs, char *filename, char *text)
{
    if (!fs || !filename || !text) return -1;
    int idx = find_inode_by_path(fs, filename);
    if (idx < 0 || fs->inodes[idx].n_type != reg_file) return -1;
    size_t remaining = strlen(text);
    size_t written = 0;
    for (int i = 0; i < DIRECT_BLOCKS_COUNT && remaining > 0; ++i) {
        int blk = fs->inodes[idx].direct_blocks[i];
        if (blk == -1) {
            blk = find_free_block(fs);
            if (blk == -1) return -2;
            fs->free_list[blk] = 0;
            fs->s_block->free_blocks--;
            fs->inodes[idx].direct_blocks[i] = blk;
            fs->data_blocks[blk].size = 0;
        }
        size_t off = fs->data_blocks[blk].size;
        size_t space = BLOCK_SIZE - off;
        size_t to_copy = MIN(remaining, space);
        memcpy(fs->data_blocks[blk].block + off, text + written, to_copy);
        fs->data_blocks[blk].size += to_copy;
        written += to_copy;
        remaining -= to_copy;
        if (remaining == 0) break;
    }
    if (remaining > 0) return -2;
    fs->inodes[idx].size = 0;
    for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
        int b = fs->inodes[idx].direct_blocks[i];
        if (b != -1) fs->inodes[idx].size += fs->data_blocks[b].size;
    }
    return (int)written;
}

uint8_t *fs_readf(file_system *fs, char *filename, int *file_size)
{
    if (!fs || !filename || !file_size) return NULL;
    int idx = find_inode_by_path(fs, filename);
    if (idx < 0 || fs->inodes[idx].n_type != reg_file) {
        *file_size = 0;
        return NULL;
    }
    int total = fs->inodes[idx].size;
    *file_size = total;
    if (total == 0) return NULL;
    uint8_t *buf = malloc(total);
    if (!buf) return NULL;
    int pos = 0;
    for (int i = 0; i < DIRECT_BLOCKS_COUNT && pos < total; ++i) {
        int b = fs->inodes[idx].direct_blocks[i];
        if (b != -1) {
            memcpy(buf + pos, fs->data_blocks[b].block, fs->data_blocks[b].size);
            pos += fs->data_blocks[b].size;
        }
    }
    return buf;
}

static void rm_inode_recursive(file_system *fs, int idx)
{
    inode *node = &fs->inodes[idx];
    if (node->n_type == directory) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
            int child = node->direct_blocks[i];
            if (child != -1) {
                rm_inode_recursive(fs, child);
            }
        }
    } else if (node->n_type == reg_file) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
            int b = node->direct_blocks[i];
            if (b != -1) {
                fs->free_list[b] = 1;
                fs->s_block->free_blocks++;
                fs->data_blocks[b].size = 0;
                node->direct_blocks[i] = -1;
            }
        }
        node->size = 0;
    }
    int parent = node->parent;
    if (parent != -1) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
            if (fs->inodes[parent].direct_blocks[i] == idx)
                fs->inodes[parent].direct_blocks[i] = -1;
        }
    }
    inode_init(node);
}

int fs_rm(file_system *fs, char *path)
{
    if (!fs || !path) return -1;
    int idx = find_inode_by_path(fs, path);
    if (idx < 0 || idx == fs->root_node) return -1;
    rm_inode_recursive(fs, idx);
    return 0;
}

int fs_import(file_system *fs, char *int_path, char *ext_path)
{
    if (!fs || !int_path || !ext_path) return -1;
    FILE *f = fopen(ext_path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = NULL;
    if (size > 0) {
        buf = malloc(size + 1);
        fread(buf, 1, size, f);
        buf[size] = '\0';
    }
    fclose(f);
    if (size > 0) {
        int res = fs_writef(fs, int_path, buf);
        free(buf);
        return res < 0 ? -1 : 0;
    }
    return 0;
}

int fs_export(file_system *fs, char *int_path, char *ext_path)
{
    if (!fs || !int_path || !ext_path) return -1;
    int idx = find_inode_by_path(fs, int_path);
    if (idx < 0 || fs->inodes[idx].n_type != reg_file) return -1;
    int size = 0;
    uint8_t *data = fs_readf(fs, int_path, &size);
    FILE *f = fopen(ext_path, "wb");
    if (!f) { free(data); return -1; }
    if (size > 0 && data) fwrite(data, 1, size, f);
    fclose(f);
    free(data);
    return 0;
}
