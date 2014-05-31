#include <bgfx.h>
#include <bgfxplatform.h>
#include "bridge.h"

BGFX_C_API void bgfx_dbg_text_print(uint32_t x, uint32_t y, uint8_t attr, const char *text) {
	bgfx::dbgTextPrintf(x, y, attr, "%s", text);
}

#ifdef BX_PLATFORM_OSX
BGFX_C_API void bgfx_osx_set_nswindow(void* wnd) {
	bgfx::osxSetNSWindow(wnd);
}
#endif
