package bgfx_glfw

import (
	"unsafe"

	glfw "github.com/go-gl/glfw3"
	"github.com/james4k/go-bgfx"
)

func SetWindow(wnd *glfw.Window) {
	hwnd := wnd.GetWin32Window()
	bgfx.SetWin32Window(uintptr(unsafe.Pointer(hwnd)))
}
