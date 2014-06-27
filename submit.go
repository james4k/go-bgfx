package bgfx

//#include "bgfx.c99.h"
//#include "bgfxdefines.h"
import "C"
import (
	"reflect"
	"unsafe"
)

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

func SetState(state State) {
	C.bgfx_set_state(C.uint64_t(state), 0)
}

func Submit(view ViewID) {
	C.bgfx_submit(C.uint8_t(view), 0)
}

func Discard() {
	C.bgfx_discard()
}
