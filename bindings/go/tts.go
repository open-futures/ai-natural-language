// Package tts provides Go bindings for the TTS SDK (TTF/ATTF v3.0).
//
// Build with: go build (requires cgo and libtts available on the linker path).
//
// Usage:
//
//	r, err := tts.OpenTtfReader("doc.ttf")
//	defer r.Close()
//	content, err := r.GetSection(1)
package tts

/*
#cgo LDFLAGS: -ltts
#include <tts/ffi.h>
#include <stdlib.h>
*/
import "C"
import (
	"errors"
	"unsafe"
)

// Version returns the SDK version string.
func Version() string {
	return C.GoString(C.tts_version())
}

// TtfReader wraps a TTF reader handle.
type TtfReader struct {
	handle unsafe.Pointer
}

// OpenTtfReader opens a TTF file.
func OpenTtfReader(path string) (*TtfReader, error) {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	var handle unsafe.Pointer
	rc := C.tts_ttf_open(cpath, &handle)
	if rc != 0 {
		return nil, errors.New(C.GoString(C.tts_last_error()))
	}
	return &TtfReader{handle: handle}, nil
}

// Close releases the reader.
func (r *TtfReader) Close() {
	if r.handle != nil {
		C.tts_ttf_close(r.handle)
		r.handle = nil
	}
}

// GetSection returns the content of the section with the given id.
func (r *TtfReader) GetSection(sectionID uint32) (string, error) {
	var need C.size_t
	C.tts_ttf_get_section(r.handle, C.uint32_t(sectionID), nil, 0, &need)
	if need == 0 {
		return "", nil
	}
	buf := make([]byte, int(need))
	var got C.size_t
	rc := C.tts_ttf_get_section(
		r.handle,
		C.uint32_t(sectionID),
		(*C.char)(unsafe.Pointer(&buf[0])),
		C.size_t(len(buf)),
		&got,
	)
	if rc != 0 {
		return "", errors.New(C.GoString(C.tts_last_error()))
	}
	return string(buf[:int(got)]), nil
}

// AttfReader wraps an ATTF reader handle.
type AttfReader struct {
	handle unsafe.Pointer
}

// OpenAttfReader opens an ATTF file.
func OpenAttfReader(path string) (*AttfReader, error) {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	var handle unsafe.Pointer
	rc := C.tts_attf_open(cpath, &handle)
	if rc != 0 {
		return nil, errors.New(C.GoString(C.tts_last_error()))
	}
	return &AttfReader{handle: handle}, nil
}

// Close releases the reader.
func (r *AttfReader) Close() {
	if r.handle != nil {
		C.tts_attf_close(r.handle)
		r.handle = nil
	}
}

// GetPage returns the content of the page at the given hex address (e.g. "0x00A1").
func (r *AttfReader) GetPage(hexAddress string) (string, error) {
	caddr := C.CString(hexAddress)
	defer C.free(unsafe.Pointer(caddr))
	var need C.size_t
	C.tts_attf_get_page(r.handle, caddr, nil, 0, &need)
	if need == 0 {
		return "", nil
	}
	buf := make([]byte, int(need))
	var got C.size_t
	rc := C.tts_attf_get_page(
		r.handle,
		caddr,
		(*C.char)(unsafe.Pointer(&buf[0])),
		C.size_t(len(buf)),
		&got,
	)
	if rc != 0 {
		return "", errors.New(C.GoString(C.tts_last_error()))
	}
	return string(buf[:int(got)]), nil
}
