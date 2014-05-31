package bgfx

// #include <stdint.h>
// #include "bridge.h"
// #include "bridge_darwin.h"
import "C"
import "unsafe"

// SetCocoaWindow sets the Cocoa window to use for context creation.
// Call before Init.
func SetCocoaWindow(wnd uintptr) {
	C.bgfx_osx_set_nswindow(unsafe.Pointer(wnd))
}
