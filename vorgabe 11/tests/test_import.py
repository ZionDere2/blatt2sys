import ctypes
from wrappers import *


class Test_Imp:
    # Creates a file, fills it with some short text, then imports it to an existing (empty) file in the fs
    # Expected behaviour:
    #  * The operation is successful, therefor retval is 0
    #  * The test data is located in the first datablock
    #  * the filesize and the datablock size are set correctly (to the length of text in the file)
    #  * the first datablock is marked as blocked in the free list
    def test_import_simple(self):
        fs = setup(5)
        fs = set_fil(name="fil1",inode=1,parent=0,parent_block=0,fs=fs)
        filename = create_temp_file(data=SHORT_DATA)
        retval = libc.fs_import(ctypes.byref(fs), ctypes.c_char_p(bytes("/fil1","UTF-8")),ctypes.c_char_p(bytes(filename,"utf-8")))

        assert retval == 0
        assert fs.inodes[1].direct_blocks[0] == 0 # the data should be written in the first possible block
        assert fs.free_list[0] == 0
        outstring = ctypes.c_char_p(ctypes.addressof(fs.data_blocks[0].block)).value #convert the raw data block to a string
        assert outstring.decode("utf-8") == SHORT_DATA
        assert fs.data_blocks[0].size == len(SHORT_DATA)
        assert fs.inodes[1].size == len(SHORT_DATA)

        delete_temp_file()


    def test_import_bigger_file(self):
        fs = setup(5)
        fs = set_fil(name="fil1",inode=1,parent=0,parent_block=0,fs=fs)
        filename = create_temp_file(data=LONG_DATA)
        retval = libc.fs_import(ctypes.byref(fs),ctypes.c_char_p(bytes("/fil1","UTF-8")),ctypes.c_char_p(bytes(filename,"utf-8")))

        assert retval == 0
        assert fs.inodes[1].direct_blocks[0] == 0 # the data should be written in the first possible block
        assert fs.inodes[1].direct_blocks[1] == 1 # and the second block
        assert fs.free_list[0] == 0
        assert fs.free_list[1] == 0

        outstring1 = bytearray(ctypes.c_char_p(ctypes.addressof(fs.data_blocks[0].block)).value) #convert the raw data block to a string
        outstring1 = outstring1[:1024] # needed reassignment because there is no terminating 0 byte in the data block
        outstring2 = ctypes.c_char_p(ctypes.addressof(fs.data_blocks[1].block)).value #convert the raw data block to a string
        assert outstring1.decode("utf-8")+outstring2.decode("utf-8") == LONG_DATA
        delete_temp_file()


    # invalid internal path should return -1 and not change filesystem
    def test_import_invalid_path(self):
        fs = setup(5)
        filename = create_temp_file(data=SHORT_DATA)
        retval = libc.fs_import(ctypes.byref(fs), ctypes.c_char_p(b"fil1"), ctypes.c_char_p(bytes(filename, "utf-8")))
        assert retval == -1
        assert fs.inodes[0].direct_blocks[0] == -1
        delete_temp_file()

    # missing external file should fail with -1
    def test_import_missing_external(self):
        fs = setup(5)
        fs = set_fil(name="fil1",inode=1,parent=0,parent_block=0,fs=fs)
        retval = libc.fs_import(ctypes.byref(fs), ctypes.c_char_p(b"/fil1"), ctypes.c_char_p(b"/no/such/file"))
        assert retval == -1
        assert fs.inodes[1].direct_blocks[0] == -1

    # insufficient space should return -2
    def test_import_insufficient_space(self):
        fs = setup(5)
        fs = set_fil(name="dummy1",inode=1,parent=0,parent_block=0,fs=fs)
        fs = set_fil(name="dummy2",inode=2,parent=0,parent_block=1,fs=fs)
        fs = set_fil(name="dummy3",inode=3,parent=0,parent_block=2,fs=fs)
        set_data_block_with_string(block_num=0,string_data="a",parent_inode=1,parent_block_num=0,fs=fs)
        set_data_block_with_string(block_num=1,string_data="b",parent_inode=2,parent_block_num=0,fs=fs)
        set_data_block_with_string(block_num=2,string_data="c",parent_inode=3,parent_block_num=0,fs=fs)
        set_data_block_with_string(block_num=3,string_data="d",parent_inode=3,parent_block_num=1,fs=fs)
        fs = set_fil(name="target",inode=4,parent=0,parent_block=3,fs=fs)
        filename = create_temp_file(data=LONG_DATA)
        retval = libc.fs_import(ctypes.byref(fs), ctypes.c_char_p(b"/target"), ctypes.c_char_p(bytes(filename, "utf-8")))
        assert retval == -2
        assert fs.inodes[4].direct_blocks[0] == -1
        delete_temp_file()
