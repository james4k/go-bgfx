package bgfx

/*
#cgo CPPFLAGS: -I include
#cgo darwin CPPFLAGS: -I include/compat/osx
#cgo darwin LDFLAGS: -framework Cocoa -framework OpenGL
#cgo linux LDFLAGS: -lGL
#cgo windows LDFLAGS: -lopengl32
#include "bgfx.c99.h"
#include "bridge.h"
*/
import "C"

func Init() {
	C.bgfx_init(C.BGFX_RENDERER_TYPE_NULL, nil, nil)
}

func Shutdown() {
	C.bgfx_shutdown()
}

type ResetFlags uint32

const (
	ResetVSync = 0x80
)

// Reset resets the graphics settings.
func Reset(width, height int, flags ResetFlags) {
	C.bgfx_reset(C.uint32_t(width), C.uint32_t(height), C.uint32_t(flags))
}

// Frame advances to the next frame. Returns the current frame number.
func Frame() uint32 {
	return uint32(C.bgfx_frame())
}
