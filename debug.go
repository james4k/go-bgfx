package bgfx

//#include "bgfx.c99.h"
//#include "bridge.h"
import "C"

import (
	"fmt"
	"unsafe"
)

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
