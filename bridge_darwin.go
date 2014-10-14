package bgfx

/*
#include <bgfx.c99.h>
#include <bgfxplatform.c99.h>
*/
import "C"
import "unsafe"

// SetCocoaWindow sets the Cocoa window to use for context creation.
// Call before Init.
func SetCocoaWindow(wnd uintptr) {
	C.bgfx_osx_set_ns_window(unsafe.Pointer(wnd))
}
