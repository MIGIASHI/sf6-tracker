"""Read only explicitly selected keyboard / mouse buttons; never record typed text."""
import os

if os.name == 'nt':
    import ctypes
    _state = ctypes.WinDLL('user32').GetAsyncKeyState
    _state.argtypes = [ctypes.c_int]
    _state.restype = ctypes.c_short


def read_keys(codes):
    if os.name != 'nt':
        return {'supported': False, 'pressed': []}
    selected = [c for c in codes[:32] if isinstance(c, int) and 0 < c < 255]
    return {'supported': True, 'pressed': [c for c in selected if _state(c) & 0x8000]}
