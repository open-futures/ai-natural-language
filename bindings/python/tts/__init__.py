"""
tts — Python binding for the TTS SDK (v3.0)

Wraps the C ABI in include/tts/ffi.h. Requires the shared library
`libtts.so` (Linux), `libtts.dylib` (macOS), or `tts.dll` (Windows).

Usage:
    import tts
    with tts.TtfReader("doc.ttf") as r:
        section_1 = r.get_section(1)
    with tts.AttfReader("memory.attf") as r:
        page = r.get_page("0x00A1")
"""
from __future__ import annotations

import ctypes
import os
import sys
from ctypes import (
    c_char_p, c_int, c_uint32, c_size_t, POINTER, byref, c_void_p, create_string_buffer,
)
from pathlib import Path
from typing import Optional, Iterator


def _find_library() -> ctypes.CDLL:
    here = Path(__file__).resolve().parent
    candidates = [
        here.parent / "lib" / "libtts.so",
        here.parent / "lib" / "libtts.dylib",
        here.parent / "lib" / "tts.dll",
        Path("libtts.so"),
        Path("libtts.dylib"),
        Path("tts.dll"),
    ]
    # Also look alongside this file
    candidates.append(here / "libtts.so")
    candidates.append(here / "libtts.dylib")
    candidates.append(here / "tts.dll")
    for p in candidates:
        if p.exists():
            return ctypes.CDLL(str(p))
    # Try system loader
    try:
        return ctypes.CDLL("tts")
    except OSError as e:
        raise RuntimeError(
            "Cannot find libtts. Set TTS_LIB_PATH or place libtts.so next to "
            "tts/__init__.py."
        ) from e


_LIB = _find_library()

# -----------------------------------------------------------------------------
#  Function signatures
# -----------------------------------------------------------------------------
_LIB.tts_version.restype = c_char_p
_LIB.tts_version_packed.restype = c_uint32
_LIB.tts_last_error.restype = c_char_p

_LIB.tts_ttf_open.argtypes = [c_char_p, POINTER(c_void_p)]
_LIB.tts_ttf_open.restype  = c_int
_LIB.tts_ttf_close.argtypes = [c_void_p]
_LIB.tts_ttf_close.restype  = c_int
_LIB.tts_ttf_section_count.argtypes = [c_void_p, POINTER(c_size_t)]
_LIB.tts_ttf_section_count.restype  = c_int
_LIB.tts_ttf_section_id_at.argtypes = [c_void_p, c_size_t, POINTER(c_uint32)]
_LIB.tts_ttf_section_id_at.restype  = c_int
_LIB.tts_ttf_get_section.argtypes = [c_void_p, c_uint32, c_char_p, c_size_t, POINTER(c_size_t)]
_LIB.tts_ttf_get_section.restype  = c_int
_LIB.tts_ttf_validate_all.argtypes = [c_void_p, POINTER(c_uint32)]
_LIB.tts_ttf_validate_all.restype  = c_int

_LIB.tts_attf_open.argtypes = [c_char_p, POINTER(c_void_p)]
_LIB.tts_attf_open.restype  = c_int
_LIB.tts_attf_close.argtypes = [c_void_p]
_LIB.tts_attf_close.restype  = c_int
_LIB.tts_attf_directory_count.argtypes = [c_void_p, POINTER(c_size_t)]
_LIB.tts_attf_directory_count.restype  = c_int
_LIB.tts_attf_get_page.argtypes = [c_void_p, c_char_p, c_char_p, c_size_t, POINTER(c_size_t)]
_LIB.tts_attf_get_page.restype  = c_int
_LIB.tts_attf_get_page_by_id.argtypes = [c_void_p, c_uint32, c_char_p, c_size_t, POINTER(c_size_t)]
_LIB.tts_attf_get_page_by_id.restype  = c_int

_LIB.tts_ttf_error_str.argtypes = [c_int]
_LIB.tts_ttf_error_str.restype  = c_char_p
_LIB.tts_attf_error_str.argtypes = [c_int]
_LIB.tts_attf_error_str.restype  = c_char_p


class TtsError(Exception):
    """Base class for TTS SDK errors."""
    def __init__(self, code: int, message: str):
        self.code = code
        self.message = message
        super().__init__(f"[{code}] {message}")


class TtfOpenError(TtsError): pass
class TtfSectionError(TtsError): pass
class AttfOpenError(TtsError): pass
class AttfPageError(TtsError): pass


# -----------------------------------------------------------------------------
#  TtfReader
# -----------------------------------------------------------------------------
class TtfReader:
    def __init__(self, path: str | os.PathLike):
        path = os.fspath(path).encode("utf-8")
        self._handle = c_void_p()
        rc = _LIB.tts_ttf_open(path, byref(self._handle))
        if rc != 0:
            err = _LIB.tts_last_error().decode("utf-8", "replace")
            raise TtfOpenError(rc, err)

    def __enter__(self): return self
    def __exit__(self, *exc):
        self.close()

    def close(self):
        if self._handle:
            _LIB.tts_ttf_close(self._handle)
            self._handle = None

    def section_count(self) -> int:
        n = c_size_t(0)
        rc = _LIB.tts_ttf_section_count(self._handle, byref(n))
        if rc != 0:
            raise TtfSectionError(rc, _LIB.tts_last_error().decode("utf-8","replace"))
        return n.value

    def section_id_at(self, index: int) -> int:
        out = c_uint32(0)
        rc = _LIB.tts_ttf_section_id_at(self._handle, index, byref(out))
        if rc != 0:
            raise TtfSectionError(rc, _LIB.tts_last_error().decode("utf-8","replace"))
        return out.value

    def section_ids(self) -> list[int]:
        return [self.section_id_at(i) for i in range(self.section_count())]

    def get_section(self, section_id: int) -> str:
        need = c_size_t(0)
        # Sizing call
        _LIB.tts_ttf_get_section(self._handle, section_id, None, 0, byref(need))
        if need.value == 0:
            return ""
        buf = create_string_buffer(need.value)
        got = c_size_t(0)
        rc = _LIB.tts_ttf_get_section(self._handle, section_id, buf, len(buf), byref(got))
        if rc == -2:
            # Should not happen — retry with bigger buffer
            buf = create_string_buffer(got.value)
            rc = _LIB.tts_ttf_get_section(self._handle, section_id, buf, len(buf), byref(got))
        if rc != 0:
            raise TtfSectionError(rc, _LIB.tts_last_error().decode("utf-8","replace"))
        return buf.raw[:got.value].decode("utf-8", "replace")

    def validate_all(self) -> bool:
        bad = c_uint32(0)
        rc = _LIB.tts_ttf_validate_all(self._handle, byref(bad))
        if rc != 0:
            raise TtfSectionError(rc, _LIB.tts_last_error().decode("utf-8","replace"))
        return True


# -----------------------------------------------------------------------------
#  AttfReader
# -----------------------------------------------------------------------------
class AttfReader:
    def __init__(self, path: str | os.PathLike):
        path = os.fspath(path).encode("utf-8")
        self._handle = c_void_p()
        rc = _LIB.tts_attf_open(path, byref(self._handle))
        if rc != 0:
            err = _LIB.tts_last_error().decode("utf-8", "replace")
            raise AttfOpenError(rc, err)

    def __enter__(self): return self
    def __exit__(self, *exc):
        self.close()

    def close(self):
        if self._handle:
            _LIB.tts_attf_close(self._handle)
            self._handle = None

    def directory_count(self) -> int:
        n = c_size_t(0)
        rc = _LIB.tts_attf_directory_count(self._handle, byref(n))
        if rc != 0:
            raise AttfPageError(rc, _LIB.tts_last_error().decode("utf-8","replace"))
        return n.value

    def get_page(self, hex_address: str) -> str:
        addr = hex_address.encode("utf-8")
        need = c_size_t(0)
        _LIB.tts_attf_get_page(self._handle, addr, None, 0, byref(need))
        if need.value == 0:
            return ""
        buf = create_string_buffer(need.value)
        got = c_size_t(0)
        rc = _LIB.tts_attf_get_page(self._handle, addr, buf, len(buf), byref(got))
        if rc == -2:
            buf = create_string_buffer(got.value)
            rc = _LIB.tts_attf_get_page(self._handle, addr, buf, len(buf), byref(got))
        if rc != 0:
            raise AttfPageError(rc, _LIB.tts_last_error().decode("utf-8","replace"))
        return buf.raw[:got.value].decode("utf-8", "replace")

    def get_page_by_id(self, page_id: int) -> str:
        need = c_size_t(0)
        _LIB.tts_attf_get_page_by_id(self._handle, page_id, None, 0, byref(need))
        if need.value == 0:
            return ""
        buf = create_string_buffer(need.value)
        got = c_size_t(0)
        rc = _LIB.tts_attf_get_page_by_id(self._handle, page_id, buf, len(buf), byref(got))
        if rc == -2:
            buf = create_string_buffer(got.value)
            rc = _LIB.tts_attf_get_page_by_id(self._handle, page_id, buf, len(buf), byref(got))
        if rc != 0:
            raise AttfPageError(rc, _LIB.tts_last_error().decode("utf-8","replace"))
        return buf.raw[:got.value].decode("utf-8", "replace")


def version() -> str:
    return _LIB.tts_version().decode("utf-8")
