package bgfx

// #include "bgfx.c99.h"
import "C"
import (
	"errors"
	"reflect"
	"unsafe"
)

type Attrib uint8

const (
	AttribPosition Attrib = iota
	AttribNormal
	AttribTangent
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

func (v *VertexDecl) Skip(num uint8) {
	C.bgfx_vertex_decl_skip(&v.decl, C.uint8_t(num))
}

func (v *VertexDecl) End() {
	C.bgfx_vertex_decl_end(&v.decl)
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
