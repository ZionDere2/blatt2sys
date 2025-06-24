#include "../lib/operations.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>



int
allocate_data_block(file_system *fs, int block_num)
{
        if (block_num < 0 || block_num >= fs->s_block->num_blocks) {
                return -1;
        }
        if (fs->free_list[block_num] == 0) {
                return -1;
        }
        fs->free_list[block_num] = 0;
        if (fs->s_block->free_blocks > 0) {
                fs->s_block->free_blocks--;
        }
        return 0;
}

int
release_data_block(file_system *fs, int block_num)
{
        if (block_num < 0 || block_num >= fs->s_block->num_blocks) {
                return -1;
        }
        if (fs->free_list[block_num] == 1) {
                return -1;
        }
        fs->free_list[block_num] = 1;
        fs->s_block->free_blocks++;
        return 0;
}

void
remove_inode_recursive(file_system *fs, int inode_no)
{
        inode *node = &fs->inodes[inode_no];

        if (node->n_type == directory) {
                for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
                        int child = node->direct_blocks[i];
                        if (child != -1) {
                                remove_inode_recursive(fs, child);
                                node->direct_blocks[i] = -1;
                        }
                }
        } else if (node->n_type == reg_file) {
                for (int i = 0; i < DIRECT_BLOCKS_COUNT; i++) {
                        int block = node->direct_blocks[i];
                        if (block != -1) {
                                release_data_block(fs, block);
                                node->direct_blocks[i] = -1;
                        }
                }
        }

        node->n_type = free_block;
        node->size = 0;
        memset(node->name, 0, NAME_MAX_LENGTH);
        node->parent = -1;
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
	return -1;
}

int
fs_export(file_system *fs, char *int_path, char *ext_path)
{
	return -1;
}
