#include <bgfx.h>
#include <bgfxplatform.h>
#include "bridge.h"

BGFX_API void bgfx_init() {
	bgfx::init();
}

BGFX_API void bgfx_shutdown() {
	bgfx::shutdown();
}

BGFX_API void bgfx_reset(uint32_t w, uint32_t h) {
	bgfx::reset(w, h, BGFX_RESET_VSYNC);
}

BGFX_API void bgfx_frame() {
	bgfx::frame();
}

BGFX_API void bgfx_submit(uint8_t viewID) {
	bgfx::submit(viewID);
}

BGFX_API void bgfx_setDebug(uint32_t f) {
	bgfx::setDebug(f);
}

BGFX_API void bgfx_dbgTextClear() {
	bgfx::dbgTextClear();
}

BGFX_API void bgfx_dbgTextPrint(uint32_t x, uint32_t y, uint8_t attr, const char *text) {
	bgfx::dbgTextPrintf(x, y, attr, "%s", text);
}

BGFX_API void bgfx_setViewRect(uint8_t id, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
	bgfx::setViewRect(id, x, y, w, h);
}

BGFX_API void bgfx_setViewClear(uint8_t id, uint8_t flags, uint32_t rgba, float depth, uint8_t stencil) {
	bgfx::setViewClear(id, flags, rgba, depth, stencil);
}

#ifdef BX_PLATFORM_OSX
BGFX_API void bgfx_osxSetNSWindow(void* wnd) {
	bgfx::osxSetNSWindow(wnd);
}
#endif
