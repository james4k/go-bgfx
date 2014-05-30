package bgfx

// #include "bridge.h"
// #include "bridge_darwin.h"
import "C"
import "unsafe"

// SetCocoaWindow sets the Cocoa window to use for context creation.
// Call before Init.
func SetCocoaWindow(wnd uintptr) {
	C.bgfx_osxSetNSWindow(unsafe.Pointer(wnd))
}
