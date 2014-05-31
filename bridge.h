#ifdef __cplusplus
#define BGFX_C_API extern "C"
#else
#define BGFX_C_API
#endif

// serves as cgo's interface to bgfx_dbg_text_printf, since cgo does not
// like varargs (wouldn't be typesafe).
BGFX_C_API void bgfx_dbg_text_print(uint32_t x, uint32_t y, uint8_t attr, const char *text);
