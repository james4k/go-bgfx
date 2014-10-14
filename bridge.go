package bgfx

/*
#cgo CPPFLAGS: -I include -msse2
#cgo CXXFLAGS: -fno-rtti -fno-exceptions
#cgo darwin CPPFLAGS: -I include/compat/osx
#cgo darwin LDFLAGS: -framework Cocoa -framework OpenGL
#cgo linux LDFLAGS: -lGL
#include "bgfx.c99.h"
#include "bridge.h"
*/
import "C"
import (
	"errors"
	"reflect"
	"unsafe"
)

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

func VertexPack(input [4]float32, normalized bool, attrib Attrib, decl VertexDecl, slice interface{}, index int) {
	val := reflect.ValueOf(slice)
	if val.Kind() != reflect.Slice {
		panic(errors.New("bgfx: expected slice"))
	}
	C.bgfx_vertex_pack(
		(*C.float)(unsafe.Pointer(&input)),
		C._Bool(normalized),
		C.bgfx_attrib_t(attrib),
		&decl.decl,
		unsafe.Pointer(val.Pointer()),
		C.uint32_t(index),
	)
}

func VertexUnpack(attrib Attrib, decl VertexDecl, slice interface{}, index int) (output [4]float32) {
	val := reflect.ValueOf(slice)
	if val.Kind() != reflect.Slice {
		panic(errors.New("bgfx: expected slice"))
	}
	C.bgfx_vertex_unpack(
		(*C.float)(unsafe.Pointer(&output)),
		C.bgfx_attrib_t(attrib),
		&decl.decl,
		unsafe.Pointer(val.Pointer()),
		C.uint32_t(index),
	)
	return
}

func VertexConvert(destDecl, srcDecl VertexDecl, dest, src interface{}) {
	destVal := reflect.ValueOf(dest)
	srcVal := reflect.ValueOf(src)
	switch {
	case destVal.Kind() != reflect.Slice,
		srcVal.Kind() != reflect.Slice:
		panic(errors.New("bgfx: expected slice"))
	case destVal.Len() != srcVal.Len():
		panic(errors.New("bgfx: len(dest) != len(src)"))
	case destDecl.Stride() != int(destVal.Type().Elem().Size()):
		panic(errors.New("bgfx: stride != dest element size"))
	}
	destPtr := unsafe.Pointer(destVal.Pointer())
	srcPtr := unsafe.Pointer(srcVal.Pointer())
	C.bgfx_vertex_convert(&destDecl.decl, destPtr,
		&srcDecl.decl, srcPtr, C.uint32_t(srcVal.Len()))
}

type TextureFlags uint32

const (
	TextureNone TextureFlags = 1 << iota
	TextureUMirror
	TextureUClamp
	TextureVMirror
	TextureVClamp
	TextureWMirror
	TextureWClamp
	TextureMinPoint
	TextureMinAnisotropic
	TextureMagPoint
	TextureMagAnisotropic
	TextureMipPoint
)

const (
	TextureRT TextureFlags = 0x00001000 + iota
	TextureRTMSAAX2
	TextureRTMSAAX4
	TextureRTMSAAX8
	TextureRTMSAAX16
	TextureRTBufferOnly = 0x00008000
)

const (
	TextureCompareLess TextureFlags = 0x00010000 + iota
	TextureCompareLEqual
	TextureCompareEqual
	TextureCompareGEqual
	TextureCompareGreater
	TextureCompareNotEqual
	TextureCompareNever
	TextureCompareAlways
)

type TextureFormat uint8

const (
	TextureFormatBC1 TextureFormat = iota
	TextureFormatBC2
	TextureFormatBC3
	TextureFormatBC4
	TextureFormatBC5
	TextureFormatETC1
	TextureFormatETC2
	TextureFormatETC2A
	TextureFormatETC2A1
	TextureFormatPTC12
	TextureFormatPTC14
	TextureFormatPTC12A
	TextureFormatPTC14A
	TextureFormatPTC22
	TextureFormatPTC24

	TextureFormatUnknown

	TextureFormatR8
	TextureFormatR16
	TextureFormatR16F
	TextureFormatBGRA8
	TextureFormatRGBA16
	TextureFormatRGBA16F
	TextureFormatR5G6B5
	TextureFormatRGBA4
	TextureFormatRGB5A1
	TextureFormatRGB10A2

	TextureFormatUnknownDepth

	TextureFormatD16
	TextureFormatD24
	TextureFormatD24S8
	TextureFormatD32
	TextureFormatD16F
	TextureFormatD24F
	TextureFormatD32F
	TextureFormatD0S8
)

type Texture struct {
	h C.bgfx_texture_handle_t
}

type TextureInfo struct {
	Format       TextureFormat
	StorageSize  uint32
	Width        uint16
	Height       uint16
	Depth        uint16
	NumMips      uint8
	BitsPerPixel uint8
}

func newTextureInfo(ti C.bgfx_texture_info_t) TextureInfo {
	return TextureInfo{
		Format:       TextureFormat(ti.format),
		StorageSize:  uint32(ti.storageSize),
		Width:        uint16(ti.width),
		Height:       uint16(ti.height),
		Depth:        uint16(ti.depth),
		NumMips:      uint8(ti.numMips),
		BitsPerPixel: uint8(ti.bitsPerPixel),
	}
}

func CreateTexture(data []byte, flags TextureFlags, skip uint8) (Texture, TextureInfo) {
	var ti C.bgfx_texture_info_t
	h := C.bgfx_create_texture(
		C.bgfx_copy(unsafe.Pointer(&data[0]), C.uint32_t(len(data))),
		C.uint32_t(flags),
		C.uint8_t(skip),
		&ti,
	)
	return Texture{h: h}, newTextureInfo(ti)
}

func CreateTexture2D(width, height, numMips int, format TextureFormat, flags TextureFlags, data []byte) Texture {
	var mem *C.bgfx_memory_t
	if data != nil {
		mem = C.bgfx_copy(unsafe.Pointer(&data[0]), C.uint32_t(len(data)))
	}
	h := C.bgfx_create_texture_2d(
		C.uint16_t(width),
		C.uint16_t(height),
		C.uint8_t(numMips),
		C.bgfx_texture_format_t(format),
		C.uint32_t(flags),
		mem,
	)
	return Texture{h: h}
}

func CreateTexture3D(width, height, depth, numMips int, format TextureFormat, flags TextureFlags, data []byte) Texture {
	var mem *C.bgfx_memory_t
	if data != nil {
		mem = C.bgfx_copy(unsafe.Pointer(&data[0]), C.uint32_t(len(data)))
	}
	h := C.bgfx_create_texture_3d(
		C.uint16_t(width),
		C.uint16_t(height),
		C.uint16_t(depth),
		C.uint8_t(numMips),
		C.bgfx_texture_format_t(format),
		C.uint32_t(flags),
		mem,
	)
	return Texture{h: h}
}

func CreateTextureCube(size, numMips int, format TextureFormat, flags TextureFlags, data []byte) Texture {
	var mem *C.bgfx_memory_t
	if data != nil {
		mem = C.bgfx_copy(unsafe.Pointer(&data[0]), C.uint32_t(len(data)))
	}
	h := C.bgfx_create_texture_cube(
		C.uint16_t(size),
		C.uint8_t(numMips),
		C.bgfx_texture_format_t(format),
		C.uint32_t(flags),
		mem,
	)
	return Texture{h: h}
}

func DestroyTexture(t Texture) {
	C.bgfx_destroy_texture(t.h)
}

func CalcTextureSize(width, height, depth, numMips int, format TextureFormat) TextureInfo {
	var ti C.bgfx_texture_info_t
	C.bgfx_calc_texture_size(
		&ti,
		C.uint16_t(width),
		C.uint16_t(height),
		C.uint16_t(depth),
		C.uint8_t(numMips),
		C.bgfx_texture_format_t(format),
	)
	return newTextureInfo(ti)
}

func UpdateTextureCube(t Texture, side, mip, x, y, width, height int, data []byte, pitch int) {
	if pitch == 0 {
		pitch = 0xffff
	}
	C.bgfx_update_texture_cube(
		t.h,
		C.uint8_t(side),
		C.uint8_t(mip),
		C.uint16_t(x),
		C.uint16_t(y),
		C.uint16_t(width),
		C.uint16_t(height),
		// to keep things simple and safe, just copy for now
		C.bgfx_copy(
			unsafe.Pointer(&data[0]),
			C.uint32_t(len(data)),
		),
		C.uint16_t(pitch),
	)
}

type FrameBuffer struct {
	h C.bgfx_frame_buffer_handle_t
}

func CreateFrameBuffer(width, height int, format TextureFormat, flags TextureFlags) FrameBuffer {
	h := C.bgfx_create_frame_buffer(
		C.uint16_t(width),
		C.uint16_t(height),
		C.bgfx_texture_format_t(format),
		C.uint32_t(flags),
	)
	return FrameBuffer{h: h}
}

func CreateFrameBufferFromTextures(textures []Texture, destroyTextures bool) FrameBuffer {
	h := C.bgfx_create_frame_buffer_from_handles(
		C.uint8_t(len(textures)),
		//(*C.bgfx_texture_handle_t)(unsafe.Pointer(&textures[0])),
		&textures[0].h,
		C._Bool(destroyTextures),
	)
	return FrameBuffer{h: h}
}

func DestroyFrameBuffer(fb FrameBuffer) {
	C.bgfx_destroy_frame_buffer(fb.h)
}
