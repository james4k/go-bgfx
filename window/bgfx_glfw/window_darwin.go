package bgfx_glfw

// #cgo darwin LDFLAGS: -lglfw3 -framework Cocoa -framework OpenGL
// #cgo darwin LDFLAGS: -framework IOKit -framework CoreVideo
// #define GLFW_EXPOSE_NATIVE_COCOA
// #define GLFW_EXPOSE_NATIVE_NSGL
// #include <GLFW/glfw3.h>
// #include <GLFW/glfw3native.h>
import "C"

import (
	"unsafe"

	glfw "github.com/go-gl/glfw3"
	"github.com/james4k/go-bgfx"
)

func SetWindow(wnd *glfw.Window) {
	cwnd := *(*unsafe.Pointer)(unsafe.Pointer(wnd))
	nswnd := C.glfwGetNSGLContext((*C.GLFWwindow)(cwnd))
	bgfx.SetCocoaWindow(uintptr(unsafe.Pointer(nswnd)))
}
