#include "../lib/operations.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper: find inode index for absolute path */
static int find_inode_by_path(file_system *fs, const char *path)
{
    if (!path || path[0] != '/')
        return -1;

    if (strcmp(path, "/") == 0)
        return fs->root_node;

    int current = fs->root_node;

    char tmp[strlen(path) + 1];
    strcpy(tmp, path + 1); /* skip leading '/' */

    char *token = strtok(tmp, "/");
    while (token) {
        int next = -1;
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            int c = fs->inodes[current].direct_blocks[i];
            if (c != -1 && strncmp(fs->inodes[c].name, token, NAME_MAX_LENGTH) == 0) {
                next = c;
                break;
            }
        }
        if (next == -1)
            return -1;
        current = next;
        token = strtok(NULL, "/");
    }

    return current;
}

/* Helper: find free data block */
static int find_free_block(file_system *fs)
{
    for (int i = 0; i < fs->s_block->num_blocks; i++) {
        if (fs->free_list[i])
            return i;
    }
    return -1;
}

/* Helper: attach child inode to parent */
static int add_child_inode(file_system *fs, int parent, int child)
{
    for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
        if (fs->inodes[parent].direct_blocks[i] == -1) {
            fs->inodes[parent].direct_blocks[i] = child;
            return 0;
        }
    }
    return -1;
}

/*
 * Extract the parent directory path and the final component from an absolute
 * path.  parent_out must be large enough to hold the resulting parent path.
 * On success 0 is returned and *name_out will point inside the original path
 * to the last component.
 */
static int split_path(const char *path, char *parent_out, const char **name_out)
{
    if (!path || path[0] != '/')
        return -1;

    const char *slash = strrchr(path, '/');
    if (!slash)
        return -1;

    *name_out = slash + 1;
    if (**name_out == '\0')
        return -1;

    if (slash == path) {
        strcpy(parent_out, "/");
    } else {
        size_t len = (size_t)(slash - path);
        memcpy(parent_out, path, len);
        parent_out[len] = '\0';
    }
    return 0;
}

/* Recursive inode copy (used by fs_cp) */
static int copy_inode_recursive(file_system *fs, int src, int dst_parent, const char *name)
{
    int new_idx = find_free_inode(fs);
    if (new_idx < 0)
        return -1;

    inode_init(&fs->inodes[new_idx]);

    inode *src_inode = &fs->inodes[src];
    inode *dst_inode = &fs->inodes[new_idx];

    dst_inode->n_type = src_inode->n_type;
    dst_inode->size = src_inode->size;
    strncpy(dst_inode->name, name, NAME_MAX_LENGTH);
    dst_inode->parent = dst_parent;

    if (add_child_inode(fs, dst_parent, new_idx) != 0)
        return -1;

    if (src_inode->n_type == reg_file) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            int b = src_inode->direct_blocks[i];
            if (b != -1) {
                int nb = find_free_block(fs);
                if (nb == -1)
                    return -1;
                fs->free_list[nb] = 0;
                fs->data_blocks[nb].size = fs->data_blocks[b].size;
                memcpy(fs->data_blocks[nb].block, fs->data_blocks[b].block,
                       fs->data_blocks[b].size);
                dst_inode->direct_blocks[i] = nb;
            }
        }
    } else if (src_inode->n_type == directory) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            int child = src_inode->direct_blocks[i];
            if (child != -1) {
                copy_inode_recursive(fs, child, new_idx, fs->inodes[child].name);
            }
        }
    }

    return new_idx;
}

/* Recursive inode removal */
static void remove_inode_recursive(file_system *fs, int idx)
{
    inode *node = &fs->inodes[idx];

    if (node->n_type == directory) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            int child = node->direct_blocks[i];
            if (child != -1)
                remove_inode_recursive(fs, child);
        }
    } else if (node->n_type == reg_file) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            int b = node->direct_blocks[i];
            if (b != -1) {
                fs->free_list[b] = 1;
                fs->data_blocks[b].size = 0;
                node->direct_blocks[i] = -1;
            }
        }
    }

    int parent = node->parent;
    if (parent != -1) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            if (fs->inodes[parent].direct_blocks[i] == idx)
                fs->inodes[parent].direct_blocks[i] = -1;
        }
    }

    inode_init(node);
}



int
fs_mkdir(file_system *fs, char *path)
{
    if (!fs || !path || path[0] != '/')
        return -1;

    char parent_path[strlen(path) + 1];
    const char *name;
    if (split_path(path, parent_path, &name) != 0)
        return -1;

    int parent = find_inode_by_path(fs, parent_path);
    if (parent < 0 || fs->inodes[parent].n_type != directory)
        return -1;

    for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
        int ch = fs->inodes[parent].direct_blocks[i];
        if (ch != -1 && strncmp(fs->inodes[ch].name, name, NAME_MAX_LENGTH) == 0)
            return -1;
    }

    int free_i = find_free_inode(fs);
    if (free_i < 0)
        return -1;

    inode_init(&fs->inodes[free_i]);
    fs->inodes[free_i].n_type = directory;
    strncpy(fs->inodes[free_i].name, name, NAME_MAX_LENGTH);
    fs->inodes[free_i].parent = parent;

    if (add_child_inode(fs, parent, free_i) != 0)
        return -1;

    return 0;
}

int
fs_mkfile(file_system *fs, char *path_and_name)
{
    if (!fs || !path_and_name || path_and_name[0] != '/')
        return -1;

    char parent_path[strlen(path_and_name) + 1];
    const char *name;
    if (split_path(path_and_name, parent_path, &name) != 0)
        return -1;

    int parent = find_inode_by_path(fs, parent_path);
    if (parent < 0 || fs->inodes[parent].n_type != directory)
        return -1;

    for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
        int ch = fs->inodes[parent].direct_blocks[i];
        if (ch != -1 && strncmp(fs->inodes[ch].name, name, NAME_MAX_LENGTH) == 0)
            return -2;
    }

    int free_i = find_free_inode(fs);
    if (free_i < 0)
        return -1;

    inode_init(&fs->inodes[free_i]);
    fs->inodes[free_i].n_type = reg_file;
    strncpy(fs->inodes[free_i].name, name, NAME_MAX_LENGTH);
    fs->inodes[free_i].parent = parent;

    if (add_child_inode(fs, parent, free_i) != 0)
        return -1;

    return 0;
}

int
fs_cp(file_system *fs, char *src_path, char *dst_path_and_name)
{
    if (!fs || !src_path || !dst_path_and_name || src_path[0] != '/' ||
        dst_path_and_name[0] != '/')
        return -1;

    int src_idx = find_inode_by_path(fs, src_path);
    if (src_idx < 0)
        return -1;

    char parent_path[strlen(dst_path_and_name) + 1];
    const char *name;
    if (split_path(dst_path_and_name, parent_path, &name) != 0)
        return -1;

    int parent = find_inode_by_path(fs, parent_path);
    if (parent < 0 || fs->inodes[parent].n_type != directory)
        return -1;

    for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
        int ch = fs->inodes[parent].direct_blocks[i];
        if (ch != -1 && strncmp(fs->inodes[ch].name, name, NAME_MAX_LENGTH) == 0)
            return -2;
    }

    if (copy_inode_recursive(fs, src_idx, parent, name) < 0)
        return -1;

    return 0;
}

char *
fs_list(file_system *fs, char *path)
{
    if (!fs || !path || path[0] != '/')
        return NULL;

    int dir = find_inode_by_path(fs, path);
    if (dir < 0 || fs->inodes[dir].n_type != directory)
        return NULL;

    /* determine how many entries reside in the directory */
    size_t count = 0;
    for (int i = 0; i < fs->s_block->num_blocks; i++) {
        if (fs->inodes[i].parent == dir && fs->inodes[i].n_type != free_block)
            count++;
    }

    size_t bufsize = (NAME_MAX_LENGTH + 5) * (count + 1);
    char *out = malloc(bufsize);
    if (!out)
        return NULL;
    out[0] = '\0';

    for (int i = 0; i < fs->s_block->num_blocks; i++) {
        if (fs->inodes[i].parent == dir && fs->inodes[i].n_type != free_block) {
            strcat(out, fs->inodes[i].n_type == directory ? "DIR " : "FIL ");
            strncat(out, fs->inodes[i].name, NAME_MAX_LENGTH);
            strcat(out, "\n");
        }
    }

    return out;
}

int
fs_writef(file_system *fs, char *filename, char *text)
{
    if (!fs || !filename || !text || filename[0] != '/')
        return -1;

    int idx = find_inode_by_path(fs, filename);
    if (idx < 0 || fs->inodes[idx].n_type != reg_file)
        return -1;

    inode *node = &fs->inodes[idx];
    size_t len = strlen(text);
    size_t remaining = len;
    size_t written = 0;

    for (int i = 0; i < DIRECT_BLOCKS_COUNT && remaining > 0; i++) {
        int b = node->direct_blocks[i];
        if (b == -1) {
            b = find_free_block(fs);
            if (b == -1)
                return -2;
            node->direct_blocks[i] = b;
            fs->free_list[b] = 0;
            fs->data_blocks[b].size = 0;
        }

        data_block *db = &fs->data_blocks[b];
        size_t free_space = BLOCK_SIZE - db->size;
        size_t to_write = MIN(free_space, remaining);
        memcpy(db->block + db->size, text + written, to_write);
        db->size += to_write;
        remaining -= to_write;
        written += to_write;
        node->size += to_write;
    }

    if (remaining > 0)
        return -2;

    return written;
}

uint8_t *
fs_readf(file_system *fs, char *filename, int *file_size)
{
    if (!fs || !filename || filename[0] != '/')
        return NULL;

    int idx = find_inode_by_path(fs, filename);
    if (idx < 0 || fs->inodes[idx].n_type != reg_file) {
        if (file_size)
            *file_size = 0;
        return NULL;
    }

    inode *node = &fs->inodes[idx];
    if (file_size)
        *file_size = node->size;
    if (node->size == 0)
        return NULL;

    uint8_t *buf = malloc(node->size);
    if (!buf)
        return NULL;

    size_t offset = 0;
    for (int i = 0; i < DIRECT_BLOCKS_COUNT && offset < node->size; i++) {
        int b = node->direct_blocks[i];
        if (b != -1) {
            data_block *db = &fs->data_blocks[b];
            memcpy(buf + offset, db->block, db->size);
            offset += db->size;
        }
    }

    return buf;
}


int
fs_rm(file_system *fs, char *path)
{
    if (!fs || !path || path[0] != '/')
        return -1;

    int idx = find_inode_by_path(fs, path);
    if (idx < 0 || idx == fs->root_node)
        return -1;

    remove_inode_recursive(fs, idx);
    return 0;
}

int
fs_import(file_system *fs, char *int_path, char *ext_path)
{
    if (!fs || !int_path || !ext_path)
        return -1;

    FILE *f = fopen(ext_path, "r");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char *buf = NULL;
    if (size > 0) {
        buf = malloc(size + 1);
        fread(buf, 1, size, f);
        buf[size] = '\0';
    } else {
        buf = calloc(1, 1);
    }
    fclose(f);

    int ret = fs_writef(fs, int_path, buf);
    free(buf);

    return ret < 0 ? -1 : 0;
}

int
fs_export(file_system *fs, char *int_path, char *ext_path)
{
    if (!fs || !int_path || !ext_path)
        return -1;

    int idx = find_inode_by_path(fs, int_path);
    if (idx < 0 || fs->inodes[idx].n_type != reg_file)
        return -1;

    int size = 0;
    uint8_t *data = fs_readf(fs, int_path, &size);
    FILE *f = fopen(ext_path, "w");
    if (!f) {
        if (data)
            free(data);
        return -1;
    }
    if (data && size > 0)
        fwrite(data, 1, size, f);
    if (data)
        free(data);
    fclose(f);
    return 0;
}
