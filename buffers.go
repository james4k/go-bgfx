package bgfx

// #include "bgfx.c99.h"
import "C"
import (
	"errors"
	"io"
	"reflect"
	"unsafe"
)

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
