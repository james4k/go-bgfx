package bgfx

// #include <stdint.h>
// #include <bgfx.c99.h>
// #include <bgfxplatform.c99.h>
// #include "bridge.h"
import "C"
import "unsafe"

// SetWin32Window sets the Win32 window to use for context creation.
// Call before Init.
func SetWin32Window(wnd uintptr) {
	C.bgfx_win_set_hwnd(C.HWND(unsafe.Pointer(wnd)))
}
