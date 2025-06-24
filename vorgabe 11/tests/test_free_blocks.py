import ctypes
from wrappers import *

class TestFreeBlocks:
    def test_alloc_and_free(self):
        fs = setup(5)
        fs = set_fil(name="fil1", inode=1, parent=0, parent_block=0, fs=fs)
        fs = set_data_block_with_string(block_num=0, string_data="abc", parent_inode=1, parent_block_num=0, fs=fs)
        assert fs.s_block.contents.free_blocks == 4
        fs = free_block(0, fs)
        assert fs.s_block.contents.free_blocks == 5

    def test_remove_inode_updates_counter(self):
        fs = setup(5)
        fs = set_fil(name="fil1", inode=1, parent=0, parent_block=0, fs=fs)
        fs = set_data_block_with_string(block_num=0, string_data="abc", parent_inode=1, parent_block_num=0, fs=fs)
        remove_inode_rec(1, fs)
        assert fs.s_block.contents.free_blocks == 5
        assert fs.inodes[1].n_type == NodeType.free_block

