
#include "../lib/operations.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


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

// writes arbitrary bytes to a file node
static int write_bytes(file_system *fs, int idx, const uint8_t *data, size_t len)
{
    size_t remaining = len;
    size_t written = 0;

    for (int i = 0; i < DIRECT_BLOCKS_COUNT && remaining > 0; ++i) {
        int blk = fs->inodes[idx].direct_blocks[i];
        if (blk == -1) {
            blk = find_free_block(fs);
            if (blk < 0)
                return -2;
            fs->inodes[idx].direct_blocks[i] = blk;
            fs->free_list[blk] = 0;
            fs->s_block->free_blocks--;
            fs->data_blocks[blk].size = 0;
        }

        size_t space = BLOCK_SIZE - fs->data_blocks[blk].size;
        if (space == 0)
            continue;

        size_t to_write = MIN(space, remaining);
        memcpy(fs->data_blocks[blk].block + fs->data_blocks[blk].size,
               data + written, to_write);
        fs->data_blocks[blk].size += to_write;

        written += to_write;
        remaining -= to_write;
    }

    fs->inodes[idx].size += written;
    return remaining == 0 ? (int)written : -2;
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

// Writes text to an existing file (append mode)
int fs_writef(file_system *fs, char *filename, char *text)
{
    if (!fs || !filename || !text) return -1;

    int idx = find_inode_by_path(fs, filename);
    if (idx < 0 || fs->inodes[idx].n_type != reg_file) return -1;

    return write_bytes(fs, idx, (const uint8_t *)text, strlen(text));
}

// Reads a file and allocates a buffer with its contents
uint8_t *fs_readf(file_system *fs, char *filename, int *file_size)
{
    if (!fs || !filename || !file_size) return NULL;

    int idx = find_inode_by_path(fs, filename);
    if (idx < 0 || fs->inodes[idx].n_type != reg_file) {
        *file_size = 0;
        return NULL;
    }

    *file_size = fs->inodes[idx].size;
    if (*file_size == 0) return NULL;

    uint8_t *buf = malloc(*file_size + 1);
    if (!buf) return NULL;
    size_t copied = 0;
    for (int i = 0; i < DIRECT_BLOCKS_COUNT && copied < (size_t)*file_size; ++i) {
        int blk = fs->inodes[idx].direct_blocks[i];
        if (blk == -1) break;
        size_t bytes = MIN(fs->data_blocks[blk].size, (size_t)*file_size - copied);
        memcpy(buf + copied, fs->data_blocks[blk].block, bytes);
        copied += bytes;
    }
    buf[*file_size] = 0;
    return buf;
}

// Helper to remove an inode recursively
static void remove_inode_recursive(file_system *fs, int idx)
{
    if (fs->inodes[idx].n_type == directory) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
            int child = fs->inodes[idx].direct_blocks[i];
            if (child != -1) {
                remove_inode_recursive(fs, child);
                fs->inodes[idx].direct_blocks[i] = -1;
            }
        }
    } else if (fs->inodes[idx].n_type == reg_file) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
            int blk = fs->inodes[idx].direct_blocks[i];
            if (blk != -1) {
                fs->free_list[blk] = 1;
                fs->data_blocks[blk].size = 0;
                fs->s_block->free_blocks++; 
                memset(fs->data_blocks[blk].block, 0, BLOCK_SIZE);
                fs->inodes[idx].direct_blocks[i] = -1;
            }
        }
    }

    int parent = fs->inodes[idx].parent;
    if (parent >= 0) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
            if (fs->inodes[parent].direct_blocks[i] == idx) {
                fs->inodes[parent].direct_blocks[i] = -1;
                break;
            }
        }
    }

    inode_init(&fs->inodes[idx]);
}

int fs_rm(file_system *fs, char *path)
{
    if (!fs || !path) return -1;
    int idx = find_inode_by_path(fs, path);
    if (idx < 0) return -1;
    remove_inode_recursive(fs, idx);
    return 0;
}

// Builds a directory listing string
char *fs_list(file_system *fs, char *path)
{
    if (!fs || !path) return NULL;
    int idx = find_inode_by_path(fs, path);
    if (idx < 0 || fs->inodes[idx].n_type != directory) return NULL;

    size_t max = fs->s_block->num_blocks * (NAME_MAX_LENGTH + 5);
    char *out = calloc(1, max);
    if (!out) return NULL;

    for (int i = 0; i < fs->s_block->num_blocks; ++i) {
        if (fs->inodes[i].parent == idx) {
            const char *prefix = NULL;
            if (fs->inodes[i].n_type == directory)
                prefix = "DIR ";
            else if (fs->inodes[i].n_type == reg_file)
                prefix = "FIL ";
            if (prefix) {
                strcat(out, prefix);
                strncat(out, fs->inodes[i].name, NAME_MAX_LENGTH);
                strcat(out, "\n");
            }
        }
    }
    return out;
}

// Recursive helper for cp
static int copy_inode(file_system *fs, int src, int parent, const char *name)
{
    int new_i = find_free_inode(fs);
    if (new_i < 0) return -1;
    inode_init(&fs->inodes[new_i]);
    fs->inodes[new_i].n_type = fs->inodes[src].n_type;
    strncpy(fs->inodes[new_i].name, name, NAME_MAX_LENGTH);
    fs->inodes[new_i].parent = parent;
    if (add_child_inode(fs, parent, new_i) != 0) return -1;

    if (fs->inodes[src].n_type == reg_file) {
        fs->inodes[new_i].size = fs->inodes[src].size;
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
            int blk = fs->inodes[src].direct_blocks[i];
            if (blk == -1) break;
            int free_b = find_free_block(fs);
            if (free_b < 0) return -1;
            fs->free_list[free_b] = 0;
            fs->s_block->free_blocks--;
            fs->data_blocks[free_b] = fs->data_blocks[blk];
            fs->inodes[new_i].direct_blocks[i] = free_b;
        }
    } else if (fs->inodes[src].n_type == directory) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
            int child = fs->inodes[src].direct_blocks[i];
            if (child != -1) {
                copy_inode(fs, child, new_i, fs->inodes[child].name);
            }
        }
    }
    return 0;
}

int fs_cp(file_system *fs, char *src_path, char *dst_path_and_name)
{
    if (!fs || !src_path || !dst_path_and_name) return -1;
    int src = find_inode_by_path(fs, src_path);
    if (src < 0) return -1;

    char parent_path[strlen(dst_path_and_name) + 1];
    const char *new_name;
    if (split_path(dst_path_and_name, parent_path, &new_name) != 0) return -1;
    int parent = find_inode_by_path(fs, parent_path);
    if (parent < 0 || fs->inodes[parent].n_type != directory) return -1;

    for (int i = 0; i < DIRECT_BLOCKS_COUNT; ++i) {
        int ch = fs->inodes[parent].direct_blocks[i];
        if (ch != -1 && strncmp(fs->inodes[ch].name, new_name, NAME_MAX_LENGTH) == 0)
            return -2;
    }

    return copy_inode(fs, src, parent, new_name);
}

int fs_import(file_system *fs, char *int_path, char *ext_path)
{
    if (!fs || !int_path || !ext_path) return -1;
    FILE *f = fopen(ext_path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *buf = malloc(size);
    if (!buf) {
        fclose(f);
        return -1;
    }
    fread(buf, 1, size, f);
    fclose(f);

    int idx = find_inode_by_path(fs, int_path);
    if (idx < 0 || fs->inodes[idx].n_type != reg_file) {
        free(buf);
        return -1;
    }

    int ret = write_bytes(fs, idx, buf, (size_t)size);
    free(buf);
    if (ret < 0) {
        return ret;
    }
    return 0;
}

int fs_export(file_system *fs, char *int_path, char *ext_path)
{
    if (!fs || !int_path || !ext_path) return -1;
    int file_size = 0;
    uint8_t *data = fs_readf(fs, int_path, &file_size);
    if (file_size == 0 && !data) return -1;
    FILE *f = fopen(ext_path, "wb");
    if (!f) {
        free(data);
        return -1;
    }
    fwrite(data, 1, file_size, f);
    fclose(f);
    free(data);
    return 0;
}
