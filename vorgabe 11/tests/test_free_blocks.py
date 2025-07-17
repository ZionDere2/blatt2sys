import ctypes
from wrappers import *

class Test_FreeBlocks:
    def test_write_and_rm_updates_free_blocks(self):
        fs = setup(5)
        fs = set_fil(name="fil1", inode=1, parent=0, parent_block=0, fs=fs)
        libc.fs_writef(ctypes.byref(fs), ctypes.c_char_p(b"/fil1"), ctypes.c_char_p(b"data"))
        assert fs.s_block.contents.free_blocks == 4
        libc.fs_rm(ctypes.byref(fs), ctypes.c_char_p(b"/fil1"))
        assert fs.s_block.contents.free_blocks == 5

    def test_remove_empty_directory_does_not_change_free_blocks(self):
        fs = setup(5)
        retval = libc.fs_mkdir(ctypes.byref(fs), ctypes.c_char_p(b"/dir1"))
        assert retval == 0
        assert fs.s_block.contents.free_blocks == 5
        libc.fs_rm(ctypes.byref(fs), ctypes.c_char_p(b"/dir1"))
        assert fs.s_block.contents.free_blocks == 5

    def test_remove_dir_with_file_restores_free_blocks(self):
        fs = setup(5)
        libc.fs_mkdir(ctypes.byref(fs), ctypes.c_char_p(b"/dir1"))
        libc.fs_mkfile(ctypes.byref(fs), ctypes.c_char_p(b"/dir1/file1"))
        libc.fs_writef(ctypes.byref(fs), ctypes.c_char_p(b"/dir1/file1"), ctypes.c_char_p(b"abc"))
        assert fs.s_block.contents.free_blocks == 4
        libc.fs_rm(ctypes.byref(fs), ctypes.c_char_p(b"/dir1"))
        assert fs.s_block.contents.free_blocks == 5

