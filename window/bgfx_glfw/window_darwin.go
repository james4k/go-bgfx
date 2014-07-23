package bgfx_glfw

import (
	"unsafe"

	glfw "github.com/go-gl/glfw3"
	"github.com/james4k/go-bgfx"
)

func SetWindow(wnd *glfw.Window) {
	nswnd := wnd.GetNSGLContext()
	bgfx.SetCocoaWindow(uintptr(unsafe.Pointer(nswnd)))
}
