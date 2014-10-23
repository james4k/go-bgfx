/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

// This shim of sorts is a temporary solution until the go
// tool gets .mm support, or bgfx provides a context API.

#include "bgfx_p.h"

#if BX_PLATFORM_OSX && (BGFX_CONFIG_RENDERER_OPENGLES2|BGFX_CONFIG_RENDERER_OPENGLES3|BGFX_CONFIG_RENDERER_OPENGL)
#	include "renderer_gl.h"
#	include <bx/os.h>

namespace bgfx
{

#	define GL_IMPORT(_optional, _proto, _func, _import) _proto _func
#	include "glimports.h"
	
	struct SwapChainGL
	{
		SwapChainGL(void* _nwh)
		{
			BX_UNUSED(_nwh);
		}

		~SwapChainGL()
		{
		}

		void makeCurrent()
		{
		}

		void swapBuffers()
		{
		}
	};
	
	static void* s_opengl = NULL;

	extern "C"
	{
		struct GlCtx_s
		{
			void* view;
			void* m_context;
		} typedef GlCtx;

		void bgfx_GlContext_create(GlCtx *ctx, void* nsctx, uint32_t _width, uint32_t _height);
		void bgfx_GlContext_destroy(GlCtx *ctx);
		void bgfx_GlContext_resize(GlCtx *ctx, uint32_t _width, uint32_t _height, bool _vsync);
		void bgfx_GlContext_swap(GlCtx *ctx);
	}

	void GlContext::create(uint32_t _width, uint32_t _height)
	{
		BX_UNUSED(_width, _height);

		s_opengl = bx::dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/Current/OpenGL");
		BX_CHECK(NULL != s_opengl, "OpenGL dynamic library is not found!");

		bgfx_GlContext_create((GlCtx*)this, g_bgfxNSWindow, _width, _height);
		import();
	}

	void GlContext::destroy()
	{
		bgfx_GlContext_destroy((GlCtx*)this);
		bx::dlclose(s_opengl);
	}

	void GlContext::resize(uint32_t _width, uint32_t _height, bool _vsync)
	{
		bgfx_GlContext_resize((GlCtx*)this, _width, _height, _vsync);
	}

	bool GlContext::isSwapChainSupported()
	{
		return false;
	}

	SwapChainGL* GlContext::createSwapChain(void* _nwh)
	{
		return BX_NEW(g_allocator, SwapChainGL)(_nwh);
	}

	void GlContext::destorySwapChain(SwapChainGL* _swapChain)
	{
		BX_DELETE(g_allocator, _swapChain);
	}

	void GlContext::swap(SwapChainGL* _swapChain)
	{
		if (NULL == _swapChain)
		{
			bgfx_GlContext_swap((GlCtx*)this);
		}
		else
		{
			_swapChain->makeCurrent();
			_swapChain->swapBuffers();
		}
	}

	void GlContext::makeCurrent(SwapChainGL* _swapChain)
	{
		if (NULL == _swapChain)
		{
		}
		else
		{
			_swapChain->makeCurrent();
		}
	}

	void GlContext::import()
	{
		BX_TRACE("Import:");
#	define GL_EXTENSION(_optional, _proto, _func, _import) \
				{ \
					if (_func == NULL) \
					{ \
						_func = (_proto)bx::dlsym(s_opengl, #_import); \
						BX_TRACE("%p " #_func " (" #_import ")", _func); \
					} \
					BGFX_FATAL(_optional || NULL != _func, Fatal::UnableToInitialize, "Failed to create OpenGL context. NSGLGetProcAddress(\"%s\")", #_import); \
				}
#	include "glimports.h"
	}

} // namespace bgfx

#endif // BX_PLATFORM_OSX && (BGFX_CONFIG_RENDERER_OPENGLES2|BGFX_CONFIG_RENDERER_OPENGLES3|BGFX_CONFIG_RENDERER_OPENGL)
