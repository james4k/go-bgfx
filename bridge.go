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
	"fmt"
	"io"
	"reflect"
	"unsafe"
)

type RendererType uint32

const (
	RendererTypeNull RendererType = iota
	RendererTypeDirect3D9
	RendererTypeDirect3D11
	_
	RendererTypeOpenGLES
	RendererTypeOpenGL
)

func Init() {
	C.bgfx_init(C.BGFX_RENDERER_TYPE_NULL, nil, nil)
}

func Shutdown() {
	C.bgfx_shutdown()
}

type ResetFlags uint32

const (
	ResetVSync ResetFlags = 0x80
)

// Reset resets the graphics settings.
func Reset(width, height int, flags ResetFlags) {
	C.bgfx_reset(C.uint32_t(width), C.uint32_t(height), C.uint32_t(flags))
}

// Frame advances to the next frame. Returns the current frame number.
func Frame() uint32 {
	return uint32(C.bgfx_frame())
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

type TextureFormat uint8

const (
	TextureFormatBC1 TextureFormat = iota
	TextureFormatBC2
	TextureFormatBC3
	TextureFormatBC4
	TextureFormatBC5
	TextureFormatBC6H
	TextureFormatBC7
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

	TextureFormatR1
	TextureFormatR8
	TextureFormatR16
	TextureFormatR16F
	TextureFormatR32
	TextureFormatR32F
	TextureFormatRG8
	TextureFormatRG16
	TextureFormatRG16F
	TextureFormatRG32
	TextureFormatRG32F
	TextureFormatBGRA8
	TextureFormatRGBA16
	TextureFormatRGBA16F
	TextureFormatRGBA32
	TextureFormatRGBA32F
	TextureFormatR5G6B5
	TextureFormatRGBA4
	TextureFormatRGB5A1
	TextureFormatRGB10A2
	TextureFormatR11G11B10F

	TextureFormatUnknownDepth

	TextureFormatD16
	TextureFormatD24
	TextureFormatD24S8
	TextureFormatD32
	TextureFormatD16F
	TextureFormatD24F
	TextureFormatD32F
	TextureFormatD0S8

	TextureFormatCount
)

type CapFlags uint64

const (
	CapsTextureCompareLEqual CapFlags = 0x0000000000000001
	CapsTextureCompareAll             = 0x0000000000000003
)

const (
	CapsTexture3D CapFlags = 0x0000000000000004 << iota
	CapsVertexAttribHalf
	CapsInstancing
	CapsRendererMultithreaded
	CapsFragmentDepth
	CapsBlendIndependent
	CapsCompute
	CapsFragmentOrdering
	CapsSwapChain
)

type Capabilities struct {
	RendererType     RendererType
	Supported        CapFlags
	MaxTextureSize   uint16
	MaxDrawCalls     uint16
	MaxFBAttachments uint8

	// 0=unsupported, 1=supported, 2=emulated
	Formats [TextureFormatCount]uint8
}

// Caps returns renderer capabilities. Note that the library must be
// initialized.
func Caps() Capabilities {
	caps := C.bgfx_get_caps()
	return Capabilities{
		RendererType:     RendererType(caps.rendererType),
		Supported:        CapFlags(caps.supported),
		MaxTextureSize:   uint16(caps.maxTextureSize),
		MaxDrawCalls:     uint16(caps.maxDrawCalls),
		MaxFBAttachments: uint8(caps.maxFBAttachments),
		Formats:          *(*[C.BGFX_TEXTURE_FORMAT_COUNT]uint8)(unsafe.Pointer(&caps.formats)),
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
	TextureUMirror TextureFlags = 1 << iota
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
	TextureRT TextureFlags = 0x00001000 + iota<<TextureRTMSAAShift
	TextureRTMSAAX2
	TextureRTMSAAX4
	TextureRTMSAAX8
	TextureRTMSAAX16
	TextureRTBufferOnly = 0x00008000
)

const (
	TextureCompareLess TextureFlags = 0x00010000 + iota<<TextureCompareShift
	TextureCompareLEqual
	TextureCompareEqual
	TextureCompareGEqual
	TextureCompareGreater
	TextureCompareNotEqual
	TextureCompareNever
	TextureCompareAlways
)

const (
	TextureUShift       TextureFlags = 0
	TextureVShift                    = 2
	TextureWShift                    = 4
	TextureMinShift                  = 6
	TextureMagShift                  = 8
	TextureMipShift                  = 10
	TextureRTMSAAShift               = 12
	TextureCompareShift              = 16

	TextureUMask       = 0x03
	TextureVMask       = 0x0c
	TextureWMask       = 0x30
	TextureMinMask     = 0xc0
	TextureMagMask     = 0x300
	TextureMipMask     = 0x400
	TextureRTMSAAMask  = 0x7000
	TextureRTMask      = 0xf000
	TextureCompareMask = 0xf0000

	TextureSamplerBitsMask = 0 |
		TextureUMask |
		TextureVMask |
		TextureWMask |
		TextureMinMask |
		TextureMagMask |
		TextureMipMask |
		TextureCompareMask
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

type Attrib uint8

const (
	AttribPosition Attrib = iota
	AttribNormal
	AttribTangent
	AttribBitangent
	AttribColor0
	AttribColor1
	AttribIndices
	AttribWeight
	AttribTexcoord0
	AttribTexcoord1
	AttribTexcoord2
	AttribTexcoord3
	AttribTexcoord4
	AttribTexcoord5
	AttribTexcoord6
	AttribTexcoord7
)

type AttribType uint8

const (
	AttribTypeUint8 AttribType = iota
	AttribTypeInt16
	AttribTypeHalf
	AttribTypeFloat
)

type VertexDecl struct {
	decl C.bgfx_vertex_decl_t
}

func (v *VertexDecl) Begin() {
	C.bgfx_vertex_decl_begin(&v.decl, C.BGFX_RENDERER_TYPE_NULL)
}

func (v *VertexDecl) Add(attrib Attrib, num uint8, typ AttribType, normalized bool, asint bool) {
	C.bgfx_vertex_decl_add(
		&v.decl,
		C.bgfx_attrib_t(attrib),
		C.uint8_t(num),
		C.bgfx_attrib_type_t(typ),
		C._Bool(normalized),
		C._Bool(asint),
	)
}

func (v *VertexDecl) SetOffset(attrib Attrib, offset uint) {
	v.decl.offset[attrib] = C.uint16_t(offset)
}

func (v *VertexDecl) Skip(num uint8) {

	C.bgfx_vertex_decl_skip(&v.decl, C.uint8_t(num))
}

func (v *VertexDecl) End() {
	C.bgfx_vertex_decl_end(&v.decl)
}

func (v *VertexDecl) Stride() int {
	return int(v.decl.stride)
}

type VertexBuffer struct {
	h C.bgfx_vertex_buffer_handle_t
}

func CreateVertexBuffer(slice interface{}, decl VertexDecl) VertexBuffer {
	val := reflect.ValueOf(slice)
	if val.Kind() != reflect.Slice {
		panic(errors.New("bgfx: expected slice"))
	}
	size := uintptr(val.Len()) * val.Type().Elem().Size()
	return VertexBuffer{
		h: C.bgfx_create_vertex_buffer(
			// to keep things simple for now, we'll just copy
			C.bgfx_copy(unsafe.Pointer(val.Pointer()), C.uint32_t(size)),
			&decl.decl,
		),
	}
}

func DestroyVertexBuffer(vb VertexBuffer) {
	C.bgfx_destroy_vertex_buffer(vb.h)
}

type IndexBuffer struct {
	h C.bgfx_index_buffer_handle_t
}

func CreateIndexBuffer(data []uint16) IndexBuffer {
	return IndexBuffer{
		h: C.bgfx_create_index_buffer(
			// to keep things simple for now, we'll just copy
			C.bgfx_copy(unsafe.Pointer(&data[0]), C.uint32_t(len(data)*2)),
		),
	}
}

func DestroyIndexBuffer(ib IndexBuffer) {
	C.bgfx_destroy_index_buffer(ib.h)
}

type TransientVertexBuffer struct {
	tvb C.bgfx_transient_vertex_buffer_t
}

func AllocTransientVertexBuffer(data interface{}, size int, decl VertexDecl) TransientVertexBuffer {
	val := reflect.ValueOf(data)
	if val.Kind() != reflect.Ptr || val.Elem().Kind() != reflect.Slice {
		panic(errors.New("bgfx: expected pointer to slice"))
	}
	var tvb TransientVertexBuffer
	C.bgfx_alloc_transient_vertex_buffer(
		&tvb.tvb,
		C.uint32_t(size),
		&decl.decl,
	)
	slice := (*reflect.SliceHeader)(unsafe.Pointer(val.Pointer()))
	slice.Data = uintptr(unsafe.Pointer(tvb.tvb.data))
	slice.Len = size
	slice.Cap = size
	return tvb
}

type TransientIndexBuffer struct {
	tib C.bgfx_transient_index_buffer_t
}

func AllocTransientIndexBuffer(buf *[]uint16, num int) TransientIndexBuffer {
	var tib TransientIndexBuffer
	C.bgfx_alloc_transient_index_buffer(
		&tib.tib,
		C.uint32_t(num),
	)
	slice := (*reflect.SliceHeader)(unsafe.Pointer(buf))
	slice.Data = uintptr(unsafe.Pointer(tib.tib.data))
	slice.Len = num
	slice.Cap = num
	return tib
}

func AllocTransientBuffers(verts interface{}, idxs *[]uint16, decl VertexDecl, numVerts, numIndices int) (tvb TransientVertexBuffer, tib TransientIndexBuffer, ok bool) {
	val := reflect.ValueOf(verts)
	if val.Kind() != reflect.Ptr || val.Elem().Kind() != reflect.Slice {
		panic(errors.New("bgfx: expected pointer to slice"))
	}
	ok = bool(C.bgfx_alloc_transient_buffers(
		&tvb.tvb,
		&decl.decl,
		C.uint16_t(numVerts),
		&tib.tib,
		C.uint16_t(numIndices),
	))
	if !ok {
		return
	}
	slice := (*reflect.SliceHeader)(unsafe.Pointer(val.Pointer()))
	slice.Data = uintptr(unsafe.Pointer(tvb.tvb.data))
	slice.Len = numVerts
	slice.Cap = numVerts
	slice = (*reflect.SliceHeader)(unsafe.Pointer(idxs))
	slice.Data = uintptr(unsafe.Pointer(tib.tib.data))
	slice.Len = numIndices
	slice.Cap = numIndices
	return
}

type InstanceDataBuffer struct {
	b    *C.bgfx_instance_data_buffer_t
	data []byte
	n    int
}

func AllocInstanceDataBuffer(num, stride int) InstanceDataBuffer {
	idb := C.bgfx_alloc_instance_data_buffer(
		C.uint32_t(num),
		C.uint16_t(stride),
	)
	slice := reflect.SliceHeader{
		Data: uintptr(unsafe.Pointer(idb.data)),
		Len:  int(idb.size),
		Cap:  int(idb.size),
	}
	return InstanceDataBuffer{
		b:    idb,
		data: *(*[]byte)(unsafe.Pointer(&slice)),
	}
}

func (b *InstanceDataBuffer) Write(p []byte) (n int, err error) {
	n = len(p)
	if b.n+n >= len(b.data) {
		n = len(b.data) - b.n
		err = io.EOF
	}
	for i := 0; i < n; i++ {
		b.data[b.n+i] = p[i]
	}
	b.n += n
	return
}

type Shader struct {
	h C.bgfx_shader_handle_t
}

func CreateShader(data []byte) Shader {
	return Shader{
		h: C.bgfx_create_shader(
			// to keep things simple for now, we'll just copy
			C.bgfx_copy(unsafe.Pointer(&data[0]), C.uint32_t(len(data))),
		),
	}
}

func DestroyShader(s Shader) {
	C.bgfx_destroy_shader(s.h)
}

type Program struct {
	h C.bgfx_program_handle_t
}

func CreateProgram(vsh, fsh Shader, destroyShaders bool) Program {
	return Program{
		h: C.bgfx_create_program(vsh.h, fsh.h, C._Bool(destroyShaders)),
	}
}

func DestroyProgram(p Program) {
	C.bgfx_destroy_program(p.h)
}

type ViewID int8

type ClearOptions uint8

const (
	ClearColor ClearOptions = 1 << iota
	ClearDepth
	ClearStencil
)

func SetViewRect(view ViewID, x, y, w, h int) {
	C.bgfx_set_view_rect(
		C.uint8_t(view),
		C.uint16_t(x),
		C.uint16_t(y),
		C.uint16_t(w),
		C.uint16_t(h),
	)
}

func SetViewTransform(viewID ViewID, view, proj [16]float32) {
	C.bgfx_set_view_transform(
		C.uint8_t(viewID),
		unsafe.Pointer(&view[0]),
		unsafe.Pointer(&proj[0]),
	)
}

func SetViewClear(view ViewID, clear ClearOptions, rgba uint32, depth float32, stencil uint8) {
	C.bgfx_set_view_clear(
		C.uint8_t(view),
		C.uint8_t(clear),
		C.uint32_t(rgba),
		C.float(depth),
		C.uint8_t(stencil),
	)
}

func SetViewFrameBuffer(view ViewID, fb FrameBuffer) {
	C.bgfx_set_view_frame_buffer(
		C.uint8_t(view),
		fb.h,
	)
}

type State uint64

const StateDefault State = 0 |
	StateRGBWrite |
	StateAlphaWrite |
	StateDepthWrite |
	StateDepthTestLess |
	StateCullCW |
	StateMSAA

const (
	StateRGBWrite State = 1 << iota
	StateAlphaWrite
	StateDepthWrite
)

const (
	StateDepthTestLess State = 0x10 << iota
	StateDepthTestLessEqual
)

const (
	StateCullCW State = 0x0000001000000000 << iota
	StateCullCCW
	StateCullMask = StateCullCW | StateCullCCW
)

const (
	StateMSAA State = 0x1000000000000000
)

const (
	StateBlendMask State = 0x000000000ffff000
)

type BlendValue uint32

const BlendShift BlendValue = 12
const (
	BlendZero BlendValue = 0x1000 + iota<<BlendShift
	BlendOne
	BlendSrcColor
	BlendInvSrcColor
	BlendSrcAlpha
	BlendInvSrcAlpha
	BlendDstAlpha
	BlendInvDstAlpha
	BlendDstColor
	BlendInvDstColor
	BlendSrcAlphaSat
	BlendFactor
	BlendInvFactor
)

func BlendFuncSeparate(srcRGB, dstRGB, srcA, dstA BlendValue) State {
	return (State(srcRGB) | (State(dstRGB) << 4)) |
		((State(srcA) | (State(dstA) << 4)) << 8)
}

func BlendFunc(src, dst BlendValue) State {
	return BlendFuncSeparate(src, dst, src, dst)
}

func StateBlendAlpha() State {
	return BlendFunc(BlendSrcAlpha, BlendInvSrcAlpha)
}

func SetTransform(mtx [16]float32) {
	C.bgfx_set_transform(unsafe.Pointer(&mtx[0]), 1)
}

func SetProgram(prog Program) {
	C.bgfx_set_program(prog.h)
}

func SetVertexBuffer(vb VertexBuffer) {
	C.bgfx_set_vertex_buffer(vb.h, 0, 0xffffffff)
}

func SetTransientVertexBuffer(tvb TransientVertexBuffer, start, num int) {
	C.bgfx_set_transient_vertex_buffer(&tvb.tvb, C.uint32_t(start), C.uint32_t(num))
}

func SetIndexBuffer(ib IndexBuffer) {
	C.bgfx_set_index_buffer(ib.h, 0, 0xffffffff)
}

func SetTransientIndexBuffer(tib TransientIndexBuffer, start, num int) {
	C.bgfx_set_transient_index_buffer(&tib.tib, C.uint32_t(start), C.uint32_t(num))
}

func SetInstanceDataBuffer(idb InstanceDataBuffer) {
	C.bgfx_set_instance_data_buffer(idb.b, 0xffff)
}

func SetUniform(u Uniform, ptr interface{}, num int) {
	val := reflect.ValueOf(ptr)
	C.bgfx_set_uniform(u.h, unsafe.Pointer(val.Pointer()), C.uint16_t(num))
}

func SetTexture(stage uint8, u Uniform, t Texture) {
	C.bgfx_set_texture(C.uint8_t(stage), u.h, t.h, C.UINT32_MAX)
}

func SetTextureFromFrameBuffer(stage uint8, u Uniform, fb FrameBuffer) {
	C.bgfx_set_texture_from_frame_buffer(C.uint8_t(stage), u.h, fb.h, 0, C.UINT32_MAX)
}

func SetState(state State) {
	C.bgfx_set_state(C.uint64_t(state), 0)
}

func Submit(view ViewID) {
	C.bgfx_submit(C.uint8_t(view), 0)
}

func Discard() {
	C.bgfx_discard()
}
