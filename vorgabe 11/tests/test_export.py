import ctypes
from wrappers import *

class Test_Expo:
    # Set up a filesystem with some data in it. Then call export on that data. Check if that exported data is same as originally set.
    def test_export_simple(self):
        fs = setup(5)
        fs = set_fil(name="fil1",inode=1,parent=0,parent_block=0,fs=fs)
        fs = set_data_block_with_string(block_num=0,string_data=SHORT_DATA,parent_inode=1,parent_block_num=0,fs=fs)

        retval = libc.fs_export(ctypes.byref(fs), ctypes.c_char_p(bytes("/fil1","utf-8")),ctypes.c_char_p(bytes(DEFAULT_TEST_FILE_NAME,"utf-8")))

        assert read_temp_file() == SHORT_DATA
        assert retval == 0

        delete_temp_file()

    # exporting a non-existing file should return -1
    def test_export_missing_internal(self):
        fs = setup(5)
        retval = libc.fs_export(ctypes.byref(fs), ctypes.c_char_p(b"/nofile"), ctypes.c_char_p(bytes(DEFAULT_TEST_FILE_NAME, "utf-8")))
        assert retval == -1

    # exporting to a path without space should also return -1
    def test_export_insufficient_space(self):
        fs = setup(5)
        fs = set_fil(name="fil1",inode=1,parent=0,parent_block=0,fs=fs)
        fs = set_data_block_with_string(block_num=0,string_data=SHORT_DATA,parent_inode=1,parent_block_num=0,fs=fs)
        retval = libc.fs_export(ctypes.byref(fs), ctypes.c_char_p(b"/fil1"), ctypes.c_char_p(b"/dev/full"))
        assert retval == -1

    def test_export_longer(self):
        fs = setup(5)
        fs = set_fil(name="fil1",inode=1,parent=0,parent_block=0,fs=fs)
        fs = set_data_block_with_string(block_num=0,string_data=LONG_DATA[:1024],parent_inode=1,parent_block_num=0,fs=fs)
        fs = set_data_block_with_string(block_num=1,string_data=LONG_DATA[1024:],parent_inode=1,parent_block_num=1,fs=fs)
        retval = libc.fs_export(ctypes.byref(fs), ctypes.c_char_p(bytes("/fil1","utf-8")),ctypes.c_char_p(bytes(DEFAULT_TEST_FILE_NAME,"utf-8")))

        assert read_temp_file() == LONG_DATA
        assert retval == 0
        
        delete_temp_file()
