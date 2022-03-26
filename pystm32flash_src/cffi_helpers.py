import sys
import os
from subprocess import Popen, PIPE
from cffi import FFI


def _get_library_prefix():
    if sys.platform == 'win32':
        return ''
    elif sys.platform=='cygwin':
        return ''
    else:
        return 'lib'


def _get_library_suffix():
    if sys.platform == "darwin":
        return 'dylib'
    elif sys.platform == 'win32':
        return 'dll'
    elif sys.platform == 'cygwin':
        return 'dll'
    else:
        return 'so'


def get_lib_handle(definitions, header, library, library_dir, include_dir):
    ffi = FFI()
    # command = ['cc', '-E'] + definitions + [os.path.join(include_dir, header)]
    # interface = Popen(command, stdout=PIPE).communicate()[0].decode("utf-8")
    header_file_name = os.path.join(include_dir, header)
    f = open(header_file_name, 'r')
    interface = f.read()
    f.close()

    try:
        ffi.cdef(interface)
        suffix = _get_library_suffix()
        prefix = _get_library_prefix()
        sys.path.append(library_dir)
        lib_file_name = os.path.join(library_dir,
                                     '{0}{1}.{2}'.format(prefix, library, suffix))
        print('lib_file_name:', lib_file_name)
        lib = ffi.dlopen(lib_file_name)
    except Exception as e:
        print('get_lib_handle error', e, interface)
        print(lib_file_name)

    return lib, ffi
