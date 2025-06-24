#include "../lib/operations.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The given code base only contained stubs for all required
 * filesystem operations.  For the test-cases we implement a
 * minimal set of helper functions which allow to manage a very
 * small in-memory filesystem.  The implementation is deliberately
 * simple and tailored to the unit tests shipped with the
 * repository.
 */

/*--------------------------------------------------------------*/
/*                      Helper functions                        */
/*--------------------------------------------------------------*/

/* Split a path of form "/a/b/c" into parent path and name.
 * The caller has to free both returned strings.  On failure -1 is
 * returned. */
static int
split_path(const char *path, char **parent_out, char **name_out)
{
    if (!path || path[0] != '/')
        return -1;

    char *tmp = strdup(path);
    if (!tmp)
        return -1;

    char *last = strrchr(tmp, '/');
    if (!last || last == tmp) {
        /* parent is root */
        *name_out = strdup(last ? last + 1 : tmp);
        *parent_out = strdup("/");
        free(tmp);
        return (*name_out && *parent_out) ? 0 : -1;
    }

    *name_out = strdup(last + 1);
    *last = '\0';
    *parent_out = strdup(tmp);
    free(tmp);
    if (!*name_out || !*parent_out)
        return -1;
    return 0;
}

/* Return the inode index for a given path or -1 on error */
static int
inode_from_path(file_system *fs, const char *path)
{
    if (!path || path[0] != '/')
        return -1;

    if (strcmp(path, "/") == 0)
        return fs->root_node;

    char *dup = strdup(path);
    if (!dup)
        return -1;

    int current = fs->root_node;
    char *token = strtok(dup + 1, "/");
    while (token) {
        inode *cur = &fs->inodes[current];
        if (cur->n_type != directory) {
            free(dup);
            return -1;
        }
        int next = -1;
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            int idx = cur->direct_blocks[i];
            if (idx != -1 && strncmp(fs->inodes[idx].name, token, NAME_MAX_LENGTH) == 0) {
                next = idx;
                break;
            }
        }
        if (next == -1) {
            free(dup);
            return -1;
        }
        current = next;
        token = strtok(NULL, "/");
    }
    free(dup);
    return current;
}

/* Find a free entry in the parent's direct blocks array */
static int
add_child(inode *parent, int child)
{
    for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
        if (parent->direct_blocks[i] == -1) {
            parent->direct_blocks[i] = child;
            return 0;
        }
    }
    return -1;
}

static void
remove_child(inode *parent, int child)
{
    for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++)
        if (parent->direct_blocks[i] == child)
            parent->direct_blocks[i] = -1;
}

/* Find and mark a free data block */
static int
alloc_block(file_system *fs)
{
    for (uint32_t i = 0; i < fs->s_block->num_blocks; i++) {
        if (fs->free_list[i]) {
            fs->free_list[i] = 0;
            fs->data_blocks[i].size = 0;
            return i;
        }
    }
    return -1;
}

static void
release_block(file_system *fs, int idx)
{
    if (idx < 0)
        return;
    fs->free_list[idx] = 1;
    fs->data_blocks[idx].size = 0;
    memset(fs->data_blocks[idx].block, 0, BLOCK_SIZE);
}

/*--------------------------------------------------------------*/

/* Recursive copy helper. Returns new inode index or -1 on error */
static int
copy_inode_recursive(file_system *fs, int src_idx, int parent_idx, const char *name,
                     int *alloc_inodes, int *alloc_parents, int *inode_count,
                     int *alloc_blocks, int *block_count)
{
    int new_idx = find_free_inode(fs);
    if (new_idx == -1)
        return -1;

    if (add_child(&fs->inodes[parent_idx], new_idx) != 0)
        return -1;

    inode_init(&fs->inodes[new_idx]);
    inode *src = &fs->inodes[src_idx];
    inode *dst = &fs->inodes[new_idx];
    dst->n_type = src->n_type;
    strncpy(dst->name, name, NAME_MAX_LENGTH);
    dst->parent = parent_idx;

    alloc_inodes[*inode_count] = new_idx;
    alloc_parents[*inode_count] = parent_idx;
    (*inode_count)++;

    if (src->n_type == reg_file) {
        dst->size = src->size;
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            int b = src->direct_blocks[i];
            if (b == -1)
                break;
            int nb = alloc_block(fs);
            if (nb == -1)
                return -1;
            dst->direct_blocks[i] = nb;
            alloc_blocks[(*block_count)++] = nb;
            fs->data_blocks[nb] = fs->data_blocks[b];
        }
    } else if (src->n_type == directory) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            int child = src->direct_blocks[i];
            if (child == -1)
                continue;
            if (copy_inode_recursive(fs, child, new_idx,
                                     fs->inodes[child].name,
                                     alloc_inodes, alloc_parents, inode_count,
                                     alloc_blocks, block_count) == -1)
                return -1;
        }
    }

    return new_idx;
}


int
fs_mkdir(file_system *fs, char *path)
{
    if (!fs || !path || path[0] != '/')
        return -1;

    char *parent_path = NULL;
    char *name = NULL;
    if (split_path(path, &parent_path, &name) != 0)
        return -1;

    int parent_idx = inode_from_path(fs, parent_path);
    free(parent_path);
    if (parent_idx == -1) {
        free(name);
        return -1;
    }

    inode *parent = &fs->inodes[parent_idx];
    if (parent->n_type != directory) {
        free(name);
        return -1;
    }

    /* check if entry already exists */
    for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
        int idx = parent->direct_blocks[i];
        if (idx != -1 && strncmp(fs->inodes[idx].name, name, NAME_MAX_LENGTH) == 0) {
            free(name);
            return -2;
        }
    }

    int new_idx = find_free_inode(fs);
    if (new_idx == -1) {
        free(name);
        return -1;
    }

    if (add_child(parent, new_idx) != 0) {
        free(name);
        return -1;
    }

    inode_init(&fs->inodes[new_idx]);
    fs->inodes[new_idx].n_type = directory;
    strncpy(fs->inodes[new_idx].name, name, NAME_MAX_LENGTH);
    fs->inodes[new_idx].parent = parent_idx;
    free(name);
    return 0;
}

int
fs_mkfile(file_system *fs, char *path_and_name)
{
    if (!fs || !path_and_name || path_and_name[0] != '/')
        return -1;

    char *parent_path = NULL;
    char *name = NULL;
    if (split_path(path_and_name, &parent_path, &name) != 0)
        return -1;

    int parent_idx = inode_from_path(fs, parent_path);
    free(parent_path);
    if (parent_idx == -1) {
        free(name);
        return -1;
    }

    inode *parent = &fs->inodes[parent_idx];
    if (parent->n_type != directory) {
        free(name);
        return -1;
    }

    for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
        int idx = parent->direct_blocks[i];
        if (idx != -1 && strncmp(fs->inodes[idx].name, name, NAME_MAX_LENGTH) == 0) {
            free(name);
            return -2;
        }
    }

    int new_idx = find_free_inode(fs);
    if (new_idx == -1) {
        free(name);
        return -1;
    }

    if (add_child(parent, new_idx) != 0) {
        free(name);
        return -1;
    }

    inode_init(&fs->inodes[new_idx]);
    fs->inodes[new_idx].n_type = reg_file;
    strncpy(fs->inodes[new_idx].name, name, NAME_MAX_LENGTH);
    fs->inodes[new_idx].parent = parent_idx;
    free(name);
    return 0;
}

int
fs_cp(file_system *fs, char *src_path, char *dst_path_and_name)
{
    if (!fs || !src_path || !dst_path_and_name)
        return -1;

    int src_idx = inode_from_path(fs, src_path);
    if (src_idx == -1)
        return -1;

    char *parent_path = NULL;
    char *name = NULL;
    if (split_path(dst_path_and_name, &parent_path, &name) != 0)
        return -1;

    int parent_idx = inode_from_path(fs, parent_path);
    free(parent_path);
    if (parent_idx == -1) {
        free(name);
        return -1;
    }

    inode *parent = &fs->inodes[parent_idx];
    if (parent->n_type != directory) {
        free(name);
        return -1;
    }

    for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
        int idx = parent->direct_blocks[i];
        if (idx != -1 && strncmp(fs->inodes[idx].name, name, NAME_MAX_LENGTH) == 0) {
            free(name);
            return -2;
        }
    }

    /* arrays to track allocations for rollback */
    int max = fs->s_block->num_blocks;
    int *alloc_inodes = calloc(max, sizeof(int));
    int *alloc_parents = calloc(max, sizeof(int));
    int *alloc_blocks = calloc(max, sizeof(int));
    int inode_count = 0;
    int block_count = 0;
    if (!alloc_inodes || !alloc_blocks || !alloc_parents) {
        free(name);
        free(alloc_inodes); free(alloc_blocks); free(alloc_parents);
        return -1;
    }

    /* recursive copy helper */
    int copy_inode_recursive(file_system *, int, int, const char *,
                             int *, int *, int *, int *, int *);

    int res = copy_inode_recursive(fs, src_idx, parent_idx, name,
                                   alloc_inodes, alloc_parents,
                                   &inode_count, alloc_blocks, &block_count);

    if (res == -1) {
        /* rollback */
        for (int i = 0; i < block_count; i++)
            release_block(fs, alloc_blocks[i]);
        for (int i = inode_count - 1; i >= 0; i--) {
            remove_child(&fs->inodes[alloc_parents[i]], alloc_inodes[i]);
            inode_init(&fs->inodes[alloc_inodes[i]]);
        }
        free(alloc_inodes); free(alloc_blocks); free(alloc_parents); free(name);
        return -1;
    }

    free(alloc_inodes); free(alloc_blocks); free(alloc_parents); free(name);
    return 0;
}

char *
fs_list(file_system *fs, char *path)
{
    if (!fs || !path)
        return NULL;

    int idx = inode_from_path(fs, path);
    if (idx == -1)
        return NULL;

    inode *node = &fs->inodes[idx];
    if (node->n_type != directory)
        return NULL;

    /* allocate a generous buffer */
    size_t buf_size = DIRECT_BLOCKS_COUNT * (NAME_MAX_LENGTH + 5) + 1;
    char *out = calloc(1, buf_size);
    if (!out)
        return NULL;

    for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
        int c = node->direct_blocks[i];
        if (c == -1)
            continue;
        inode *child = &fs->inodes[c];
        strncat(out, child->n_type == directory ? "DIR " : "FIL ", buf_size - strlen(out) - 1);
        strncat(out, child->name, buf_size - strlen(out) - 1);
        strncat(out, "\n", buf_size - strlen(out) - 1);
    }

    return out;
}

int
fs_writef(file_system *fs, char *filename, char *text)
{
    if (!fs || !filename || !text)
        return -1;

    int idx = inode_from_path(fs, filename);
    if (idx == -1)
        return -1;

    inode *file = &fs->inodes[idx];
    if (file->n_type != reg_file)
        return -1;

    int written = 0;
    size_t len = strlen(text);
    size_t pos = 0;

    while (pos < len) {
        int block_idx = -1;
        int block_slot = -1;
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            if (file->direct_blocks[i] == -1) {
                block_slot = i;
                break;
            }
        }

        if (block_slot == -1)
            return -2;

        /* reuse last block if still space */
        if (block_slot > 0 && file->direct_blocks[block_slot - 1] != -1 &&
            fs->data_blocks[file->direct_blocks[block_slot - 1]].size < BLOCK_SIZE) {
            block_idx = file->direct_blocks[block_slot - 1];
            data_block *db = &fs->data_blocks[block_idx];
            size_t copy = MIN(len - pos, BLOCK_SIZE - db->size);
            memcpy(&db->block[db->size], text + pos, copy);
            db->size += copy;
            pos += copy;
            written += copy;
            file->size += copy;
            continue;
        }

        block_idx = alloc_block(fs);
        if (block_idx == -1)
            return -2;
        file->direct_blocks[block_slot] = block_idx;
        data_block *db = &fs->data_blocks[block_idx];
        size_t copy = MIN(len - pos, BLOCK_SIZE);
        memcpy(db->block, text + pos, copy);
        db->size = copy;
        pos += copy;
        written += copy;
        file->size += copy;
    }

    return written;
}

uint8_t *
fs_readf(file_system *fs, char *filename, int *file_size)
{
    if (!fs || !filename || !file_size)
        return NULL;

    int idx = inode_from_path(fs, filename);
    if (idx == -1)
        return NULL;

    inode *file = &fs->inodes[idx];
    if (file->n_type != reg_file) {
        *file_size = 0;
        return NULL;
    }

    *file_size = file->size;
    if (file->size == 0)
        return NULL;

    uint8_t *buf = malloc(file->size);
    if (!buf)
        return NULL;

    int written = 0;
    for (int i = 0; i < DIRECT_BLOCKS_COUNT && written < file->size; i++) {
        int b = file->direct_blocks[i];
        if (b == -1)
            break;
        data_block *db = &fs->data_blocks[b];
        memcpy(buf + written, db->block, db->size);
        written += db->size;
    }
    return buf;
}


int
fs_rm(file_system *fs, char *path)
{
    if (!fs || !path || path[0] != '/')
        return -1;

    int idx = inode_from_path(fs, path);
    if (idx == -1 || idx == fs->root_node)
        return -1;

    inode *node = &fs->inodes[idx];
    inode *parent = &fs->inodes[node->parent];

    if (node->n_type == reg_file) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            int b = node->direct_blocks[i];
            if (b != -1)
                release_block(fs, b);
            node->direct_blocks[i] = -1;
        }
    } else if (node->n_type == directory) {
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            int child = node->direct_blocks[i];
            if (child != -1) {
                char child_path[NAME_MAX_LENGTH * 4];
                snprintf(child_path, sizeof(child_path), "%s/%s", path, fs->inodes[child].name);
                fs_rm(fs, child_path);
            }
        }
    }

    remove_child(parent, idx);
    inode_init(node);
    return 0;
}

int
fs_import(file_system *fs, char *int_path, char *ext_path)
{
    if (!fs || !int_path || !ext_path)
        return -1;

    int idx = inode_from_path(fs, int_path);
    if (idx == -1)
        return -1;

    inode *file = &fs->inodes[idx];
    if (file->n_type != reg_file)
        return -1;

    FILE *f = fopen(ext_path, "r");
    if (!f)
        return -1;

    /* remove old data */
    for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
        if (file->direct_blocks[i] != -1)
            release_block(fs, file->direct_blocks[i]);
        file->direct_blocks[i] = -1;
    }
    file->size = 0;

    char buffer[BLOCK_SIZE];
    size_t read;
    while ((read = fread(buffer, 1, BLOCK_SIZE, f)) > 0) {
        int b = alloc_block(fs);
        if (b == -1) {
            fclose(f);
            return -1;
        }
        int slot = 0;
        while (slot < DIRECT_BLOCKS_COUNT && file->direct_blocks[slot] != -1)
            slot++;
        if (slot == DIRECT_BLOCKS_COUNT) {
            release_block(fs, b);
            fclose(f);
            return -1;
        }
        file->direct_blocks[slot] = b;
        memcpy(fs->data_blocks[b].block, buffer, read);
        fs->data_blocks[b].size = read;
        file->size += read;
    }

    fclose(f);
    return 0;
}

int
fs_export(file_system *fs, char *int_path, char *ext_path)
{
    if (!fs || !int_path || !ext_path)
        return -1;

    int idx = inode_from_path(fs, int_path);
    if (idx == -1)
        return -1;

    inode *file = &fs->inodes[idx];
    if (file->n_type != reg_file)
        return -1;

    FILE *f = fopen(ext_path, "w");
    if (!f)
        return -1;

    for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
        int b = file->direct_blocks[i];
        if (b == -1)
            break;
        fwrite(fs->data_blocks[b].block, fs->data_blocks[b].size, 1, f);
    }

    fclose(f);
    return 0;
}
