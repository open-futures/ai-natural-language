//! tts — Rust binding for the TTS SDK (v3.0)
//!
//! Thin idiomatic wrapper over the C ABI in include/tts/ffi.h.
//! Build with: cargo build (assumes libtts is on the linker path).

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::path::Path;
use std::ptr;

extern "C" {
    fn tts_version() -> *const c_char;
    fn tts_ttf_open(path: *const c_char, out: *mut *mut c_void) -> c_int;
    fn tts_ttf_close(reader: *mut c_void) -> c_int;
    fn tts_ttf_section_count(reader: *mut c_void, out: *mut usize) -> c_int;
    fn tts_ttf_section_id_at(reader: *mut c_void, idx: usize, out: *mut u32) -> c_int;
    fn tts_ttf_get_section(
        reader: *mut c_void,
        section_id: u32,
        buf: *mut c_char,
        cap: usize,
        out_len: *mut usize,
    ) -> c_int;
    fn tts_ttf_validate_all(reader: *mut c_void, bad: *mut u32) -> c_int;

    fn tts_attf_open(path: *const c_char, out: *mut *mut c_void) -> c_int;
    fn tts_attf_close(reader: *mut c_void) -> c_int;
    fn tts_attf_directory_count(reader: *mut c_void, out: *mut usize) -> c_int;
    fn tts_attf_get_page(
        reader: *mut c_void,
        addr: *const c_char,
        buf: *mut c_char,
        cap: usize,
        out_len: *mut usize,
    ) -> c_int;
    fn tts_attf_get_page_by_id(
        reader: *mut c_void,
        page_id: u32,
        buf: *mut c_char,
        cap: usize,
        out_len: *mut usize,
    ) -> c_int;

    fn tts_last_error() -> *const c_char;
}

#[derive(Debug)]
pub struct Error(pub i32, pub String);

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[{}] {}", self.0, self.1)
    }
}
impl std::error::Error for Error {}

pub fn version() -> String {
    unsafe {
        let s = CStr::from_ptr(tts_version());
        s.to_string_lossy().into_owned()
    }
}

pub struct TtfReader {
    handle: *mut c_void,
}

impl TtfReader {
    pub fn open<P: AsRef<Path>>(path: P) -> Result<Self, Error> {
        let c_path = CString::new(path.as_ref().to_string_lossy().as_bytes())
            .map_err(|_| Error(15, "path contains null".into()))?;
        let mut handle = ptr::null_mut::<c_void>();
        let rc = unsafe { tts_ttf_open(c_path.as_ptr(), &mut handle) };
        if rc != 0 {
            let msg = unsafe { CStr::from_ptr(tts_last_error()) }
                .to_string_lossy().into_owned();
            return Err(Error(rc, msg));
        }
        Ok(Self { handle })
    }

    pub fn section_count(&self) -> Result<usize, Error> {
        let mut n = 0usize;
        let rc = unsafe { tts_ttf_section_count(self.handle, &mut n) };
        if rc != 0 { return Err(Error(rc, last_err())); }
        Ok(n)
    }

    pub fn section_ids(&self) -> Result<Vec<u32>, Error> {
        let n = self.section_count()?;
        let mut out = Vec::with_capacity(n);
        for i in 0..n {
            let mut id = 0u32;
            let rc = unsafe { tts_ttf_section_id_at(self.handle, i, &mut id) };
            if rc != 0 { return Err(Error(rc, last_err())); }
            out.push(id);
        }
        Ok(out)
    }

    pub fn get_section(&self, section_id: u32) -> Result<String, Error> {
        let mut need = 0usize;
        unsafe { tts_ttf_get_section(self.handle, section_id, ptr::null_mut(), 0, &mut need); }
        if need == 0 { return Ok(String::new()); }
        let mut buf = vec![0u8; need];
        let mut got = 0usize;
        let rc = unsafe {
            tts_ttf_get_section(
                self.handle,
                section_id,
                buf.as_mut_ptr() as *mut c_char,
                buf.len(),
                &mut got,
            )
        };
        if rc != 0 { return Err(Error(rc, last_err())); }
        buf.truncate(got);
        String::from_utf8(buf).map_err(|e| Error(15, e.to_string()))
    }

    pub fn validate_all(&self) -> Result<(), Error> {
        let mut bad = 0u32;
        let rc = unsafe { tts_ttf_validate_all(self.handle, &mut bad) };
        if rc != 0 { return Err(Error(rc, last_err())); }
        Ok(())
    }
}

impl Drop for TtfReader {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe { tts_ttf_close(self.handle); }
        }
    }
}

pub struct AttfReader {
    handle: *mut c_void,
}

impl AttfReader {
    pub fn open<P: AsRef<Path>>(path: P) -> Result<Self, Error> {
        let c_path = CString::new(path.as_ref().to_string_lossy().as_bytes())
            .map_err(|_| Error(15, "path contains null".into()))?;
        let mut handle = ptr::null_mut::<c_void>();
        let rc = unsafe { tts_attf_open(c_path.as_ptr(), &mut handle) };
        if rc != 0 {
            let msg = unsafe { CStr::from_ptr(tts_last_error()) }
                .to_string_lossy().into_owned();
            return Err(Error(rc, msg));
        }
        Ok(Self { handle })
    }

    pub fn get_page(&self, hex_address: &str) -> Result<String, Error> {
        let c_addr = CString::new(hex_address)
            .map_err(|_| Error(15, "address contains null".into()))?;
        let mut need = 0usize;
        unsafe { tts_attf_get_page(self.handle, c_addr.as_ptr(), ptr::null_mut(), 0, &mut need); }
        if need == 0 { return Ok(String::new()); }
        let mut buf = vec![0u8; need];
        let mut got = 0usize;
        let rc = unsafe {
            tts_attf_get_page(
                self.handle,
                c_addr.as_ptr(),
                buf.as_mut_ptr() as *mut c_char,
                buf.len(),
                &mut got,
            )
        };
        if rc != 0 { return Err(Error(rc, last_err())); }
        buf.truncate(got);
        String::from_utf8(buf).map_err(|e| Error(15, e.to_string()))
    }

    pub fn get_page_by_id(&self, page_id: u32) -> Result<String, Error> {
        let mut need = 0usize;
        unsafe { tts_attf_get_page_by_id(self.handle, page_id, ptr::null_mut(), 0, &mut need); }
        if need == 0 { return Ok(String::new()); }
        let mut buf = vec![0u8; need];
        let mut got = 0usize;
        let rc = unsafe {
            tts_attf_get_page_by_id(
                self.handle,
                page_id,
                buf.as_mut_ptr() as *mut c_char,
                buf.len(),
                &mut got,
            )
        };
        if rc != 0 { return Err(Error(rc, last_err())); }
        buf.truncate(got);
        String::from_utf8(buf).map_err(|e| Error(15, e.to_string()))
    }
}

impl Drop for AttfReader {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe { tts_attf_close(self.handle); }
        }
    }
}

fn last_err() -> String {
    unsafe { CStr::from_ptr(tts_last_error()).to_string_lossy().into_owned() }
}
