#include <stdint.h>

#ifdef __cplusplus
#define BGFX_API extern "C"
#else
#define BGFX_API
#endif

BGFX_API void bgfx_init();
BGFX_API void bgfx_shutdown();

BGFX_API void bgfx_reset(uint32_t w, uint32_t h);
BGFX_API void bgfx_frame();
BGFX_API void bgfx_submit(uint8_t viewID);

BGFX_API void bgfx_setDebug(uint32_t f);
BGFX_API void bgfx_dbgTextClear();
BGFX_API void bgfx_dbgTextPrint(uint32_t x, uint32_t y, uint8_t attr, const char *text);

BGFX_API void bgfx_setViewRect(uint8_t id, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
BGFX_API void bgfx_setViewClear(uint8_t id, uint8_t flags, uint32_t rgba, float depth, uint8_t stencil);
