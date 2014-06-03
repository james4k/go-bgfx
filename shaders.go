package bgfx

//#include "bgfx.c99.h"
import "C"
import "unsafe"

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
