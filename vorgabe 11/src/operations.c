#include "../lib/operations.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Helper that resolves a path to an inode index. Returns -1 if the
 * path could not be resolved.
 */
static int path_to_inode(file_system *fs, const char *path)
{
    if (fs == NULL || path == NULL || path[0] != '/')
        return -1;

    // copy path because strtok modifies the string
    char tmp[strlen(path) + 1];
    strcpy(tmp, path);

    int current = fs->root_node;
    char *token = strtok(tmp, "/");
    while (token) {
        int next = -1;
        inode *node = &fs->inodes[current];
        for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
            int idx = node->direct_blocks[i];
            if (idx == -1)
                continue;
            if (strncmp(fs->inodes[idx].name, token, NAME_MAX_LENGTH) == 0) {
                next = idx;
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



int
fs_mkdir(file_system *fs, char *path)
{
	return -1;
}

int
fs_mkfile(file_system *fs, char *path_and_name)
{
	return -1;
}

int
fs_cp(file_system *fs, char *src_path, char *dst_path_and_name)
{
	return -1;
}

char *
fs_list(file_system *fs, char *path)
{
	return NULL;
}

int
fs_writef(file_system *fs, char *filename, char *text)
{
	return -1;
}

uint8_t *
fs_readf(file_system *fs, char *filename, int *file_size)
{
	return NULL;
}


int
fs_rm(file_system *fs, char *path)
{
	return -1;
}

int
fs_import(file_system *fs, char *int_path, char *ext_path)
{
    int inode_idx = path_to_inode(fs, int_path);
    if (inode_idx < 0)
        return -1;

    inode *node = &fs->inodes[inode_idx];
    if (node->n_type != reg_file)
        return -1;

    FILE *f = fopen(ext_path, "rb");
    if (!f)
        return -1;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize < 0) {
        fclose(f);
        return -1;
    }

    uint8_t *buffer = malloc((size_t)fsize);
    if (!buffer) {
        fclose(f);
        return -1;
    }
    size_t r = fread(buffer, 1, (size_t)fsize, f);
    fclose(f);
    if (r != (size_t)fsize) {
        free(buffer);
        return -1;
    }

    node->size = (uint16_t)fsize;
    size_t remaining = (size_t)fsize;
    size_t offset = 0;
    int d_idx = 0;
    while (remaining > 0 && d_idx < DIRECT_BLOCKS_COUNT) {
        int free_block = -1;
        for (int i = 0; i < fs->s_block->num_blocks; i++) {
            if (fs->free_list[i] == 1) {
                free_block = i;
                break;
            }
        }
        if (free_block == -1) {
            free(buffer);
            return -1;
        }

        fs->free_list[free_block] = 0;
        size_t to_copy = MIN(remaining, BLOCK_SIZE);
        memcpy(fs->data_blocks[free_block].block, buffer + offset, to_copy);
        fs->data_blocks[free_block].size = to_copy;
        node->direct_blocks[d_idx++] = free_block;
        offset += to_copy;
        remaining -= to_copy;
    }

    free(buffer);
    if (remaining > 0)
        return -1;

    return 0;
}

int
fs_export(file_system *fs, char *int_path, char *ext_path)
{
    int inode_idx = path_to_inode(fs, int_path);
    if (inode_idx < 0)
        return -1;

    inode *node = &fs->inodes[inode_idx];
    if (node->n_type != reg_file)
        return -1;

    FILE *f = fopen(ext_path, "wb");
    if (!f)
        return -1;

    for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
        int db = node->direct_blocks[i];
        if (db == -1)
            break;
        fwrite(fs->data_blocks[db].block, fs->data_blocks[db].size, 1, f);
    }

    fclose(f);
    return 0;
}
