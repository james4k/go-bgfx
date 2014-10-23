package bgfx

// #include "bgfx.c99.h"
import "C"
import "unsafe"

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
