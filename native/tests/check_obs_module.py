"""Load the packaged plugin using a real Windows OBS distribution, without launching its UI.

python native/tests/check_obs_module.py <OBS-root> <package-root>
"""
import ctypes as c
import os
from pathlib import Path
import sys
import tempfile


def main():
    obs_root, package = (Path(p).resolve() for p in sys.argv[1:])
    obs_bin = obs_root / 'bin' / '64bit'
    dll = package / 'obs-plugins' / '64bit' / 'sf6-tracker-native.dll'
    data = package / 'data' / 'obs-plugins' / 'sf6-tracker-native'
    assert dll.is_file() and (data / 'web' / 'input.html').is_file(), 'Invalid package layout'
    with os.add_dll_directory(str(obs_bin)), tempfile.TemporaryDirectory(prefix='sf6-obs-check-') as config:
        obs = c.CDLL(str(obs_bin / 'obs.dll'))
        obs.obs_startup.argtypes = [c.c_char_p, c.c_char_p, c.c_void_p]
        obs.obs_startup.restype = c.c_bool
        obs.obs_shutdown.argtypes = []
        obs.obs_shutdown.restype = None
        obs.obs_open_module.argtypes = [c.POINTER(c.c_void_p), c.c_char_p, c.c_char_p]
        obs.obs_open_module.restype = c.c_int
        obs.obs_init_module.argtypes = [c.c_void_p]
        obs.obs_init_module.restype = c.c_bool
        obs.obs_enum_filter_types.argtypes = [c.c_size_t, c.POINTER(c.c_char_p)]
        obs.obs_enum_filter_types.restype = c.c_bool
        obs.obs_get_version_string.restype = c.c_char_p
        version = obs.obs_get_version_string().decode()
        assert obs.obs_startup(b'en-US', config.encode('utf-8'), None), 'OBS startup failed'
        try:
            module = c.c_void_p()
            code = obs.obs_open_module(c.byref(module), str(dll).encode('utf-8'), str(data).encode('utf-8'))
            assert code == 0, f'OBS {version}: module open failed ({code})'
            assert obs.obs_init_module(module), f'OBS {version}: module initialization failed'
            filters = []
            name = c.c_char_p()
            while obs.obs_enum_filter_types(len(filters), c.byref(name)):
                filters.append(name.value.decode())
            assert 'sf6_tracker_filter' in filters, f'Filter missing: {filters}'
            print(f'PASS: OBS {version} loaded the DLL and registered sf6_tracker_filter')
        finally:
            obs.obs_shutdown()


if __name__ == '__main__':
    main()
