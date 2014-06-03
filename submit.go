package bgfx

//#include "bgfx.c99.h"
//#include "bgfxdefines.h"
import "C"
import "unsafe"

func SetTransform(mtx [16]float32) {
	C.bgfx_set_transform(unsafe.Pointer(&mtx[0]), 1)
}

func SetProgram(prog Program) {
	C.bgfx_set_program(prog.h)
}

func SetVertexBuffer(vb VertexBuffer) {
	C.bgfx_set_vertex_buffer(vb.h, 0, 0xffffffff)
}

func SetIndexBuffer(ib IndexBuffer) {
	C.bgfx_set_index_buffer(ib.h, 0, 0xffffffff)
}

func SetState(state State) {
	C.bgfx_set_state(C.uint64_t(state), 0)
}

func Submit(view uint8) {
	C.bgfx_submit(C.uint8_t(view), 0)
}
