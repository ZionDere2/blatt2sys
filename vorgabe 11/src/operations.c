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
