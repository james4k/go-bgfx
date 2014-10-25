package bgfx

// Test files do not allow cgo, so we define cgo values to test against
// here.

// #include "bgfx.c99.h"
// #include "bgfxdefines.h"
import "C"

// TODO: CapFlags, UniformType, TextureFlags, Attrib
// TODO: AttribType, DebugOptions, ClearOptions

var resetFlagsTable = []struct {
	a ResetFlags
	b C.uint32_t
}{
	{ResetVSync, C.BGFX_RESET_VSYNC},
}

var rendererTypeTable = []struct {
	a RendererType
	b C.bgfx_renderer_type_t
}{
	{RendererTypeNull, C.BGFX_RENDERER_TYPE_NULL},
	{RendererTypeDirect3D9, C.BGFX_RENDERER_TYPE_DIRECT3D9},
	{RendererTypeDirect3D11, C.BGFX_RENDERER_TYPE_DIRECT3D11},
	{RendererTypeOpenGLES, C.BGFX_RENDERER_TYPE_OPENGLES},
	{RendererTypeOpenGL, C.BGFX_RENDERER_TYPE_OPENGL},
}

var texFormatTable = []struct {
	a TextureFormat
	b C.bgfx_texture_format_t
}{
	{TextureFormatBC1, C.BGFX_TEXTURE_FORMAT_BC1},
	{TextureFormatBC2, C.BGFX_TEXTURE_FORMAT_BC2},
	{TextureFormatBC3, C.BGFX_TEXTURE_FORMAT_BC3},
	{TextureFormatBC4, C.BGFX_TEXTURE_FORMAT_BC4},
	{TextureFormatBC5, C.BGFX_TEXTURE_FORMAT_BC5},
	{TextureFormatBC6H, C.BGFX_TEXTURE_FORMAT_BC6H},
	{TextureFormatBC7, C.BGFX_TEXTURE_FORMAT_BC7},
	{TextureFormatETC1, C.BGFX_TEXTURE_FORMAT_ETC1},
	{TextureFormatETC2, C.BGFX_TEXTURE_FORMAT_ETC2},
	{TextureFormatETC2A, C.BGFX_TEXTURE_FORMAT_ETC2A},
	{TextureFormatETC2A1, C.BGFX_TEXTURE_FORMAT_ETC2A1},
	{TextureFormatPTC12, C.BGFX_TEXTURE_FORMAT_PTC12},
	{TextureFormatPTC14, C.BGFX_TEXTURE_FORMAT_PTC14},
	{TextureFormatPTC12A, C.BGFX_TEXTURE_FORMAT_PTC12A},
	{TextureFormatPTC14A, C.BGFX_TEXTURE_FORMAT_PTC14A},
	{TextureFormatPTC22, C.BGFX_TEXTURE_FORMAT_PTC22},
	{TextureFormatPTC24, C.BGFX_TEXTURE_FORMAT_PTC24},
	{TextureFormatUnknown, C.BGFX_TEXTURE_FORMAT_UNKNOWN},
	{TextureFormatR1, C.BGFX_TEXTURE_FORMAT_R1},
	{TextureFormatR8, C.BGFX_TEXTURE_FORMAT_R8},
	{TextureFormatR16, C.BGFX_TEXTURE_FORMAT_R16},
	{TextureFormatR16F, C.BGFX_TEXTURE_FORMAT_R16F},
	{TextureFormatR32, C.BGFX_TEXTURE_FORMAT_R32},
	{TextureFormatR32F, C.BGFX_TEXTURE_FORMAT_R32F},
	{TextureFormatRG8, C.BGFX_TEXTURE_FORMAT_RG8},
	{TextureFormatRG16, C.BGFX_TEXTURE_FORMAT_RG16},
	{TextureFormatRG16F, C.BGFX_TEXTURE_FORMAT_RG16F},
	{TextureFormatRG32, C.BGFX_TEXTURE_FORMAT_RG32},
	{TextureFormatRG32F, C.BGFX_TEXTURE_FORMAT_RG32F},
	{TextureFormatBGRA8, C.BGFX_TEXTURE_FORMAT_BGRA8},
	{TextureFormatRGBA16, C.BGFX_TEXTURE_FORMAT_RGBA16},
	{TextureFormatRGBA16F, C.BGFX_TEXTURE_FORMAT_RGBA16F},
	{TextureFormatRGBA32, C.BGFX_TEXTURE_FORMAT_RGBA32},
	{TextureFormatRGBA32F, C.BGFX_TEXTURE_FORMAT_RGBA32F},
	{TextureFormatR5G6B5, C.BGFX_TEXTURE_FORMAT_R5G6B5},
	{TextureFormatRGBA4, C.BGFX_TEXTURE_FORMAT_RGBA4},
	{TextureFormatRGB5A1, C.BGFX_TEXTURE_FORMAT_RGB5A1},
	{TextureFormatRGB10A2, C.BGFX_TEXTURE_FORMAT_RGB10A2},
	{TextureFormatR11G11B10F, C.BGFX_TEXTURE_FORMAT_R11G11B10F},
	{TextureFormatUnknownDepth, C.BGFX_TEXTURE_FORMAT_UNKNOWN_DEPTH},
	{TextureFormatD16, C.BGFX_TEXTURE_FORMAT_D16},
	{TextureFormatD24, C.BGFX_TEXTURE_FORMAT_D24},
	{TextureFormatD24S8, C.BGFX_TEXTURE_FORMAT_D24S8},
	{TextureFormatD32, C.BGFX_TEXTURE_FORMAT_D32},
	{TextureFormatD16F, C.BGFX_TEXTURE_FORMAT_D16F},
	{TextureFormatD24F, C.BGFX_TEXTURE_FORMAT_D24F},
	{TextureFormatD32F, C.BGFX_TEXTURE_FORMAT_D32F},
	{TextureFormatD0S8, C.BGFX_TEXTURE_FORMAT_D0S8},
	{TextureFormatCount, C.BGFX_TEXTURE_FORMAT_COUNT},
}

var stateTable = []struct {
	a State
	b C.uint64_t
}{
	{StateDefault, C.BGFX_STATE_DEFAULT},
	{StateRGBWrite, C.BGFX_STATE_RGB_WRITE},
	{StateAlphaWrite, C.BGFX_STATE_ALPHA_WRITE},
	{StateDepthWrite, C.BGFX_STATE_DEPTH_WRITE},
	{StateDepthTestLess, C.BGFX_STATE_DEPTH_TEST_LESS},
	{StateDepthTestLessEqual, C.BGFX_STATE_DEPTH_TEST_LEQUAL},
	{StateCullCW, C.BGFX_STATE_CULL_CW},
	{StateCullCCW, C.BGFX_STATE_CULL_CCW},
	{StateCullMask, C.BGFX_STATE_CULL_MASK},
	{StateMSAA, C.BGFX_STATE_MSAA},
	{StateBlendMask, C.BGFX_STATE_BLEND_MASK},
}

var blendTable = []struct {
	a BlendValue
	b C.uint64_t
}{
	{BlendShift, C.BGFX_STATE_BLEND_SHIFT},
	{BlendZero, C.BGFX_STATE_BLEND_ZERO},
	{BlendOne, C.BGFX_STATE_BLEND_ONE},
	{BlendSrcColor, C.BGFX_STATE_BLEND_SRC_COLOR},
	{BlendInvSrcColor, C.BGFX_STATE_BLEND_INV_SRC_COLOR},
	{BlendSrcAlpha, C.BGFX_STATE_BLEND_SRC_ALPHA},
	{BlendInvSrcAlpha, C.BGFX_STATE_BLEND_INV_SRC_ALPHA},
	{BlendDstAlpha, C.BGFX_STATE_BLEND_DST_ALPHA},
	{BlendInvDstAlpha, C.BGFX_STATE_BLEND_INV_DST_ALPHA},
	{BlendDstColor, C.BGFX_STATE_BLEND_DST_COLOR},
	{BlendInvDstColor, C.BGFX_STATE_BLEND_INV_DST_COLOR},
	{BlendSrcAlphaSat, C.BGFX_STATE_BLEND_SRC_ALPHA_SAT},
	{BlendFactor, C.BGFX_STATE_BLEND_FACTOR},
	{BlendInvFactor, C.BGFX_STATE_BLEND_INV_FACTOR},
}
