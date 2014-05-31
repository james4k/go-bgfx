package bgfx

/*
#cgo CPPFLAGS: -I include
#cgo darwin CPPFLAGS: -I include/compat/osx
#cgo darwin LDFLAGS: -framework Cocoa -framework OpenGL -lGLEW
#cgo linux LDFLAGS: -lGLEW -lGL
#cgo windows LDFLAGS: -lglew32 -lopengl32
#include "bgfx.c99.h"
#include "bridge.h"
*/
import "C"
import (
	"fmt"
	"unsafe"
)

func Init() {
	C.bgfx_init(C.BGFX_RENDERER_TYPE_NULL, nil, nil)
}

func Shutdown() {
	C.bgfx_shutdown()
}

// Reset resets the graphics settings.
// TODO: flags/options
func Reset(width, height int) {
	C.bgfx_reset(C.uint32_t(width), C.uint32_t(height), C.BGFX_RESET_VSYNC)
}

// Frame advances to the next frame.
func Frame() {
	C.bgfx_frame()
}

func Submit(view uint8) {
	C.bgfx_submit(C.uint8_t(view), 0)
}

type DebugOptions uint32

const (
	DebugWireframe DebugOptions = 1 << iota
	DebugIFH
	DebugStats
	DebugText
)

func SetDebug(f DebugOptions) {
	C.bgfx_set_debug(C.uint32_t(f))
}

func DebugTextClear() {
	C.bgfx_dbg_text_clear(0, false)
}

func DebugTextPrintf(x, y int, attr uint8, format string, args ...interface{}) {
	text := []byte(fmt.Sprintf(format+"\x00", args...))
	C.bgfx_dbg_text_print(
		C.uint32_t(x),
		C.uint32_t(y),
		C.uint8_t(attr),
		(*C.char)(unsafe.Pointer(&text[0])),
	)
}

type ViewID int8

func SetViewRect(view ViewID, x, y, w, h int) {
	C.bgfx_set_view_rect(
		C.uint8_t(view),
		C.uint16_t(x),
		C.uint16_t(y),
		C.uint16_t(w),
		C.uint16_t(h),
	)
}

type ClearOptions uint8

const (
	ClearColor ClearOptions = 1 << iota
	ClearDepth
	ClearStencil
)

func SetViewClear(view ViewID, clear ClearOptions, rgba uint32, depth float32, stencil uint8) {
	C.bgfx_set_view_clear(
		C.uint8_t(view),
		C.uint8_t(clear),
		C.uint32_t(rgba),
		C.float(depth),
		C.uint8_t(stencil),
	)
}
