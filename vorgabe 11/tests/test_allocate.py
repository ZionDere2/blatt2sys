import ctypes
from wrappers import *

class TestAllocateBlock:
    def test_allocate_simple(self):
        fs = setup(5)
        idx = libc.allocate_data_block(ctypes.byref(fs))
        assert idx == 0
        assert fs.free_list[idx] == 0
        # block should be zeroed
        data = bytes(fs.data_blocks[idx].block)
        assert data == b"\x00" * BLOCK_SIZE
        # next allocation should give next block
        idx2 = libc.allocate_data_block(ctypes.byref(fs))
        assert idx2 == 1
        assert fs.free_list[idx2] == 0
        assert fs.s_block.contents.free_blocks == 3

