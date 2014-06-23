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
import "unsafe"

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

type RendererType uint32

const (
	RendererTypeNull RendererType = iota
	RendererTypeDirect3D9
	RendererTypeDirect3D11
	RendererTypeOpenGLES
	RendererTypeOpenGL
)

type CapFlags uint64

const (
	CapsTextureFormatBC1 CapFlags = 1 << iota
	CapsTextureFormatBC2
	CapsTextureFormatBC3
	CapsTextureFormatBC4
	CapsTextureFormatBC5
	CapsTextureFormatETC1
	CapsTextureFormatETC2
	CapsTextureFormatETC2A
	CapsTextureFormatETC2A1
	CapsTextureFormatPTC12
	CapsTextureFormatPTC14
	CapsTextureFormatPTC14A
	CapsTextureFormatPTC12A
	CapsTextureFormatPTC22
	CapsTextureFormatPTC24
	CapsTextureFormatD16
	CapsTextureFormatD24
	CapsTextureFormatD24S8
	CapsTextureFormatD32
	CapsTextureFormatD16F
	CapsTextureFormatD24F
	CapsTextureFormatD32F
	CapsTextureFormatD0S8
	CapsTextureCompareLEqual = 0x0000000001000000
	CapsTextureCompareAll    = 0x0000000003000000
)

const (
	CapsTexture3D CapFlags = 0x0000000004000000 << iota
	CapsVertexAttribHalf
	CapsInstancing
	CapsRendererMultithreaded
	CapsFragmentDepth
	CapsBlendIndependent
)

type Capabilities struct {
	RendererType     RendererType
	Supported        CapFlags
	Emulated         CapFlags
	MaxTextureSize   uint16
	MaxDrawCalls     uint16
	MaxFBAttachments uint8
}

// Caps returns renderer capabilities. Note that the library must be
// initialized.
func Caps() Capabilities {
	caps := C.bgfx_get_caps()
	return Capabilities{
		RendererType:     RendererType(caps.rendererType),
		Supported:        CapFlags(caps.supported),
		Emulated:         CapFlags(caps.emulated),
		MaxTextureSize:   uint16(caps.maxTextureSize),
		MaxDrawCalls:     uint16(caps.maxDrawCalls),
		MaxFBAttachments: uint8(caps.maxFBAttachments),
	}
}

type UniformType uint8

const (
	Uniform1i UniformType = iota
	Uniform1f
	_
	Uniform1iv
	Uniform1fv
	Uniform2fv
	Uniform3fv
	Uniform4fv
	Uniform3x3fv
	Uniform4x4fv
)

type Uniform struct {
	h C.bgfx_uniform_handle_t
}

func CreateUniform(name string, typ UniformType, num int) Uniform {
	cname := C.CString(name)
	defer C.free(unsafe.Pointer(cname))
	h := C.bgfx_create_uniform(cname, C.bgfx_uniform_type_t(typ), C.uint16_t(num))
	return Uniform{h: h}
}
func DestroyUniform(u Uniform) {
	C.bgfx_destroy_uniform(u.h)
}
