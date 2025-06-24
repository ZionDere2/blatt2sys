import ctypes
import hashlib
from wrappers import *

class Test_Binary_ImpExp:
    def test_binary_import_export(self):
        fs = setup(5)
        fs = set_fil(name="fil1", inode=1, parent=0, parent_block=0, fs=fs)
        data = bytes(range(256))
        src = create_temp_binary_file(data=data, filename="temp_bin_in")
        retval = libc.fs_import(ctypes.byref(fs), ctypes.c_char_p(b"/fil1"), ctypes.c_char_p(bytes(src, "utf-8")))
        assert retval == 0
        out = "temp_bin_out"
        retval = libc.fs_export(ctypes.byref(fs), ctypes.c_char_p(b"/fil1"), ctypes.c_char_p(bytes(out, "utf-8")))
        assert retval == 0
        orig = read_temp_binary_file(src)
        exported = read_temp_binary_file(out)
        assert hashlib.md5(orig).digest() == hashlib.md5(exported).digest()
        delete_temp_file(src)
        delete_temp_file(out)
