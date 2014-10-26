package bgfx

// Test files do not allow cgo, so we define cgo values to test against
// here.

// #include "bgfx.c99.h"
// #include "bgfxdefines.h"
import "C"

var rendererTypeTable = []struct {
	a RendererType
	b C.bgfx_renderer_type_t
}{
	{RendererTypeNull, C.BGFX_RENDERER_TYPE_NULL},
	{RendererTypeDirect3D9, C.BGFX_RENDERER_TYPE_DIRECT3D9},
	{RendererTypeDirect3D11, C.BGFX_RENDERER_TYPE_DIRECT3D11},
	{RendererTypeOpenGLES, C.BGFX_RENDERER_TYPE_OPENGLES},
	{RendererTypeOpenGL, C.BGFX_RENDERER_TYPE_OPENGL},
	{RendererTypeCount, C.BGFX_RENDERER_TYPE_COUNT},
}

var resetFlagsTable = []struct {
	a ResetFlags
	b C.uint32_t
}{
	{ResetFullscreen, C.BGFX_RESET_FULLSCREEN},
	{ResetMSAAX2, C.BGFX_RESET_MSAA_X2},
	{ResetMSAAX4, C.BGFX_RESET_MSAA_X4},
	{ResetMSAAX8, C.BGFX_RESET_MSAA_X8},
	{ResetMSAAX16, C.BGFX_RESET_MSAA_X16},
	{ResetMSAAShift, C.BGFX_RESET_MSAA_SHIFT},
	{ResetMSAAMask, C.BGFX_RESET_MSAA_MASK},
	{ResetVSync, C.BGFX_RESET_VSYNC},
	{ResetCapture, C.BGFX_RESET_CAPTURE},
}

var debugOptionsTable = []struct {
	a DebugOptions
	b C.uint32_t
}{
	{DebugWireframe, C.BGFX_DEBUG_WIREFRAME},
	{DebugIFH, C.BGFX_DEBUG_IFH},
	{DebugStats, C.BGFX_DEBUG_STATS},
	{DebugText, C.BGFX_DEBUG_TEXT},
}

var clearOptionsTable = []struct {
	a ClearOptions
	b C.uint8_t
}{
	{ClearColor, C.BGFX_CLEAR_COLOR_BIT},
	{ClearDepth, C.BGFX_CLEAR_DEPTH_BIT},
	{ClearStencil, C.BGFX_CLEAR_STENCIL_BIT},
}

var capFlagsTable = []struct {
	a CapFlags
	b C.uint64_t
}{
	{CapsTextureCompareLEqual, C.BGFX_CAPS_TEXTURE_COMPARE_LEQUAL},
	{CapsTextureCompareAll, C.BGFX_CAPS_TEXTURE_COMPARE_ALL},
	{CapsTexture3D, C.BGFX_CAPS_TEXTURE_3D},
	{CapsVertexAttribHalf, C.BGFX_CAPS_VERTEX_ATTRIB_HALF},
	{CapsInstancing, C.BGFX_CAPS_INSTANCING},
	{CapsRendererMultithreaded, C.BGFX_CAPS_RENDERER_MULTITHREADED},
	{CapsFragmentDepth, C.BGFX_CAPS_FRAGMENT_DEPTH},
	{CapsBlendIndependent, C.BGFX_CAPS_BLEND_INDEPENDENT},
	{CapsCompute, C.BGFX_CAPS_COMPUTE},
	{CapsFragmentOrdering, C.BGFX_CAPS_FRAGMENT_ORDERING},
	{CapsSwapChain, C.BGFX_CAPS_SWAP_CHAIN},
}

var uniformTypeTable = []struct {
	a UniformType
	b C.uint8_t
}{
	{Uniform1i, C.BGFX_UNIFORM_TYPE_UNIFORM1I},
	{Uniform1f, C.BGFX_UNIFORM_TYPE_UNIFORM1F},
	//{UniformEnd,C.BGFX_UNIFORM_TYPE_END,},

	{Uniform1iv, C.BGFX_UNIFORM_TYPE_UNIFORM1IV},
	{Uniform1fv, C.BGFX_UNIFORM_TYPE_UNIFORM1FV},
	{Uniform2fv, C.BGFX_UNIFORM_TYPE_UNIFORM2FV},
	{Uniform3fv, C.BGFX_UNIFORM_TYPE_UNIFORM3FV},
	{Uniform4fv, C.BGFX_UNIFORM_TYPE_UNIFORM4FV},
	{Uniform3x3fv, C.BGFX_UNIFORM_TYPE_UNIFORM3X3FV},
	{Uniform4x4fv, C.BGFX_UNIFORM_TYPE_UNIFORM4X4FV},

	{UniformTypeCount, C.BGFX_UNIFORM_TYPE_COUNT},
}

var attribTable = []struct {
	a Attrib
	b C.bgfx_attrib_t
}{
	{AttribPosition, C.BGFX_ATTRIB_POSITION},
	{AttribNormal, C.BGFX_ATTRIB_NORMAL},
	{AttribTangent, C.BGFX_ATTRIB_TANGENT},
	{AttribBitangent, C.BGFX_ATTRIB_BITANGENT},
	{AttribColor0, C.BGFX_ATTRIB_COLOR0},
	{AttribColor1, C.BGFX_ATTRIB_COLOR1},
	{AttribIndices, C.BGFX_ATTRIB_INDICES},
	{AttribWeight, C.BGFX_ATTRIB_WEIGHT},
	{AttribTexcoord0, C.BGFX_ATTRIB_TEXCOORD0},
	{AttribTexcoord1, C.BGFX_ATTRIB_TEXCOORD1},
	{AttribTexcoord2, C.BGFX_ATTRIB_TEXCOORD2},
	{AttribTexcoord3, C.BGFX_ATTRIB_TEXCOORD3},
	{AttribTexcoord4, C.BGFX_ATTRIB_TEXCOORD4},
	{AttribTexcoord5, C.BGFX_ATTRIB_TEXCOORD5},
	{AttribTexcoord6, C.BGFX_ATTRIB_TEXCOORD6},
	{AttribTexcoord7, C.BGFX_ATTRIB_TEXCOORD7},
	{AttribCount, C.BGFX_ATTRIB_COUNT},
}

var attribTypeTable = []struct {
	a AttribType
	b C.bgfx_attrib_type_t
}{
	{AttribTypeUint8, C.BGFX_ATTRIB_TYPE_UINT8},
	{AttribTypeInt16, C.BGFX_ATTRIB_TYPE_INT16},
	{AttribTypeHalf, C.BGFX_ATTRIB_TYPE_HALF},
	{AttribTypeFloat, C.BGFX_ATTRIB_TYPE_FLOAT},

	{AttribTypeCount, C.BGFX_ATTRIB_TYPE_COUNT},
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

var texFlagsTable = []struct {
	a TextureFlags
	b C.uint32_t
}{
	//{TextureNone, C.BGFX_TEXTURE_NONE},
	{TextureUMirror, C.BGFX_TEXTURE_U_MIRROR},
	{TextureUClamp, C.BGFX_TEXTURE_U_CLAMP},
	{TextureUShift, C.BGFX_TEXTURE_U_SHIFT},
	{TextureUMask, C.BGFX_TEXTURE_U_MASK},
	{TextureVMirror, C.BGFX_TEXTURE_V_MIRROR},
	{TextureVClamp, C.BGFX_TEXTURE_V_CLAMP},
	{TextureVShift, C.BGFX_TEXTURE_V_SHIFT},
	{TextureVMask, C.BGFX_TEXTURE_V_MASK},
	{TextureWMirror, C.BGFX_TEXTURE_W_MIRROR},
	{TextureWClamp, C.BGFX_TEXTURE_W_CLAMP},
	{TextureWShift, C.BGFX_TEXTURE_W_SHIFT},
	{TextureWMask, C.BGFX_TEXTURE_W_MASK},
	{TextureMinPoint, C.BGFX_TEXTURE_MIN_POINT},
	{TextureMinAnisotropic, C.BGFX_TEXTURE_MIN_ANISOTROPIC},
	{TextureMinShift, C.BGFX_TEXTURE_MIN_SHIFT},
	{TextureMinMask, C.BGFX_TEXTURE_MIN_MASK},
	{TextureMagPoint, C.BGFX_TEXTURE_MAG_POINT},
	{TextureMagAnisotropic, C.BGFX_TEXTURE_MAG_ANISOTROPIC},
	{TextureMagShift, C.BGFX_TEXTURE_MAG_SHIFT},
	{TextureMagMask, C.BGFX_TEXTURE_MAG_MASK},
	{TextureMipPoint, C.BGFX_TEXTURE_MIP_POINT},
	{TextureMipShift, C.BGFX_TEXTURE_MIP_SHIFT},
	{TextureMipMask, C.BGFX_TEXTURE_MIP_MASK},
	{TextureRT, C.BGFX_TEXTURE_RT},
	{TextureRTMSAAX2, C.BGFX_TEXTURE_RT_MSAA_X2},
	{TextureRTMSAAX4, C.BGFX_TEXTURE_RT_MSAA_X4},
	{TextureRTMSAAX8, C.BGFX_TEXTURE_RT_MSAA_X8},
	{TextureRTMSAAX16, C.BGFX_TEXTURE_RT_MSAA_X16},
	{TextureRTMSAAShift, C.BGFX_TEXTURE_RT_MSAA_SHIFT},
	{TextureRTMSAAMask, C.BGFX_TEXTURE_RT_MSAA_MASK},
	{TextureRTBufferOnly, C.BGFX_TEXTURE_RT_BUFFER_ONLY},
	{TextureRTMask, C.BGFX_TEXTURE_RT_MASK},
	{TextureCompareLess, C.BGFX_TEXTURE_COMPARE_LESS},
	{TextureCompareLEqual, C.BGFX_TEXTURE_COMPARE_LEQUAL},
	{TextureCompareEqual, C.BGFX_TEXTURE_COMPARE_EQUAL},
	{TextureCompareGEqual, C.BGFX_TEXTURE_COMPARE_GEQUAL},
	{TextureCompareGreater, C.BGFX_TEXTURE_COMPARE_GREATER},
	{TextureCompareNotEqual, C.BGFX_TEXTURE_COMPARE_NOTEQUAL},

	{TextureCompareNever, C.BGFX_TEXTURE_COMPARE_NEVER},
	{TextureCompareAlways, C.BGFX_TEXTURE_COMPARE_ALWAYS},
	{TextureCompareShift, C.BGFX_TEXTURE_COMPARE_SHIFT},
	{TextureCompareMask, C.BGFX_TEXTURE_COMPARE_MASK},
	{TextureComputeWrite, C.BGFX_TEXTURE_COMPUTE_WRITE},
	//{TextureReservedShift, C.BGFX_TEXTURE_RESERVED_SHIFT},
	//{TextureReservedMask, C.BGFX_TEXTURE_RESERVED_MASK},
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
