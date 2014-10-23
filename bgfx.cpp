// Generate file with prepare.sh
/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "bgfx_p.h"

namespace bgfx
{
#define BGFX_MAIN_THREAD_MAGIC 0x78666762

#if BGFX_CONFIG_MULTITHREADED && !BX_PLATFORM_OSX && !BX_PLATFORM_IOS
#	define BGFX_CHECK_MAIN_THREAD() \
				BX_CHECK(NULL != s_ctx, "Library is not initialized yet."); \
				BX_CHECK(BGFX_MAIN_THREAD_MAGIC == s_threadIndex, "Must be called from main thread.")
#	define BGFX_CHECK_RENDER_THREAD() BX_CHECK(BGFX_MAIN_THREAD_MAGIC != s_threadIndex, "Must be called from render thread.")
#else
#	define BGFX_CHECK_MAIN_THREAD()
#	define BGFX_CHECK_RENDER_THREAD()
#endif // BGFX_CONFIG_MULTITHREADED && !BX_PLATFORM_OSX && !BX_PLATFORM_IOS

#if BX_PLATFORM_ANDROID
	::ANativeWindow* g_bgfxAndroidWindow = NULL;
	void androidSetWindow(::ANativeWindow* _window)
	{
		g_bgfxAndroidWindow = _window;
	}
#elif BX_PLATFORM_IOS
	void* g_bgfxEaglLayer = NULL;
	void iosSetEaglLayer(void* _layer)
	{
		g_bgfxEaglLayer = _layer;
	}

#elif BX_PLATFORM_OSX
	void* g_bgfxNSWindow = NULL;

	void osxSetNSWindow(void* _window)
	{
		g_bgfxNSWindow = _window;
	}
#elif BX_PLATFORM_WINDOWS
	::HWND g_bgfxHwnd = NULL;

	void winSetHwnd(::HWND _window)
	{
		g_bgfxHwnd = _window;
	}
#endif // BX_PLATFORM_*

#if BGFX_CONFIG_USE_TINYSTL
	void* TinyStlAllocator::static_allocate(size_t _bytes)
	{
		return BX_ALLOC(g_allocator, _bytes);
	}

	void TinyStlAllocator::static_deallocate(void* _ptr, size_t /*_bytes*/)
	{
		if (NULL != _ptr)
		{
			BX_FREE(g_allocator, _ptr);
		}
	}
#endif // BGFX_CONFIG_USE_TINYSTL

	struct CallbackStub : public CallbackI
	{
		virtual ~CallbackStub()
		{
		}

		virtual void fatal(Fatal::Enum _code, const char* _str) BX_OVERRIDE
		{
			if (Fatal::DebugCheck == _code)
			{
				bx::debugBreak();
			}
			else
			{
				BX_TRACE("0x%08x: %s", _code, _str);
				BX_UNUSED(_code, _str);
				abort();
			}
		}

		virtual uint32_t cacheReadSize(uint64_t /*_id*/) BX_OVERRIDE
		{
			return 0;
		}

		virtual bool cacheRead(uint64_t /*_id*/, void* /*_data*/, uint32_t /*_size*/) BX_OVERRIDE
		{
			return false;
		}

		virtual void cacheWrite(uint64_t /*_id*/, const void* /*_data*/, uint32_t /*_size*/) BX_OVERRIDE
		{
		}

		virtual void screenShot(const char* _filePath, uint32_t _width, uint32_t _height, uint32_t _pitch, const void* _data, uint32_t _size, bool _yflip) BX_OVERRIDE
		{
			BX_UNUSED(_filePath, _width, _height, _pitch, _data, _size, _yflip);

#if BX_CONFIG_CRT_FILE_READER_WRITER
			char* filePath = (char*)alloca(strlen(_filePath)+5);
			strcpy(filePath, _filePath);
			strcat(filePath, ".tga");

			bx::CrtFileWriter writer;
			if (0 == writer.open(filePath) )
			{
				imageWriteTga(&writer, _width, _height, _pitch, _data, false, _yflip);
				writer.close();
			}
#endif // BX_CONFIG_CRT_FILE_READER_WRITER
		}

		virtual void captureBegin(uint32_t /*_width*/, uint32_t /*_height*/, uint32_t /*_pitch*/, TextureFormat::Enum /*_format*/, bool /*_yflip*/) BX_OVERRIDE
		{
			BX_TRACE("Warning: using capture without callback (a.k.a. pointless).");
		}

		virtual void captureEnd() BX_OVERRIDE
		{
		}

		virtual void captureFrame(const void* /*_data*/, uint32_t /*_size*/) BX_OVERRIDE
		{
		}
	};

#ifndef BGFX_CONFIG_MEMORY_TRACKING
#	define BGFX_CONFIG_MEMORY_TRACKING (BGFX_CONFIG_DEBUG && BX_CONFIG_SUPPORTS_THREADING)
#endif // BGFX_CONFIG_MEMORY_TRACKING

	class AllocatorStub : public bx::ReallocatorI
	{
	public:
		AllocatorStub()
#if BGFX_CONFIG_MEMORY_TRACKING
			: m_numBlocks(0)
			, m_maxBlocks(0)
#endif // BGFX_CONFIG_MEMORY_TRACKING
		{
		}

		virtual void* alloc(size_t _size, size_t _align, const char* _file, uint32_t _line) BX_OVERRIDE
		{
			if (BX_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT >= _align)
			{
#if BGFX_CONFIG_MEMORY_TRACKING
				{
					bx::LwMutexScope scope(m_mutex);
					++m_numBlocks;
					m_maxBlocks = bx::uint32_max(m_maxBlocks, m_numBlocks);
				}
#endif // BGFX_CONFIG_MEMORY_TRACKING

				return ::malloc(_size);
			}

			return bx::alignedAlloc(this, _size, _align, _file, _line);
		}

		virtual void free(void* _ptr, size_t _align, const char* _file, uint32_t _line) BX_OVERRIDE
		{
			if (NULL != _ptr)
			{
				if (BX_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT >= _align)
				{
#if BGFX_CONFIG_MEMORY_TRACKING
					{
						bx::LwMutexScope scope(m_mutex);
						BX_CHECK(m_numBlocks > 0, "Number of blocks is 0. Possible alloc/free mismatch?");
						--m_numBlocks;
					}
#endif // BGFX_CONFIG_MEMORY_TRACKING

					::free(_ptr);
				}
				else
				{
					bx::alignedFree(this, _ptr, _align, _file, _line);
				}
			}
		}

		virtual void* realloc(void* _ptr, size_t _size, size_t _align, const char* _file, uint32_t _line) BX_OVERRIDE
		{
			if (BX_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT >= _align)
			{
#if BGFX_CONFIG_MEMORY_TRACKING
				if (NULL == _ptr)
				{
					bx::LwMutexScope scope(m_mutex);
					++m_numBlocks;
					m_maxBlocks = bx::uint32_max(m_maxBlocks, m_numBlocks);
				}
#endif // BGFX_CONFIG_MEMORY_TRACKING

				return ::realloc(_ptr, _size);
			}

			return bx::alignedRealloc(this, _ptr, _size, _align, _file, _line);
		}

		void checkLeaks()
		{
#if BGFX_CONFIG_MEMORY_TRACKING
			BX_WARN(0 == m_numBlocks, "MEMORY LEAK: %d (max: %d)", m_numBlocks, m_maxBlocks);
#endif // BGFX_CONFIG_MEMORY_TRACKING
		}

	protected:
#if BGFX_CONFIG_MEMORY_TRACKING
		bx::LwMutex m_mutex;
		uint32_t m_numBlocks;
		uint32_t m_maxBlocks;
#endif // BGFX_CONFIG_MEMORY_TRACKING
	};

	static CallbackStub* s_callbackStub = NULL;
	static AllocatorStub* s_allocatorStub = NULL;
	static bool s_graphicsDebuggerPresent = false;

	CallbackI* g_callback = NULL;
	bx::ReallocatorI* g_allocator = NULL;

	Caps g_caps;

	static BX_THREAD uint32_t s_threadIndex = 0;
	static Context* s_ctx = NULL;
	static bool s_renderFrameCalled = false;

	void setGraphicsDebuggerPresent(bool _present)
	{
		BX_TRACE("Graphics debugger is %spresent.", _present ? "" : "not ");
		s_graphicsDebuggerPresent = _present;
	}

	bool isGraphicsDebuggerPresent()
	{
		return s_graphicsDebuggerPresent;
	}

	void fatal(Fatal::Enum _code, const char* _format, ...)
	{
		char temp[8192];

		va_list argList;
		va_start(argList, _format);
		bx::vsnprintf(temp, sizeof(temp), _format, argList);
		va_end(argList);

		temp[sizeof(temp)-1] = '\0';

		g_callback->fatal(_code, temp);
	}

	void mtxOrtho(float* _result, float _left, float _right, float _bottom, float _top, float _near, float _far)
	{
		const float aa = 2.0f/(_right - _left);
		const float bb = 2.0f/(_top - _bottom);
		const float cc = 1.0f/(_far - _near);
		const float dd = (_left + _right)/(_left - _right);
		const float ee = (_top + _bottom)/(_bottom - _top);
		const float ff = _near / (_near - _far);

		memset(_result, 0, sizeof(float)*16);
		_result[0] = aa;
		_result[5] = bb;
		_result[10] = cc;
		_result[12] = dd;
		_result[13] = ee;
		_result[14] = ff;
		_result[15] = 1.0f;
	}

#include "charset.h"

	void charsetFillTexture(const uint8_t* _charset, uint8_t* _rgba, uint32_t _height, uint32_t _pitch, uint32_t _bpp)
	{
		for (uint32_t ii = 0; ii < 256; ++ii)
		{
			uint8_t* pix = &_rgba[ii*8*_bpp];
			for (uint32_t yy = 0; yy < _height; ++yy)
			{
				for (uint32_t xx = 0; xx < 8; ++xx)
				{
					uint8_t bit = 1<<(7-xx);
					memset(&pix[xx*_bpp], _charset[ii*_height+yy]&bit ? 255 : 0, _bpp);
				}

				pix += _pitch;
			}
		}
	}

	static const uint32_t numCharsPerBatch = 1024;
	static const uint32_t numBatchVertices = numCharsPerBatch*4;
	static const uint32_t numBatchIndices = numCharsPerBatch*6;

	void TextVideoMemBlitter::init()
	{
		BGFX_CHECK_MAIN_THREAD();
		m_decl
			.begin()
			.add(Attrib::Position,  3, AttribType::Float)
			.add(Attrib::Color0,    4, AttribType::Uint8, true)
			.add(Attrib::Color1,    4, AttribType::Uint8, true)
			.add(Attrib::TexCoord0, 2, AttribType::Float)
			.end();

		uint16_t width = 2048;
		uint16_t height = 24;
		uint8_t bpp = 1;
		uint32_t pitch = width*bpp;

		const Memory* mem;

		mem = alloc(pitch*height);
		uint8_t* rgba = mem->data;
		charsetFillTexture(vga8x8, rgba, 8, pitch, bpp);
		charsetFillTexture(vga8x16, &rgba[8*pitch], 16, pitch, bpp);
		m_texture = createTexture2D(width, height, 1, TextureFormat::R8
						, BGFX_TEXTURE_MIN_POINT
						| BGFX_TEXTURE_MAG_POINT
						| BGFX_TEXTURE_MIP_POINT
						| BGFX_TEXTURE_U_CLAMP
						| BGFX_TEXTURE_V_CLAMP
						, mem
						);

		switch (g_caps.rendererType)
		{
		case RendererType::Direct3D9:
			mem = makeRef(vs_debugfont_dx9, sizeof(vs_debugfont_dx9) );
			break;

		case RendererType::Direct3D11:
			mem = makeRef(vs_debugfont_dx11, sizeof(vs_debugfont_dx11) );
			break;

		default:
			mem = makeRef(vs_debugfont_glsl, sizeof(vs_debugfont_glsl) );
			break;
		}

		ShaderHandle vsh = createShader(mem);

		switch (g_caps.rendererType)
		{
		case RendererType::Direct3D9:
			mem = makeRef(fs_debugfont_dx9, sizeof(fs_debugfont_dx9) );
			break;

		case RendererType::Direct3D11:
			mem = makeRef(fs_debugfont_dx11, sizeof(fs_debugfont_dx11) );
			break;

		default:
			mem = makeRef(fs_debugfont_glsl, sizeof(fs_debugfont_glsl) );
			break;
		}

		ShaderHandle fsh = createShader(mem);

		m_program = createProgram(vsh, fsh, true);

		m_vb = s_ctx->createTransientVertexBuffer(numBatchVertices*m_decl.m_stride, &m_decl);
		m_ib = s_ctx->createTransientIndexBuffer(numBatchIndices*2);
	}

	void TextVideoMemBlitter::shutdown()
	{
		BGFX_CHECK_MAIN_THREAD();

		destroyProgram(m_program);
		destroyTexture(m_texture);
		s_ctx->destroyTransientVertexBuffer(m_vb);
		s_ctx->destroyTransientIndexBuffer(m_ib);
	}

	void blit(RendererContextI* _renderCtx, TextVideoMemBlitter& _blitter, const TextVideoMem& _mem)
	{
		BGFX_CHECK_RENDER_THREAD();
		struct Vertex
		{
			float m_x;
			float m_y;
			float m_z;
			uint32_t m_fg;
			uint32_t m_bg;
			float m_u;
			float m_v;
		};

		static uint32_t palette[16] =
		{
			0x0,
			0xff0000cc,
			0xff069a4e,
			0xff00a0c4,
			0xffa46534,
			0xff7b5075,
			0xff9a9806,
			0xffcfd7d3,
			0xff535755,
			0xff2929ef,
			0xff34e28a,
			0xff4fe9fc,
			0xffcf9f72,
			0xffa87fad,
			0xffe2e234,
			0xffeceeee,
		};

		uint32_t yy = 0;
		uint32_t xx = 0;

		const float texelWidth = 1.0f/2048.0f;
		const float texelWidthHalf = texelWidth*0.5f;
		const float texelHeight = 1.0f/24.0f;
		const float texelHeightHalf = RendererType::Direct3D9 == g_caps.rendererType ? texelHeight*0.5f : 0.0f;
		const float utop = (_mem.m_small ? 0.0f : 8.0f)*texelHeight + texelHeightHalf;
		const float ubottom = (_mem.m_small ? 8.0f : 24.0f)*texelHeight + texelHeightHalf;
		const float fontHeight = (_mem.m_small ? 8.0f : 16.0f);

		_renderCtx->blitSetup(_blitter);

		for (;yy < _mem.m_height;)
		{
			Vertex* vertex = (Vertex*)_blitter.m_vb->data;
			uint16_t* indices = (uint16_t*)_blitter.m_ib->data;
			uint32_t startVertex = 0;
			uint32_t numIndices = 0;

			for (; yy < _mem.m_height && numIndices < numBatchIndices; ++yy)
			{
				xx = xx < _mem.m_width ? xx : 0;
				const uint8_t* line = &_mem.m_mem[(yy*_mem.m_width+xx)*2];

				for (; xx < _mem.m_width && numIndices < numBatchIndices; ++xx)
				{
					uint8_t ch = line[0];
					uint8_t attr = line[1];

					if (0 != (ch|attr)
					&& (' ' != ch || 0 != (attr&0xf0) ) )
					{
						uint32_t fg = palette[attr&0xf];
						uint32_t bg = palette[(attr>>4)&0xf];

						Vertex vert[4] =
						{
							{ (xx  )*8.0f, (yy  )*fontHeight, 0.0f, fg, bg, (ch  )*8.0f*texelWidth - texelWidthHalf, utop },
							{ (xx+1)*8.0f, (yy  )*fontHeight, 0.0f, fg, bg, (ch+1)*8.0f*texelWidth - texelWidthHalf, utop },
							{ (xx+1)*8.0f, (yy+1)*fontHeight, 0.0f, fg, bg, (ch+1)*8.0f*texelWidth - texelWidthHalf, ubottom },
							{ (xx  )*8.0f, (yy+1)*fontHeight, 0.0f, fg, bg, (ch  )*8.0f*texelWidth - texelWidthHalf, ubottom },
						};

						memcpy(vertex, vert, sizeof(vert) );
						vertex += 4;

						indices[0] = startVertex+0;
						indices[1] = startVertex+1;
						indices[2] = startVertex+2;
						indices[3] = startVertex+2;
						indices[4] = startVertex+3;
						indices[5] = startVertex+0;

						startVertex += 4;
						indices += 6;

						numIndices += 6;
					}

					line += 2;
				}

				if (numIndices >= numBatchIndices)
				{
					break;
				}
			}

			_renderCtx->blitRender(_blitter, numIndices);
		}
	}

	void ClearQuad::init()
	{
		BGFX_CHECK_MAIN_THREAD();

		if (RendererType::Null != g_caps.rendererType)
		{
			m_decl
				.begin()
				.add(Attrib::Position, 3, AttribType::Float)
				.end();

			ShaderHandle vsh = BGFX_INVALID_HANDLE;
			
			struct Mem
			{
				Mem(const void* _data, size_t _size)
					: data(_data)
					, size(_size)
				{
				}

				const void*  data;
				size_t size;
			};

			const Memory* fragMem[BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS];
			if (RendererType::Direct3D9 == g_caps.rendererType)
			{
				vsh = createShader(makeRef(vs_clear_dx9, sizeof(vs_clear_dx9) ) );

				const Mem mem[] =
				{
					Mem(fs_clear0_dx9, sizeof(fs_clear0_dx9) ),
					Mem(fs_clear1_dx9, sizeof(fs_clear1_dx9) ),
					Mem(fs_clear2_dx9, sizeof(fs_clear2_dx9) ),
					Mem(fs_clear3_dx9, sizeof(fs_clear3_dx9) ),
					Mem(fs_clear4_dx9, sizeof(fs_clear4_dx9) ),
					Mem(fs_clear5_dx9, sizeof(fs_clear5_dx9) ),
					Mem(fs_clear6_dx9, sizeof(fs_clear6_dx9) ),
					Mem(fs_clear7_dx9, sizeof(fs_clear7_dx9) ),
				};

				for (uint32_t ii = 0, num = g_caps.maxFBAttachments; ii < num; ++ii)
				{
					fragMem[ii] = makeRef(mem[ii].data, uint32_t(mem[ii].size) );
				}
			}
			else if (RendererType::Direct3D11 == g_caps.rendererType)
			{
				vsh = createShader(makeRef(vs_clear_dx11, sizeof(vs_clear_dx11) ) );

				const Mem mem[] =
				{
					Mem(fs_clear0_dx11, sizeof(fs_clear0_dx11) ),
					Mem(fs_clear1_dx11, sizeof(fs_clear1_dx11) ),
					Mem(fs_clear2_dx11, sizeof(fs_clear2_dx11) ),
					Mem(fs_clear3_dx11, sizeof(fs_clear3_dx11) ),
					Mem(fs_clear4_dx11, sizeof(fs_clear4_dx11) ),
					Mem(fs_clear5_dx11, sizeof(fs_clear5_dx11) ),
					Mem(fs_clear6_dx11, sizeof(fs_clear6_dx11) ),
					Mem(fs_clear7_dx11, sizeof(fs_clear7_dx11) ),
				};

				for (uint32_t ii = 0, num = g_caps.maxFBAttachments; ii < num; ++ii)
				{
					fragMem[ii] = makeRef(mem[ii].data, uint32_t(mem[ii].size) );
				}
			}
			else if (RendererType::OpenGLES == g_caps.rendererType
				 ||  RendererType::OpenGL   == g_caps.rendererType)
			{
				vsh = createShader(makeRef(vs_clear_glsl, sizeof(vs_clear_glsl) ) );

				const Mem mem[] =
				{
					Mem(fs_clear0_glsl, sizeof(fs_clear0_glsl) ),
					Mem(fs_clear1_glsl, sizeof(fs_clear1_glsl) ),
					Mem(fs_clear2_glsl, sizeof(fs_clear2_glsl) ),
					Mem(fs_clear3_glsl, sizeof(fs_clear3_glsl) ),
					Mem(fs_clear4_glsl, sizeof(fs_clear4_glsl) ),
					Mem(fs_clear5_glsl, sizeof(fs_clear5_glsl) ),
					Mem(fs_clear6_glsl, sizeof(fs_clear6_glsl) ),
					Mem(fs_clear7_glsl, sizeof(fs_clear7_glsl) ),
				};

				for (uint32_t ii = 0, num = g_caps.maxFBAttachments; ii < num; ++ii)
				{
					fragMem[ii] = makeRef(mem[ii].data, uint32_t(mem[ii].size) );
				}
			}

			for (uint32_t ii = 0, num = g_caps.maxFBAttachments; ii < num; ++ii)
			{
				ShaderHandle fsh = createShader(fragMem[ii]);
				m_program[ii] = createProgram(vsh, fsh);
				BX_CHECK(isValid(m_program[ii]), "Failed to create clear quad program.");
				destroyShader(fsh);
			}

			destroyShader(vsh);

			m_vb = s_ctx->createTransientVertexBuffer(4*m_decl.m_stride, &m_decl);
		}
	}

	void ClearQuad::shutdown()
	{
		BGFX_CHECK_MAIN_THREAD();

		if (RendererType::Null != g_caps.rendererType)
		{
			for (uint32_t ii = 0, num = g_caps.maxFBAttachments; ii < num; ++ii)
			{
				if (isValid(m_program[ii]) )
				{
					destroyProgram(m_program[ii]);
					m_program[ii].idx = invalidHandle;
				}
			}

			s_ctx->destroyTransientVertexBuffer(m_vb);
		}
	}

	const char* s_uniformTypeName[UniformType::Count] =
	{
		"int",
		"float",
		NULL,
		"int",
		"float",
		"vec2",
		"vec3",
		"vec4",
		"mat3",
		"mat4",
	};

	const char* getUniformTypeName(UniformType::Enum _enum)
	{
		return s_uniformTypeName[_enum];
	}

	UniformType::Enum nameToUniformTypeEnum(const char* _name)
	{
		for (uint32_t ii = 0; ii < UniformType::Count; ++ii)
		{
			if (NULL != s_uniformTypeName[ii]
			&&  0 == strcmp(_name, s_uniformTypeName[ii]) )
			{
				return UniformType::Enum(ii);
			}
		}

		return UniformType::Count;
	}

	static const char* s_predefinedName[PredefinedUniform::Count] =
	{
		"u_viewRect",
		"u_viewTexel",
		"u_view",
		"u_invView",
		"u_proj",
		"u_invProj",
		"u_viewProj",
		"u_invViewProj",
		"u_model",
		"u_modelView",
		"u_modelViewProj",
		"u_alphaRef",
	};

	const char* getPredefinedUniformName(PredefinedUniform::Enum _enum)
	{
		return s_predefinedName[_enum];
	}

	PredefinedUniform::Enum nameToPredefinedUniformEnum(const char* _name)
	{
		for (uint32_t ii = 0; ii < PredefinedUniform::Count; ++ii)
		{
			if (0 == strcmp(_name, s_predefinedName[ii]) )
			{
				return PredefinedUniform::Enum(ii);
			}
		}

		return PredefinedUniform::Count;
	}

	uint32_t Frame::submit(uint8_t _id, int32_t _depth)
	{
		if (m_discard)
		{
			discard();
			return m_num;
		}

		if (BGFX_CONFIG_MAX_DRAW_CALLS-1 <= m_num
		|| (0 == m_draw.m_numVertices && 0 == m_draw.m_numIndices) )
		{
			++m_numDropped;
			return m_num;
		}

		m_constEnd = m_constantBuffer->getPos();

		BX_WARN(invalidHandle != m_key.m_program, "Program with invalid handle");
		if (invalidHandle != m_key.m_program)
		{
			m_key.m_depth  = _depth;
			m_key.m_view   = _id;
			m_key.m_seq    = s_ctx->m_seq[_id] & s_ctx->m_seqMask[_id];
			s_ctx->m_seq[_id]++;

			uint64_t key = m_key.encodeDraw();
			m_sortKeys[m_num]   = key;
			m_sortValues[m_num] = m_numRenderItems;
			++m_num;

			m_draw.m_constBegin = m_constBegin;
			m_draw.m_constEnd   = m_constEnd;
			m_draw.m_flags |= m_flags;
			m_renderItem[m_numRenderItems].draw = m_draw;
			++m_numRenderItems;
		}

		m_draw.clear();
		m_constBegin = m_constEnd;
		m_flags = BGFX_STATE_NONE;

		return m_num;
	}

	uint32_t Frame::dispatch(uint8_t _id, ProgramHandle _handle, uint16_t _numX, uint16_t _numY, uint16_t _numZ)
	{
		if (m_discard)
		{
			discard();
			return m_num;
		}

		if (BGFX_CONFIG_MAX_DRAW_CALLS-1 <= m_num)
		{
			++m_numDropped;
			return m_num;
		}

		m_constEnd = m_constantBuffer->getPos();

		m_compute.m_numX = bx::uint16_max(_numX, 1);
		m_compute.m_numY = bx::uint16_max(_numY, 1);
		m_compute.m_numZ = bx::uint16_max(_numZ, 1);
		m_key.m_program = _handle.idx;
		if (invalidHandle != m_key.m_program)
		{
			m_key.m_depth  = 0;
			m_key.m_view   = _id;
			m_key.m_seq    = s_ctx->m_seq[_id] & s_ctx->m_seqMask[_id];
			s_ctx->m_seq[_id]++;

			uint64_t key = m_key.encodeCompute();
			m_sortKeys[m_num]   = key;
			m_sortValues[m_num] = m_numRenderItems;
			++m_num;

			m_compute.m_constBegin = m_constBegin;
			m_compute.m_constEnd   = m_constEnd;
			m_renderItem[m_numRenderItems].compute = m_compute;
			++m_numRenderItems;
		}

		m_compute.clear();
		m_constBegin = m_constEnd;

		return m_num;
	}

	void Frame::sort()
	{
		bx::radixSort64(m_sortKeys, s_ctx->m_tempKeys, m_sortValues, s_ctx->m_tempValues, m_num);
	}

	RenderFrame::Enum renderFrame()
	{
		if (NULL == s_ctx)
		{
			s_renderFrameCalled = true;
			return RenderFrame::NoContext;
		}

		BGFX_CHECK_RENDER_THREAD();
		if (s_ctx->renderFrame() )
		{
			return RenderFrame::Exiting;
		}

		return RenderFrame::Render;
	}

	const uint32_t g_uniformTypeSize[UniformType::Count+1] =
	{
		sizeof(int32_t),
		sizeof(float),
		0,
		1*sizeof(int32_t),
		1*sizeof(float),
		2*sizeof(float),
		3*sizeof(float),
		4*sizeof(float),
		3*3*sizeof(float),
		4*4*sizeof(float),
		1,
	};

	void ConstantBuffer::writeUniform(UniformType::Enum _type, uint16_t _loc, const void* _value, uint16_t _num)
	{
		uint32_t opcode = encodeOpcode(_type, _loc, _num, true);
		write(opcode);
		write(_value, g_uniformTypeSize[_type]*_num);
	}

	void ConstantBuffer::writeUniformHandle(UniformType::Enum _type, uint16_t _loc, UniformHandle _handle, uint16_t _num)
	{
		uint32_t opcode = encodeOpcode(_type, _loc, _num, false);
		write(opcode);
		write(&_handle, sizeof(UniformHandle) );
	}

	void ConstantBuffer::writeMarker(const char* _marker)
	{
		uint16_t num = (uint16_t)strlen(_marker)+1;
		uint32_t opcode = encodeOpcode(bgfx::UniformType::Count, 0, num, true);
		write(opcode);
		write(_marker, num);
	}

	struct CapsFlags
	{
		uint64_t m_flag;
		const char* m_str;
	};

	static const CapsFlags s_capsFlags[] =
	{
#define CAPS_FLAGS(_x) { _x, #_x }
		CAPS_FLAGS(BGFX_CAPS_TEXTURE_COMPARE_LEQUAL),
		CAPS_FLAGS(BGFX_CAPS_TEXTURE_COMPARE_ALL),
		CAPS_FLAGS(BGFX_CAPS_TEXTURE_3D),
		CAPS_FLAGS(BGFX_CAPS_VERTEX_ATTRIB_HALF),
		CAPS_FLAGS(BGFX_CAPS_INSTANCING),
		CAPS_FLAGS(BGFX_CAPS_RENDERER_MULTITHREADED),
		CAPS_FLAGS(BGFX_CAPS_FRAGMENT_DEPTH),
		CAPS_FLAGS(BGFX_CAPS_BLEND_INDEPENDENT),
		CAPS_FLAGS(BGFX_CAPS_COMPUTE),
#undef CAPS_FLAGS
	};

	static void dumpCaps()
	{
		BX_TRACE("Supported capabilities (%s):", s_ctx->m_renderCtx->getRendererName() );
		for (uint32_t ii = 0; ii < BX_COUNTOF(s_capsFlags); ++ii)
		{
			if (0 != (g_caps.supported & s_capsFlags[ii].m_flag) )
			{
				BX_TRACE("\t%s", s_capsFlags[ii].m_str);
			}
		}

		BX_TRACE("Supported texture formats:");
		for (uint32_t ii = 0; ii < TextureFormat::Count; ++ii)
		{
			if (TextureFormat::Unknown != ii
			&&  TextureFormat::UnknownDepth != ii)
			{
				uint8_t flags = g_caps.formats[ii];
				BX_TRACE("\t[%c] %s"
					, flags&1 ? 'x' : flags&2 ? '*' : ' '
					, getName(TextureFormat::Enum(ii) )
					);
				BX_UNUSED(flags);
			}
		}

		BX_TRACE("Max FB attachments: %d", g_caps.maxFBAttachments);
	}

	static TextureFormat::Enum s_emulatedFormats[] =
	{
		TextureFormat::BC1,
		TextureFormat::BC2,
		TextureFormat::BC3,
		TextureFormat::BC4,
		TextureFormat::BC5,
		TextureFormat::ETC1,
		TextureFormat::ETC2,
		TextureFormat::ETC2A,
		TextureFormat::ETC2A1,
	};

	void Context::init(RendererType::Enum _type)
	{
		BX_CHECK(!m_rendererInitialized, "Already initialized?");

		m_exit = false;
		m_frames = 0;
		m_render = &m_frame[0];
		m_submit = &m_frame[1];
		m_debug = BGFX_DEBUG_NONE;

		m_submit->create();
		m_render->create();

#if BGFX_CONFIG_MULTITHREADED
		if (s_renderFrameCalled)
		{
			// When bgfx::renderFrame is called before init render thread
			// should not be created.
			BX_TRACE("Application called bgfx::renderFrame directly, not creating render thread.");
		}
		else
		{
			BX_TRACE("Creating rendering thread.");
			m_thread.init(renderThread, this);
		}
#else
		BX_TRACE("Multithreaded renderer is disabled.");
#endif // BGFX_CONFIG_MULTITHREADED

		memset(m_fb, 0xff, sizeof(m_fb) );
		memset(m_clear, 0, sizeof(m_clear) );
		memset(m_rect, 0, sizeof(m_rect) );
		memset(m_scissor, 0, sizeof(m_scissor) );
		memset(m_seq, 0, sizeof(m_seq) );
		memset(m_seqMask, 0, sizeof(m_seqMask) );

		for (uint32_t ii = 0; ii < BX_COUNTOF(m_rect); ++ii)
		{
			m_rect[ii].m_width = 1;
			m_rect[ii].m_height = 1;
		}

		for (uint32_t ii = 0; ii < BX_COUNTOF(m_clearColor); ++ii)
		{
			m_clearColor[ii][0] = 0.0f;
			m_clearColor[ii][1] = 0.0f;
			m_clearColor[ii][2] = 0.0f;
			m_clearColor[ii][3] = 1.0f;
		}

		m_declRef.init();

		CommandBuffer& cmdbuf = getCommandBuffer(CommandBuffer::RendererInit);
		cmdbuf.write(_type);

		frameNoRenderWait();

		// Make sure renderer init is called from render thread.
		// g_caps is initialized and available after this point.
		frame();

		for (uint32_t ii = 0; ii < BX_COUNTOF(s_emulatedFormats); ++ii)
		{
			if (0 == g_caps.formats[s_emulatedFormats[ii] ])
			{
				g_caps.formats[s_emulatedFormats[ii] ] = 2;
			}
		}

		g_caps.rendererType = m_renderCtx->getRendererType();
		initAttribTypeSizeTable(g_caps.rendererType);

		dumpCaps();

		m_textVideoMemBlitter.init();
		m_clearQuad.init();

		m_submit->m_transientVb = createTransientVertexBuffer(BGFX_CONFIG_TRANSIENT_VERTEX_BUFFER_SIZE);
		m_submit->m_transientIb = createTransientIndexBuffer(BGFX_CONFIG_TRANSIENT_INDEX_BUFFER_SIZE);
		frame();
		m_submit->m_transientVb = createTransientVertexBuffer(BGFX_CONFIG_TRANSIENT_VERTEX_BUFFER_SIZE);
		m_submit->m_transientIb = createTransientIndexBuffer(BGFX_CONFIG_TRANSIENT_INDEX_BUFFER_SIZE);
		frame();

		for (uint8_t ii = 0; ii < BGFX_CONFIG_MAX_VIEWS; ++ii)
		{
			char name[256];
			bx::snprintf(name, sizeof(name), "%02d view", ii);
			setViewName(ii, name);
		}
	}

	void Context::shutdown()
	{
		getCommandBuffer(CommandBuffer::RendererShutdownBegin);
		frame();

		destroyTransientVertexBuffer(m_submit->m_transientVb);
		destroyTransientIndexBuffer(m_submit->m_transientIb);
		m_textVideoMemBlitter.shutdown();
		m_clearQuad.shutdown();
		frame();

		destroyTransientVertexBuffer(m_submit->m_transientVb);
		destroyTransientIndexBuffer(m_submit->m_transientIb);
		frame();

		frame(); // If any VertexDecls needs to be destroyed.

		getCommandBuffer(CommandBuffer::RendererShutdownEnd);
		frame();

		m_declRef.shutdown(m_vertexDeclHandle);

#if BGFX_CONFIG_MULTITHREADED
		if (m_thread.isRunning() )
		{
			m_thread.shutdown();
		}
#endif // BGFX_CONFIG_MULTITHREADED

		s_ctx = NULL; // Can't be used by renderFrame at this point.
		renderSemWait();

		m_submit->destroy();
		m_render->destroy();

		if (BX_ENABLED(BGFX_CONFIG_DEBUG) )
		{
#define CHECK_HANDLE_LEAK(_handleAlloc) \
					BX_MACRO_BLOCK_BEGIN \
						BX_WARN(0 == _handleAlloc.getNumHandles() \
							, "LEAK: " #_handleAlloc " %d (max: %d)" \
							, _handleAlloc.getNumHandles() \
							, _handleAlloc.getMaxHandles() \
							); \
					BX_MACRO_BLOCK_END

			CHECK_HANDLE_LEAK(m_dynamicIndexBufferHandle);
			CHECK_HANDLE_LEAK(m_dynamicVertexBufferHandle);
			CHECK_HANDLE_LEAK(m_indexBufferHandle);
			CHECK_HANDLE_LEAK(m_vertexDeclHandle);
			CHECK_HANDLE_LEAK(m_vertexBufferHandle);
			CHECK_HANDLE_LEAK(m_shaderHandle);
			CHECK_HANDLE_LEAK(m_programHandle);
			CHECK_HANDLE_LEAK(m_textureHandle);
			CHECK_HANDLE_LEAK(m_frameBufferHandle);
			CHECK_HANDLE_LEAK(m_uniformHandle);
#undef CHECK_HANDLE_LEAK
		}
	}

	void Context::freeDynamicBuffers()
	{
		for (uint16_t ii = 0, num = m_numFreeDynamicIndexBufferHandles; ii < num; ++ii)
		{
			destroyDynamicIndexBufferInternal(m_freeDynamicIndexBufferHandle[ii]);
		}
		m_numFreeDynamicIndexBufferHandles = 0;

		for (uint16_t ii = 0, num = m_numFreeDynamicVertexBufferHandles; ii < num; ++ii)
		{
			destroyDynamicVertexBufferInternal(m_freeDynamicVertexBufferHandle[ii]);
		}
		m_numFreeDynamicVertexBufferHandles = 0;
	}

	void Context::freeAllHandles(Frame* _frame)
	{
		for (uint16_t ii = 0, num = _frame->m_numFreeIndexBufferHandles; ii < num; ++ii)
		{
			m_indexBufferHandle.free(_frame->m_freeIndexBufferHandle[ii].idx);
		}

		for (uint16_t ii = 0, num = _frame->m_numFreeVertexDeclHandles; ii < num; ++ii)
		{
			m_vertexDeclHandle.free(_frame->m_freeVertexDeclHandle[ii].idx);
		}

		for (uint16_t ii = 0, num = _frame->m_numFreeVertexBufferHandles; ii < num; ++ii)
		{
			destroyVertexBufferInternal(_frame->m_freeVertexBufferHandle[ii]);
		}

		for (uint16_t ii = 0, num = _frame->m_numFreeShaderHandles; ii < num; ++ii)
		{
			m_shaderHandle.free(_frame->m_freeShaderHandle[ii].idx);
		}

		for (uint16_t ii = 0, num = _frame->m_numFreeProgramHandles; ii < num; ++ii)
		{
			m_programHandle.free(_frame->m_freeProgramHandle[ii].idx);
		}

		for (uint16_t ii = 0, num = _frame->m_numFreeTextureHandles; ii < num; ++ii)
		{
			m_textureHandle.free(_frame->m_freeTextureHandle[ii].idx);
		}

		for (uint16_t ii = 0, num = _frame->m_numFreeFrameBufferHandles; ii < num; ++ii)
		{
			m_frameBufferHandle.free(_frame->m_freeFrameBufferHandle[ii].idx);
		}

		for (uint16_t ii = 0, num = _frame->m_numFreeUniformHandles; ii < num; ++ii)
		{
			m_uniformHandle.free(_frame->m_freeUniformHandle[ii].idx);
		}
	}

	uint32_t Context::frame()
	{
		BX_CHECK(0 == m_instBufferCount, "Instance buffer allocated, but not used. This is incorrect, and causes memory leak.");

		// wait for render thread to finish
		renderSemWait();
		frameNoRenderWait();

		return m_frames;
	}

	void Context::frameNoRenderWait()
	{
		swap();

		// release render thread
		gameSemPost();

#if !BGFX_CONFIG_MULTITHREADED
		renderFrame();
#endif // BGFX_CONFIG_MULTITHREADED
	}

	void Context::swap()
	{
		freeDynamicBuffers();
		m_submit->m_resolution = m_resolution;
		m_submit->m_debug = m_debug;
		memcpy(m_submit->m_fb, m_fb, sizeof(m_fb) );
		memcpy(m_submit->m_clear, m_clear, sizeof(m_clear) );
		memcpy(m_submit->m_rect, m_rect, sizeof(m_rect) );
		memcpy(m_submit->m_scissor, m_scissor, sizeof(m_scissor) );
		memcpy(m_submit->m_view, m_view, sizeof(m_view) );
		memcpy(m_submit->m_proj, m_proj, sizeof(m_proj) );
		if (m_clearColorDirty > 0)
		{
			--m_clearColorDirty;
			memcpy(m_submit->m_clearColor, m_clearColor, sizeof(m_clearColor) );
		}
		m_submit->finish();

		Frame* temp = m_render;
		m_render = m_submit;
		m_submit = temp;

		m_frames++;
		m_submit->start();

		memset(m_seq, 0, sizeof(m_seq) );
		freeAllHandles(m_submit);

		m_submit->resetFreeHandles();
		m_submit->m_textVideoMem->resize(m_render->m_textVideoMem->m_small, m_resolution.m_width, m_resolution.m_height);
	}

	bool Context::renderFrame()
	{
		if (m_rendererInitialized)
		{
			m_renderCtx->flip();
		}

		gameSemWait();

		rendererExecCommands(m_render->m_cmdPre);
		if (m_rendererInitialized)
		{
			m_renderCtx->submit(m_render, m_clearQuad, m_textVideoMemBlitter);
		}
		rendererExecCommands(m_render->m_cmdPost);

		renderSemPost();

		return m_exit;
	}

	void rendererUpdateUniforms(RendererContextI* _renderCtx, ConstantBuffer* _constantBuffer, uint32_t _begin, uint32_t _end)
	{
		_constantBuffer->reset(_begin);
		while (_constantBuffer->getPos() < _end)
		{
			uint32_t opcode = _constantBuffer->read();

			if (UniformType::End == opcode)
			{
				break;
			}

			UniformType::Enum type;
			uint16_t loc;
			uint16_t num;
			uint16_t copy;
			ConstantBuffer::decodeOpcode(opcode, type, loc, num, copy);

			uint32_t size = g_uniformTypeSize[type]*num;
			const char* data = _constantBuffer->read(size);
			if (UniformType::Count > type)
			{
				if (copy)
				{
					_renderCtx->updateUniform(loc, data, size);
				}
				else
				{
					_renderCtx->updateUniform(loc, *(const char**)(data), size);
				}
			}
			else
			{
				_renderCtx->setMarker(data, size);
			}
		}
	}

	void Context::flushTextureUpdateBatch(CommandBuffer& _cmdbuf)
	{
		if (m_textureUpdateBatch.sort() )
		{
			const uint32_t pos = _cmdbuf.m_pos;

			uint32_t currentKey = UINT32_MAX;

			for (uint32_t ii = 0, num = m_textureUpdateBatch.m_num; ii < num; ++ii)
			{
				_cmdbuf.m_pos = m_textureUpdateBatch.m_values[ii];

				TextureHandle handle;
				_cmdbuf.read(handle);

				uint8_t side;
				_cmdbuf.read(side);

				uint8_t mip;
				_cmdbuf.read(mip);

				Rect rect;
				_cmdbuf.read(rect);

				uint16_t zz;
				_cmdbuf.read(zz);

				uint16_t depth;
				_cmdbuf.read(depth);

				uint16_t pitch;
				_cmdbuf.read(pitch);

				Memory* mem;
				_cmdbuf.read(mem);

				uint32_t key = m_textureUpdateBatch.m_keys[ii];
				if (key != currentKey)
				{
					if (currentKey != UINT32_MAX)
					{
						m_renderCtx->updateTextureEnd();
					}
					currentKey = key;
					m_renderCtx->updateTextureBegin(handle, side, mip);
				}

				m_renderCtx->updateTexture(handle, side, mip, rect, zz, depth, pitch, mem);

				release(mem);
			}

			if (currentKey != UINT32_MAX)
			{
				m_renderCtx->updateTextureEnd();
			}

			m_textureUpdateBatch.reset();

			_cmdbuf.m_pos = pos;
		}
	}

	typedef RendererContextI* (*RendererCreateFn)();
	typedef void (*RendererDestroyFn)();

	extern RendererContextI* rendererCreateNULL();
	extern void rendererDestroyNULL();

	extern RendererContextI* rendererCreateGL();
	extern void rendererDestroyGL();

	extern RendererContextI* rendererCreateD3D9();
	extern void rendererDestroyD3D9();

	extern RendererContextI* rendererCreateD3D11();
	extern void rendererDestroyD3D11();

	extern RendererContextI* rendererCreateD3D12();
	extern void rendererDestroyD3D12();

	struct RendererCreator
	{
		RendererCreateFn  createFn;
		RendererDestroyFn destroyFn;
		const char* name;
		bool supported;
	};

	static const RendererCreator s_rendererCreator[] =
	{
		{ rendererCreateNULL,  rendererDestroyNULL,  BGFX_RENDERER_NULL_NAME,       !!BGFX_CONFIG_RENDERER_NULL       }, // Null
		{ rendererCreateD3D9,  rendererDestroyD3D9,  BGFX_RENDERER_DIRECT3D9_NAME,  !!BGFX_CONFIG_RENDERER_DIRECT3D9  }, // Direct3D9
		{ rendererCreateD3D11, rendererDestroyD3D11, BGFX_RENDERER_DIRECT3D11_NAME, !!BGFX_CONFIG_RENDERER_DIRECT3D11 }, // Direct3D11
		{ rendererCreateD3D12, rendererDestroyD3D12, BGFX_RENDERER_DIRECT3D12_NAME, !!BGFX_CONFIG_RENDERER_DIRECT3D12 }, // Direct3D12
		{ rendererCreateGL,    rendererDestroyGL,    BGFX_RENDERER_OPENGL_NAME,     !!BGFX_CONFIG_RENDERER_OPENGLES   }, // OpenGLES
		{ rendererCreateGL,    rendererDestroyGL,    BGFX_RENDERER_OPENGL_NAME,     !!BGFX_CONFIG_RENDERER_OPENGL     }, // OpenGL
	};
	BX_STATIC_ASSERT(BX_COUNTOF(s_rendererCreator) == RendererType::Count);

	static RendererDestroyFn s_rendererDestroyFn;

	bool windowsVersionIsOrAbove(uint32_t _winver)
	{
#if BX_PLATFORM_WINDOWS
		OSVERSIONINFOEXA ovi;
		memset(&ovi, 0, sizeof(ovi) );
		ovi.dwOSVersionInfoSize = sizeof(ovi);
		// _WIN32_WINNT_WINBLUE 0x0603
		// _WIN32_WINNT_WIN8    0x0602
		// _WIN32_WINNT_WIN7    0x0601
		// _WIN32_WINNT_VISTA   0x0600
		ovi.dwMajorVersion = HIBYTE(_winver);
		ovi.dwMinorVersion = LOBYTE(_winver);
		DWORDLONG cond = 0;
		VER_SET_CONDITION(cond, VER_MAJORVERSION, VER_GREATER_EQUAL);
		VER_SET_CONDITION(cond, VER_MINORVERSION, VER_GREATER_EQUAL);
		return !!VerifyVersionInfoA(&ovi, VER_MAJORVERSION | VER_MINORVERSION, cond);
#else
		BX_UNUSED(_winver);
		return false;
#endif // BX_PLATFORM_WINDOWS
	}

	RendererContextI* rendererCreate(RendererType::Enum _type)
	{
		if (RendererType::Count == _type)
		{
again:
			if (BX_ENABLED(BX_PLATFORM_WINDOWS) )
			{
				RendererType::Enum first  = RendererType::Direct3D9;
				RendererType::Enum second = RendererType::Direct3D11;
				if (windowsVersionIsOrAbove(0x0603) )
				{
					first  = RendererType::Direct3D11 /* Direct3D12 */;
					second = RendererType::Direct3D11;
				}
				else if (windowsVersionIsOrAbove(0x0601) )
				{
					first  = RendererType::Direct3D11;
					second = RendererType::Direct3D9;
				}

				if (s_rendererCreator[first].supported)
				{
					_type = first;
				}
				else if (s_rendererCreator[second].supported)
				{
					_type = second;
				}
				else if (s_rendererCreator[RendererType::OpenGL].supported)
				{
					_type = RendererType::OpenGL;
				}
				else if (s_rendererCreator[RendererType::OpenGLES].supported)
				{
					_type = RendererType::OpenGLES;
				}
			}
			else if (BX_ENABLED(0
				 ||  BX_PLATFORM_ANDROID
				 ||  BX_PLATFORM_EMSCRIPTEN
				 ||  BX_PLATFORM_IOS
				 ||  BX_PLATFORM_NACL
				 ||  BX_PLATFORM_RPI
				 ) )
			{
				_type = RendererType::OpenGLES;
			}
			else
			{
				_type = RendererType::OpenGL;
			}

			if (!s_rendererCreator[_type].supported)
			{
				_type = RendererType::Null;
			}
		}

		RendererContextI* renderCtx = s_rendererCreator[_type].createFn();

		if (NULL == renderCtx)
		{
			goto again;
		}

		s_rendererDestroyFn = s_rendererCreator[_type].destroyFn;

		return renderCtx;
	}

	void rendererDestroy()
	{
		s_rendererDestroyFn();
	}

	void Context::rendererExecCommands(CommandBuffer& _cmdbuf)
	{
		_cmdbuf.reset();

		bool end = false;

		do
		{
			uint8_t command;
			_cmdbuf.read(command);

			switch (command)
			{
			case CommandBuffer::RendererInit:
				{
					BX_CHECK(!m_rendererInitialized, "This shouldn't happen! Bad synchronization?");

					RendererType::Enum type;
					_cmdbuf.read(type);

					m_renderCtx = rendererCreate(type);
					m_rendererInitialized = true;
				}
				break;

			case CommandBuffer::RendererShutdownBegin:
				{
					BX_CHECK(m_rendererInitialized, "This shouldn't happen! Bad synchronization?");
					m_rendererInitialized = false;
				}
				break;

			case CommandBuffer::RendererShutdownEnd:
				{
					BX_CHECK(!m_rendererInitialized && !m_exit, "This shouldn't happen! Bad synchronization?");
					rendererDestroy();
					m_renderCtx = NULL;
					m_exit = true;
				}
				break;

			case CommandBuffer::CreateIndexBuffer:
				{
					IndexBufferHandle handle;
					_cmdbuf.read(handle);

					Memory* mem;
					_cmdbuf.read(mem);

					m_renderCtx->createIndexBuffer(handle, mem);

					release(mem);
				}
				break;

			case CommandBuffer::DestroyIndexBuffer:
				{
					IndexBufferHandle handle;
					_cmdbuf.read(handle);

					m_renderCtx->destroyIndexBuffer(handle);
				}
				break;

			case CommandBuffer::CreateVertexDecl:
				{
					VertexDeclHandle handle;
					_cmdbuf.read(handle);

					VertexDecl decl;
					_cmdbuf.read(decl);

					m_renderCtx->createVertexDecl(handle, decl);
				}
				break;

			case CommandBuffer::DestroyVertexDecl:
				{
					VertexDeclHandle handle;
					_cmdbuf.read(handle);

					m_renderCtx->destroyVertexDecl(handle);
				}
				break;

			case CommandBuffer::CreateVertexBuffer:
				{
					VertexBufferHandle handle;
					_cmdbuf.read(handle);

					Memory* mem;
					_cmdbuf.read(mem);

					VertexDeclHandle declHandle;
					_cmdbuf.read(declHandle);

					m_renderCtx->createVertexBuffer(handle, mem, declHandle);

					release(mem);
				}
				break;

			case CommandBuffer::DestroyVertexBuffer:
				{
					VertexBufferHandle handle;
					_cmdbuf.read(handle);

					m_renderCtx->destroyVertexBuffer(handle);
				}
				break;

			case CommandBuffer::CreateDynamicIndexBuffer:
				{
					IndexBufferHandle handle;
					_cmdbuf.read(handle);

					uint32_t size;
					_cmdbuf.read(size);

					m_renderCtx->createDynamicIndexBuffer(handle, size);
				}
				break;

			case CommandBuffer::UpdateDynamicIndexBuffer:
				{
					IndexBufferHandle handle;
					_cmdbuf.read(handle);

					uint32_t offset;
					_cmdbuf.read(offset);

					uint32_t size;
					_cmdbuf.read(size);

					Memory* mem;
					_cmdbuf.read(mem);

					m_renderCtx->updateDynamicIndexBuffer(handle, offset, size, mem);

					release(mem);
				}
				break;

			case CommandBuffer::DestroyDynamicIndexBuffer:
				{
					IndexBufferHandle handle;
					_cmdbuf.read(handle);

					m_renderCtx->destroyDynamicIndexBuffer(handle);
				}
				break;

			case CommandBuffer::CreateDynamicVertexBuffer:
				{
					VertexBufferHandle handle;
					_cmdbuf.read(handle);

					uint32_t size;
					_cmdbuf.read(size);

					m_renderCtx->createDynamicVertexBuffer(handle, size);
				}
				break;

			case CommandBuffer::UpdateDynamicVertexBuffer:
				{
					VertexBufferHandle handle;
					_cmdbuf.read(handle);

					uint32_t offset;
					_cmdbuf.read(offset);

					uint32_t size;
					_cmdbuf.read(size);

					Memory* mem;
					_cmdbuf.read(mem);

					m_renderCtx->updateDynamicVertexBuffer(handle, offset, size, mem);

					release(mem);
				}
				break;

			case CommandBuffer::DestroyDynamicVertexBuffer:
				{
					VertexBufferHandle handle;
					_cmdbuf.read(handle);

					m_renderCtx->destroyDynamicVertexBuffer(handle);
				}
				break;

			case CommandBuffer::CreateShader:
				{
					ShaderHandle handle;
					_cmdbuf.read(handle);

					Memory* mem;
					_cmdbuf.read(mem);

					m_renderCtx->createShader(handle, mem);

					release(mem);
				}
				break;

			case CommandBuffer::DestroyShader:
				{
					ShaderHandle handle;
					_cmdbuf.read(handle);

					m_renderCtx->destroyShader(handle);
				}
				break;

			case CommandBuffer::CreateProgram:
				{
					ProgramHandle handle;
					_cmdbuf.read(handle);

					ShaderHandle vsh;
					_cmdbuf.read(vsh);

					ShaderHandle fsh;
					_cmdbuf.read(fsh);

					m_renderCtx->createProgram(handle, vsh, fsh);
				}
				break;

			case CommandBuffer::DestroyProgram:
				{
					ProgramHandle handle;
					_cmdbuf.read(handle);

					m_renderCtx->destroyProgram(handle);
				}
				break;

			case CommandBuffer::CreateTexture:
				{
					TextureHandle handle;
					_cmdbuf.read(handle);

					Memory* mem;
					_cmdbuf.read(mem);

					uint32_t flags;
					_cmdbuf.read(flags);

					uint8_t skip;
					_cmdbuf.read(skip);

					m_renderCtx->createTexture(handle, mem, flags, skip);

					bx::MemoryReader reader(mem->data, mem->size);

					uint32_t magic;
					bx::read(&reader, magic);

					if (BGFX_CHUNK_MAGIC_TEX == magic)
					{
						TextureCreate tc;
						bx::read(&reader, tc);

						if (NULL != tc.m_mem)
						{
							release(tc.m_mem);
						}
					}

					release(mem);
				}
				break;

			case CommandBuffer::UpdateTexture:
				{
					if (m_textureUpdateBatch.isFull() )
					{
						flushTextureUpdateBatch(_cmdbuf);
					}

					uint32_t value = _cmdbuf.m_pos;

					TextureHandle handle;
					_cmdbuf.read(handle);

					uint8_t side;
					_cmdbuf.read(side);

					uint8_t mip;
					_cmdbuf.read(mip);

					_cmdbuf.skip<Rect>();
					_cmdbuf.skip<uint16_t>();
					_cmdbuf.skip<uint16_t>();
					_cmdbuf.skip<uint16_t>();
					_cmdbuf.skip<Memory*>();

					uint32_t key = (handle.idx<<16)
						| (side<<8)
						| mip
						;

					m_textureUpdateBatch.add(key, value);
				}
				break;

			case CommandBuffer::DestroyTexture:
				{
					TextureHandle handle;
					_cmdbuf.read(handle);

					m_renderCtx->destroyTexture(handle);
				}
				break;

			case CommandBuffer::CreateFrameBuffer:
				{
					FrameBufferHandle handle;
					_cmdbuf.read(handle);

					bool window;
					_cmdbuf.read(window);

					if (window)
					{
						void* nwh;
						_cmdbuf.read(nwh);

						uint16_t width;
						_cmdbuf.read(width);

						uint16_t height;
						_cmdbuf.read(height);

						TextureFormat::Enum depthFormat;
						_cmdbuf.read(depthFormat);

						m_renderCtx->createFrameBuffer(handle, nwh, width, height, depthFormat);
					}
					else
					{
						uint8_t num;
						_cmdbuf.read(num);

						TextureHandle textureHandles[BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS];
						for (uint32_t ii = 0; ii < num; ++ii)
						{
							_cmdbuf.read(textureHandles[ii]);
						}

						m_renderCtx->createFrameBuffer(handle, num, textureHandles);
					}
				}
				break;

			case CommandBuffer::DestroyFrameBuffer:
				{
					FrameBufferHandle handle;
					_cmdbuf.read(handle);

					m_renderCtx->destroyFrameBuffer(handle);
				}
				break;

			case CommandBuffer::CreateUniform:
				{
					UniformHandle handle;
					_cmdbuf.read(handle);

					UniformType::Enum type;
					_cmdbuf.read(type);

					uint16_t num;
					_cmdbuf.read(num);

					uint8_t len;
					_cmdbuf.read(len);

					const char* name = (const char*)_cmdbuf.skip(len);

					m_renderCtx->createUniform(handle, type, num, name);
				}
				break;

			case CommandBuffer::DestroyUniform:
				{
					UniformHandle handle;
					_cmdbuf.read(handle);

					m_renderCtx->destroyUniform(handle);
				}
				break;

			case CommandBuffer::SaveScreenShot:
				{
					uint16_t len;
					_cmdbuf.read(len);

					const char* filePath = (const char*)_cmdbuf.skip(len);

					m_renderCtx->saveScreenShot(filePath);
				}
				break;

			case CommandBuffer::UpdateViewName:
				{
					uint8_t id;
					_cmdbuf.read(id);

					uint16_t len;
					_cmdbuf.read(len);

					const char* name = (const char*)_cmdbuf.skip(len);

					m_renderCtx->updateViewName(id, name);
				}
				break;

			case CommandBuffer::End:
				end = true;
				break;

			default:
				BX_CHECK(false, "Invalid command: %d", command);
				break;
			}
		} while (!end);

		flushTextureUpdateBatch(_cmdbuf);
	}

	uint8_t getSupportedRenderers(RendererType::Enum _enum[RendererType::Count])
	{
		uint8_t num = 0;
		for (uint8_t ii = 0; ii < uint8_t(RendererType::Count); ++ii)
		{
			if (s_rendererCreator[ii].supported)
			{
				_enum[num++] = RendererType::Enum(ii);
			}
		}

		return num;
	}

	const char* getRendererName(RendererType::Enum _type)
	{
		BX_CHECK(_type < RendererType::Count, "Invalid renderer type %d.", _type);
		return s_rendererCreator[_type].name;
	}

	void init(RendererType::Enum _type, CallbackI* _callback, bx::ReallocatorI* _allocator)
	{
		BX_CHECK(NULL == s_ctx, "bgfx is already initialized.");
		BX_TRACE("Init...");

		memset(&g_caps, 0, sizeof(g_caps) );
		g_caps.supported = 0
			| (BGFX_CONFIG_MULTITHREADED ? BGFX_CAPS_RENDERER_MULTITHREADED : 0)
			;
		g_caps.maxDrawCalls = BGFX_CONFIG_MAX_DRAW_CALLS;
		g_caps.maxFBAttachments = 1;

		if (NULL != _allocator)
		{
			g_allocator = _allocator;
		}
		else
		{
			bx::CrtAllocator allocator;
			g_allocator =
				s_allocatorStub = BX_NEW(&allocator, AllocatorStub);
		}

		if (NULL != _callback)
		{
			g_callback = _callback;
		}
		else
		{
			g_callback =
				s_callbackStub = BX_NEW(g_allocator, CallbackStub);
		}

		s_threadIndex = BGFX_MAIN_THREAD_MAGIC;

		s_ctx = BX_ALIGNED_NEW(g_allocator, Context, 16);
		s_ctx->init(_type);

		BX_TRACE("Init complete.");
	}

	void shutdown()
	{
		BX_TRACE("Shutdown...");

		BGFX_CHECK_MAIN_THREAD();
		Context* ctx = s_ctx; // it's going to be NULLd inside shutdown.
		ctx->shutdown();

		BX_ALIGNED_DELETE(g_allocator, ctx, 16);

		if (NULL != s_callbackStub)
		{
			BX_DELETE(g_allocator, s_callbackStub);
			s_callbackStub = NULL;
		}

		if (NULL != s_allocatorStub)
		{
			s_allocatorStub->checkLeaks();

			bx::CrtAllocator allocator;
			BX_DELETE(&allocator, s_allocatorStub);
			s_allocatorStub = NULL;
		}

		s_threadIndex = 0;
		g_callback = NULL;
		g_allocator = NULL;

		BX_TRACE("Shutdown complete.");
	}

	void reset(uint32_t _width, uint32_t _height, uint32_t _flags)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->reset(_width, _height, _flags);
	}

	uint32_t frame()
	{
		BGFX_CHECK_MAIN_THREAD();
		return s_ctx->frame();
	}

	const Caps* getCaps()
	{
		return &g_caps;
	}

	RendererType::Enum getRendererType()
	{
		return g_caps.rendererType;
	}

	const Memory* alloc(uint32_t _size)
	{
		Memory* mem = (Memory*)BX_ALLOC(g_allocator, sizeof(Memory) + _size);
		mem->size = _size;
		mem->data = (uint8_t*)mem + sizeof(Memory);
		return mem;
	}

	const Memory* copy(const void* _data, uint32_t _size)
	{
		const Memory* mem = alloc(_size);
		memcpy(mem->data, _data, _size);
		return mem;
	}

	const Memory* makeRef(const void* _data, uint32_t _size)
	{
		Memory* mem = (Memory*)BX_ALLOC(g_allocator, sizeof(Memory) );
		mem->size = _size;
		mem->data = (uint8_t*)_data;
		return mem;
	}

	void release(const Memory* _mem)
	{
		BX_CHECK(NULL != _mem, "_mem can't be NULL");
		BX_FREE(g_allocator, const_cast<Memory*>(_mem) );
	}

	void setDebug(uint32_t _debug)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setDebug(_debug);
	}

	void dbgTextClear(uint8_t _attr, bool _small)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->dbgTextClear(_attr, _small);
	}

	void dbgTextPrintfVargs(uint16_t _x, uint16_t _y, uint8_t _attr, const char* _format, va_list _argList)
	{
		s_ctx->dbgTextPrintfVargs(_x, _y, _attr, _format, _argList);
	}

	void dbgTextPrintf(uint16_t _x, uint16_t _y, uint8_t _attr, const char* _format, ...)
	{
		BGFX_CHECK_MAIN_THREAD();
		va_list argList;
		va_start(argList, _format);
		s_ctx->dbgTextPrintfVargs(_x, _y, _attr, _format, argList);
		va_end(argList);
	}

	IndexBufferHandle createIndexBuffer(const Memory* _mem)
	{
		BGFX_CHECK_MAIN_THREAD();
		return s_ctx->createIndexBuffer(_mem);
	}

	void destroyIndexBuffer(IndexBufferHandle _handle)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->destroyIndexBuffer(_handle);
	}

	VertexBufferHandle createVertexBuffer(const Memory* _mem, const VertexDecl& _decl)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(0 != _decl.m_stride, "Invalid VertexDecl.");
		return s_ctx->createVertexBuffer(_mem, _decl);
	}

	void destroyVertexBuffer(VertexBufferHandle _handle)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->destroyVertexBuffer(_handle);
	}

	DynamicIndexBufferHandle createDynamicIndexBuffer(uint32_t _num)
	{
		BGFX_CHECK_MAIN_THREAD();
		return s_ctx->createDynamicIndexBuffer(_num);
	}

	DynamicIndexBufferHandle createDynamicIndexBuffer(const Memory* _mem)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _mem, "_mem can't be NULL");
		return s_ctx->createDynamicIndexBuffer(_mem);
	}

	void updateDynamicIndexBuffer(DynamicIndexBufferHandle _handle, const Memory* _mem)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _mem, "_mem can't be NULL");
		s_ctx->updateDynamicIndexBuffer(_handle, _mem);
	}

	void destroyDynamicIndexBuffer(DynamicIndexBufferHandle _handle)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->destroyDynamicIndexBuffer(_handle);
	}

	DynamicVertexBufferHandle createDynamicVertexBuffer(uint16_t _num, const VertexDecl& _decl)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(0 != _decl.m_stride, "Invalid VertexDecl.");
		return s_ctx->createDynamicVertexBuffer(_num, _decl);
	}

	DynamicVertexBufferHandle createDynamicVertexBuffer(const Memory* _mem, const VertexDecl& _decl)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _mem, "_mem can't be NULL");
		BX_CHECK(0 != _decl.m_stride, "Invalid VertexDecl.");
		return s_ctx->createDynamicVertexBuffer(_mem, _decl);
	}

	void updateDynamicVertexBuffer(DynamicVertexBufferHandle _handle, const Memory* _mem)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _mem, "_mem can't be NULL");
		s_ctx->updateDynamicVertexBuffer(_handle, _mem);
	}

	void destroyDynamicVertexBuffer(DynamicVertexBufferHandle _handle)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->destroyDynamicVertexBuffer(_handle);
	}

	bool checkAvailTransientIndexBuffer(uint32_t _num)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(0 < _num, "Requesting 0 indices.");
		return s_ctx->checkAvailTransientIndexBuffer(_num);
	}

	bool checkAvailTransientVertexBuffer(uint32_t _num, const VertexDecl& _decl)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(0 < _num, "Requesting 0 vertices.");
		BX_CHECK(0 != _decl.m_stride, "Invalid VertexDecl.");
		return s_ctx->checkAvailTransientVertexBuffer(_num, _decl.m_stride);
	}

	bool checkAvailInstanceDataBuffer(uint32_t _num, uint16_t _stride)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(0 < _num, "Requesting 0 instances.");
		return s_ctx->checkAvailTransientVertexBuffer(_num, _stride);
	}

	bool checkAvailTransientBuffers(uint32_t _numVertices, const VertexDecl& _decl, uint32_t _numIndices)
	{
		BX_CHECK(0 != _decl.m_stride, "Invalid VertexDecl.");
		return checkAvailTransientVertexBuffer(_numVertices, _decl)
			&& checkAvailTransientIndexBuffer(_numIndices)
			;
	}

	void allocTransientIndexBuffer(TransientIndexBuffer* _tib, uint32_t _num)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _tib, "_tib can't be NULL");
		BX_CHECK(0 < _num, "Requesting 0 indices.");
		return s_ctx->allocTransientIndexBuffer(_tib, _num);
	}

	void allocTransientVertexBuffer(TransientVertexBuffer* _tvb, uint32_t _num, const VertexDecl& _decl)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _tvb, "_tvb can't be NULL");
		BX_CHECK(0 < _num, "Requesting 0 vertices.");
		BX_CHECK(UINT16_MAX >= _num, "Requesting %d vertices (max: %d).", _num, UINT16_MAX);
		BX_CHECK(0 != _decl.m_stride, "Invalid VertexDecl.");
		return s_ctx->allocTransientVertexBuffer(_tvb, _num, _decl);
	}

	bool allocTransientBuffers(bgfx::TransientVertexBuffer* _tvb, const bgfx::VertexDecl& _decl, uint16_t _numVertices, bgfx::TransientIndexBuffer* _tib, uint16_t _numIndices)
	{
		if (checkAvailTransientBuffers(_numVertices, _decl, _numIndices) )
		{
			allocTransientVertexBuffer(_tvb, _numVertices, _decl);
			allocTransientIndexBuffer(_tib, _numIndices);
			return true;
		}

		return false;
	}

	const InstanceDataBuffer* allocInstanceDataBuffer(uint32_t _num, uint16_t _stride)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(0 != (g_caps.supported & BGFX_CAPS_INSTANCING), "Instancing is not supported! Use bgfx::getCaps to check backend renderer capabilities.");
		BX_CHECK(0 < _num, "Requesting 0 instanced data vertices.");
		return s_ctx->allocInstanceDataBuffer(_num, _stride);
	}

	ShaderHandle createShader(const Memory* _mem)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _mem, "_mem can't be NULL");
		return s_ctx->createShader(_mem);
	}

	uint16_t getShaderUniforms(ShaderHandle _handle, UniformHandle* _uniforms, uint16_t _max)
	{
		BGFX_CHECK_MAIN_THREAD();
		return s_ctx->getShaderUniforms(_handle, _uniforms, _max);
	}

	void destroyShader(ShaderHandle _handle)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->destroyShader(_handle);
	}

	ProgramHandle createProgram(ShaderHandle _vsh, ShaderHandle _fsh, bool _destroyShaders)
	{
		BGFX_CHECK_MAIN_THREAD();
		ProgramHandle handle = s_ctx->createProgram(_vsh, _fsh);

		if (_destroyShaders)
		{
			destroyShader(_vsh);
			destroyShader(_fsh);
		}

		return handle;
	}

	ProgramHandle createProgram(ShaderHandle _vsh, bool _destroyShaders)
	{
		BGFX_CHECK_MAIN_THREAD();
		ProgramHandle handle = s_ctx->createProgram(_vsh);

		if (_destroyShaders)
		{
			destroyShader(_vsh);
		}

		return handle;
	}

	void destroyProgram(ProgramHandle _handle)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->destroyProgram(_handle);
	}

	void calcTextureSize(TextureInfo& _info, uint16_t _width, uint16_t _height, uint16_t _depth, uint8_t _numMips, TextureFormat::Enum _format)
	{
		_width   = bx::uint32_max(1, _width);
		_height  = bx::uint32_max(1, _height);
		_depth   = bx::uint32_max(1, _depth);

		uint32_t width  = _width;
		uint32_t height = _height;
		uint32_t depth  = _depth;

		uint32_t bpp = getBitsPerPixel(_format);
		uint32_t size = 0;

		for (uint32_t lod = 0; lod < _numMips; ++lod)
		{
			width  = bx::uint32_max(1, width);
			height = bx::uint32_max(1, height);
			depth  = bx::uint32_max(1, depth);

			size += width*height*depth*bpp/8;

			width >>= 1;
			height >>= 1;
			depth >>= 1;
		}

		_info.format = _format;
		_info.storageSize = size;
		_info.width = _width;
		_info.height = _height;
		_info.depth = _depth;
		_info.numMips = _numMips;
		_info.bitsPerPixel = bpp;
	}

	TextureHandle createTexture(const Memory* _mem, uint32_t _flags, uint8_t _skip, TextureInfo* _info)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _mem, "_mem can't be NULL");
		return s_ctx->createTexture(_mem, _flags, _skip, _info);
	}

	TextureHandle createTexture2D(uint16_t _width, uint16_t _height, uint8_t _numMips, TextureFormat::Enum _format, uint32_t _flags, const Memory* _mem)
	{
		BGFX_CHECK_MAIN_THREAD();

		_numMips = bx::uint32_max(1, _numMips);

		if (BX_ENABLED(BGFX_CONFIG_DEBUG)
		&&  NULL != _mem)
		{
			TextureInfo ti;
			calcTextureSize(ti, _width, _height, 1, _numMips, _format);
			BX_CHECK(ti.storageSize == _mem->size
				, "createTexture2D: Texture storage size doesn't match passed memory size (storage size: %d, memory size: %d)"
				, ti.storageSize
				, _mem->size
				);
		}

		uint32_t size = sizeof(uint32_t)+sizeof(TextureCreate);
		const Memory* mem = alloc(size);

		bx::StaticMemoryBlockWriter writer(mem->data, mem->size);
		uint32_t magic = BGFX_CHUNK_MAGIC_TEX;
		bx::write(&writer, magic);

		TextureCreate tc;
		tc.m_flags = _flags;
		tc.m_width = _width;
		tc.m_height = _height;
		tc.m_sides = 0;
		tc.m_depth = 0;
		tc.m_numMips = _numMips;
		tc.m_format = uint8_t(_format);
		tc.m_cubeMap = false;
		tc.m_mem = _mem;
		bx::write(&writer, tc);

		return s_ctx->createTexture(mem, _flags, 0, NULL);
	}

	TextureHandle createTexture3D(uint16_t _width, uint16_t _height, uint16_t _depth, uint8_t _numMips, TextureFormat::Enum _format, uint32_t _flags, const Memory* _mem)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(0 != (g_caps.supported & BGFX_CAPS_TEXTURE_3D), "Texture3D is not supported! Use bgfx::getCaps to check backend renderer capabilities.");

		_numMips = bx::uint32_max(1, _numMips);

		if (BX_ENABLED(BGFX_CONFIG_DEBUG)
		&&  NULL != _mem)
		{
			TextureInfo ti;
			calcTextureSize(ti, _width, _height, _depth, _numMips, _format);
			BX_CHECK(ti.storageSize == _mem->size
				, "createTexture3D: Texture storage size doesn't match passed memory size (storage size: %d, memory size: %d)"
				, ti.storageSize
				, _mem->size
				);
		}

		uint32_t size = sizeof(uint32_t)+sizeof(TextureCreate);
		const Memory* mem = alloc(size);

		bx::StaticMemoryBlockWriter writer(mem->data, mem->size);
		uint32_t magic = BGFX_CHUNK_MAGIC_TEX;
		bx::write(&writer, magic);

		TextureCreate tc;
		tc.m_flags = _flags;
		tc.m_width = _width;
		tc.m_height = _height;
		tc.m_sides = 0;
		tc.m_depth = _depth;
		tc.m_numMips = _numMips;
		tc.m_format = uint8_t(_format);
		tc.m_cubeMap = false;
		tc.m_mem = _mem;
		bx::write(&writer, tc);

		return s_ctx->createTexture(mem, _flags, 0, NULL);
	}

	TextureHandle createTextureCube(uint16_t _size, uint8_t _numMips, TextureFormat::Enum _format, uint32_t _flags, const Memory* _mem)
	{
		BGFX_CHECK_MAIN_THREAD();

		_numMips = bx::uint32_max(1, _numMips);

		if (BX_ENABLED(BGFX_CONFIG_DEBUG)
		&&  NULL != _mem)
		{
			TextureInfo ti;
			calcTextureSize(ti, _size, _size, 1, _numMips, _format);
			BX_CHECK(ti.storageSize*6 == _mem->size
				, "createTextureCube: Texture storage size doesn't match passed memory size (storage size: %d, memory size: %d)"
				, ti.storageSize*6
				, _mem->size
				);
		}

		uint32_t size = sizeof(uint32_t)+sizeof(TextureCreate);
		const Memory* mem = alloc(size);

		bx::StaticMemoryBlockWriter writer(mem->data, mem->size);
		uint32_t magic = BGFX_CHUNK_MAGIC_TEX;
		bx::write(&writer, magic);

		TextureCreate tc;
		tc.m_flags = _flags;
		tc.m_width = _size;
		tc.m_height = _size;
		tc.m_sides = 6;
		tc.m_depth = 0;
		tc.m_numMips = _numMips;
		tc.m_format = uint8_t(_format);
		tc.m_cubeMap = true;
		tc.m_mem = _mem;
		bx::write(&writer, tc);

		return s_ctx->createTexture(mem, _flags, 0, NULL);
	}

	void destroyTexture(TextureHandle _handle)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->destroyTexture(_handle);
	}

	void updateTexture2D(TextureHandle _handle, uint8_t _mip, uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height, const Memory* _mem, uint16_t _pitch)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _mem, "_mem can't be NULL");
		if (_width == 0
		||  _height == 0)
		{
			release(_mem);
		}
		else
		{
			s_ctx->updateTexture(_handle, 0, _mip, _x, _y, 0, _width, _height, 1, _pitch, _mem);
		}
	}

	void updateTexture3D(TextureHandle _handle, uint8_t _mip, uint16_t _x, uint16_t _y, uint16_t _z, uint16_t _width, uint16_t _height, uint16_t _depth, const Memory* _mem)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _mem, "_mem can't be NULL");
		if (_width == 0
		||  _height == 0
		||  _depth == 0)
		{
			release(_mem);
		}
		else
		{
			s_ctx->updateTexture(_handle, 0, _mip, _x, _y, _z, _width, _height, _depth, UINT16_MAX, _mem);
		}
	}

	void updateTextureCube(TextureHandle _handle, uint8_t _side, uint8_t _mip, uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height, const Memory* _mem, uint16_t _pitch)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _mem, "_mem can't be NULL");
		BX_CHECK(_side <= 5, "Invalid side %d.", _side);
		if (_width == 0
		||  _height == 0)
		{
			release(_mem);
		}
		else
		{
			s_ctx->updateTexture(_handle, _side, _mip, _x, _y, 0, _width, _height, 1, _pitch, _mem);
		}
	}

	FrameBufferHandle createFrameBuffer(uint16_t _width, uint16_t _height, TextureFormat::Enum _format, uint32_t _textureFlags)
	{
		_textureFlags |= _textureFlags&BGFX_TEXTURE_RT_MSAA_MASK ? 0 : BGFX_TEXTURE_RT;
		TextureHandle th = createTexture2D(_width, _height, 1, _format, _textureFlags);
		return createFrameBuffer(1, &th, true);
	}

	FrameBufferHandle createFrameBuffer(uint8_t _num, TextureHandle* _handles, bool _destroyTextures)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _handles, "_handles can't be NULL");
		FrameBufferHandle handle = s_ctx->createFrameBuffer(_num, _handles);
		if (_destroyTextures)
		{
			for (uint32_t ii = 0; ii < _num; ++ii)
			{
				destroyTexture(_handles[ii]);
			}
		}

		return handle;
	}

	FrameBufferHandle createFrameBuffer(void* _nwh, uint16_t _width, uint16_t _height, TextureFormat::Enum _depthFormat)
	{
		BGFX_CHECK_MAIN_THREAD();
		return s_ctx->createFrameBuffer(_nwh, _width, _height, _depthFormat);
	}

	void destroyFrameBuffer(FrameBufferHandle _handle)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->destroyFrameBuffer(_handle);
	}

	UniformHandle createUniform(const char* _name, UniformType::Enum _type, uint16_t _num)
	{
		BGFX_CHECK_MAIN_THREAD();
		return s_ctx->createUniform(_name, _type, _num);
	}

	void destroyUniform(UniformHandle _handle)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->destroyUniform(_handle);
	}

	void setClearColor(uint8_t _index, uint32_t _rgba)
	{
		BGFX_CHECK_MAIN_THREAD();

		const uint8_t rr = _rgba>>24;
		const uint8_t gg = _rgba>>16;
		const uint8_t bb = _rgba>> 8;
		const uint8_t aa = _rgba>> 0;

		float rgba[4] =
		{
			rr * 1.0f/255.0f,
			gg * 1.0f/255.0f,
			bb * 1.0f/255.0f,
			aa * 1.0f/255.0f,
		};
		s_ctx->setClearColor(_index, rgba);
	}

	void setClearColor(uint8_t _index, float _r, float _g, float _b, float _a)
	{
		BGFX_CHECK_MAIN_THREAD();
		float rgba[4] = { _r, _g, _b, _a };
		s_ctx->setClearColor(_index, rgba);
	}

	void setClearColor(uint8_t _index, const float _rgba[4])
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setClearColor(_index, _rgba);
	}

	void setViewName(uint8_t _id, const char* _name)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setViewName(_id, _name);
	}

	void setViewRect(uint8_t _id, uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setViewRect(_id, _x, _y, _width, _height);
	}

	void setViewScissor(uint8_t _id, uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setViewScissor(_id, _x, _y, _width, _height);
	}

	void setViewClear(uint8_t _id, uint8_t _flags, uint32_t _rgba, float _depth, uint8_t _stencil)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setViewClear(_id, _flags, _rgba, _depth, _stencil);
	}

	void setViewClear(uint8_t _id, uint8_t _flags, float _depth, uint8_t _stencil, uint8_t _0, uint8_t _1, uint8_t _2, uint8_t _3, uint8_t _4, uint8_t _5, uint8_t _6, uint8_t _7)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setViewClear(_id, _flags, _depth, _stencil, _0, _1, _2, _3, _4, _5, _6, _7);
	}

	void setViewSeq(uint8_t _id, bool _enabled)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setViewSeq(_id, _enabled);
	}

	void setViewFrameBuffer(uint8_t _id, FrameBufferHandle _handle)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setViewFrameBuffer(_id, _handle);
	}

	void setViewTransform(uint8_t _id, const void* _view, const void* _proj)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setViewTransform(_id, _view, _proj);
	}

	void setMarker(const char* _marker)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setMarker(_marker);
	}

	void setState(uint64_t _state, uint32_t _rgba)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setState(_state, _rgba);
	}

	void setStencil(uint32_t _fstencil, uint32_t _bstencil)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setStencil(_fstencil, _bstencil);
	}

	uint16_t setScissor(uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height)
	{
		BGFX_CHECK_MAIN_THREAD();
		return s_ctx->setScissor(_x, _y, _width, _height);
	}

	void setScissor(uint16_t _cache)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setScissor(_cache);
	}

	uint32_t setTransform(const void* _mtx, uint16_t _num)
	{
		BGFX_CHECK_MAIN_THREAD();
		return s_ctx->setTransform(_mtx, _num);
	}

	uint32_t allocTransform(Transform* _transform, uint16_t _num)
	{
		BGFX_CHECK_MAIN_THREAD();
		return s_ctx->allocTransform(_transform, _num);
	}

	void setTransform(uint32_t _cache, uint16_t _num)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setTransform(_cache, _num);
	}

	void setUniform(UniformHandle _handle, const void* _value, uint16_t _num)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setUniform(_handle, _value, _num);
	}

	void setIndexBuffer(IndexBufferHandle _handle, uint32_t _firstIndex, uint32_t _numIndices)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setIndexBuffer(_handle, _firstIndex, _numIndices);
	}

	void setIndexBuffer(DynamicIndexBufferHandle _handle, uint32_t _firstIndex, uint32_t _numIndices)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setIndexBuffer(_handle, _firstIndex, _numIndices);
	}

	void setIndexBuffer(const TransientIndexBuffer* _tib)
	{
		setIndexBuffer(_tib, 0, UINT32_MAX);
	}

	void setIndexBuffer(const TransientIndexBuffer* _tib, uint32_t _firstIndex, uint32_t _numIndices)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _tib, "_tib can't be NULL");
		uint32_t numIndices = bx::uint32_min(_numIndices, _tib->size/2);
		s_ctx->setIndexBuffer(_tib, _tib->startIndex + _firstIndex, numIndices);
	}

	void setVertexBuffer(VertexBufferHandle _handle)
	{
		setVertexBuffer(_handle, 0, UINT32_MAX);
	}

	void setVertexBuffer(VertexBufferHandle _handle, uint32_t _startVertex, uint32_t _numVertices)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setVertexBuffer(_handle, _startVertex, _numVertices);
	}

	void setVertexBuffer(DynamicVertexBufferHandle _handle, uint32_t _numVertices)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setVertexBuffer(_handle, _numVertices);
	}

	void setVertexBuffer(const TransientVertexBuffer* _tvb)
	{
		setVertexBuffer(_tvb, 0, UINT32_MAX);
	}

	void setVertexBuffer(const TransientVertexBuffer* _tvb, uint32_t _startVertex, uint32_t _numVertices)
	{
		BGFX_CHECK_MAIN_THREAD();
		BX_CHECK(NULL != _tvb, "_tvb can't be NULL");
		s_ctx->setVertexBuffer(_tvb, _tvb->startVertex + _startVertex, _numVertices);
	}

	void setInstanceDataBuffer(const InstanceDataBuffer* _idb, uint16_t _num)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setInstanceDataBuffer(_idb, _num);
	}

	void setProgram(ProgramHandle _handle)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setProgram(_handle);
	}

	void setTexture(uint8_t _stage, UniformHandle _sampler, TextureHandle _handle, uint32_t _flags)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setTexture(_stage, _sampler, _handle, _flags);
	}

	void setTexture(uint8_t _stage, UniformHandle _sampler, FrameBufferHandle _handle, uint8_t _attachment, uint32_t _flags)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setTexture(_stage, _sampler, _handle, _attachment, _flags);
	}

	uint32_t submit(uint8_t _id, int32_t _depth)
	{
		BGFX_CHECK_MAIN_THREAD();
		return s_ctx->submit(_id, _depth);
	}

	void setImage(uint8_t _stage, UniformHandle _sampler, TextureHandle _handle, uint8_t _mip, TextureFormat::Enum _format, Access::Enum _access)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setImage(_stage, _sampler, _handle, _mip, _format, _access);
	}

	void setImage(uint8_t _stage, UniformHandle _sampler, FrameBufferHandle _handle, uint8_t _attachment, TextureFormat::Enum _format, Access::Enum _access)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->setImage(_stage, _sampler, _handle, _attachment, _format, _access);
	}

	void dispatch(uint8_t _id, ProgramHandle _handle, uint16_t _numX, uint16_t _numY, uint16_t _numZ)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->dispatch(_id, _handle, _numX, _numY, _numZ);
	}

	void discard()
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->discard();
	}

	void saveScreenShot(const char* _filePath)
	{
		BGFX_CHECK_MAIN_THREAD();
		s_ctx->saveScreenShot(_filePath);
	}
} // namespace bgfx

#include <bgfx.c99.h>
#include <bgfxplatform.c99.h>

BX_STATIC_ASSERT(bgfx::Fatal::Count         == bgfx::Fatal::Enum(BGFX_FATAL_COUNT) );
BX_STATIC_ASSERT(bgfx::RendererType::Count  == bgfx::RendererType::Enum(BGFX_RENDERER_TYPE_COUNT) );
BX_STATIC_ASSERT(bgfx::Attrib::Count        == bgfx::Attrib::Enum(BGFX_ATTRIB_COUNT) );
BX_STATIC_ASSERT(bgfx::AttribType::Count    == bgfx::AttribType::Enum(BGFX_ATTRIB_TYPE_COUNT) );
BX_STATIC_ASSERT(bgfx::TextureFormat::Count == bgfx::TextureFormat::Enum(BGFX_TEXTURE_FORMAT_COUNT) );
BX_STATIC_ASSERT(bgfx::UniformType::Count   == bgfx::UniformType::Enum(BGFX_UNIFORM_TYPE_COUNT) );
BX_STATIC_ASSERT(bgfx::RenderFrame::Count   == bgfx::RenderFrame::Enum(BGFX_RENDER_FRAME_COUNT) );

BX_STATIC_ASSERT(sizeof(bgfx::Memory)                == sizeof(bgfx_memory_t) );
BX_STATIC_ASSERT(sizeof(bgfx::VertexDecl)            == sizeof(bgfx_vertex_decl_t) );
BX_STATIC_ASSERT(sizeof(bgfx::TransientIndexBuffer)  == sizeof(bgfx_transient_index_buffer_t) );
BX_STATIC_ASSERT(sizeof(bgfx::TransientVertexBuffer) == sizeof(bgfx_transient_vertex_buffer_t) );
BX_STATIC_ASSERT(sizeof(bgfx::InstanceDataBuffer)    == sizeof(bgfx_instance_data_buffer_t) );
BX_STATIC_ASSERT(sizeof(bgfx::TextureInfo)           == sizeof(bgfx_texture_info_t) );
BX_STATIC_ASSERT(sizeof(bgfx::Caps)                  == sizeof(bgfx_caps_t) );

BGFX_C_API void bgfx_vertex_decl_begin(bgfx_vertex_decl_t* _decl, bgfx_renderer_type_t _renderer)
{
	bgfx::VertexDecl* decl = (bgfx::VertexDecl*)_decl;
	decl->begin(bgfx::RendererType::Enum(_renderer) );
}

BGFX_C_API void bgfx_vertex_decl_add(bgfx_vertex_decl_t* _decl, bgfx_attrib_t _attrib, uint8_t _num, bgfx_attrib_type_t _type, bool _normalized, bool _asInt)
{
	bgfx::VertexDecl* decl = (bgfx::VertexDecl*)_decl;
	decl->add(bgfx::Attrib::Enum(_attrib)
		, _num
		, bgfx::AttribType::Enum(_type)
		, _normalized
		, _asInt
		);
}

BGFX_C_API void bgfx_vertex_decl_skip(bgfx_vertex_decl_t* _decl, uint8_t _num)
{
	bgfx::VertexDecl* decl = (bgfx::VertexDecl*)_decl;
	decl->skip(_num);
}

BGFX_C_API void bgfx_vertex_decl_end(bgfx_vertex_decl_t* _decl)
{
	bgfx::VertexDecl* decl = (bgfx::VertexDecl*)_decl;
	decl->end();
}

BGFX_C_API void bgfx_vertex_pack(const float _input[4], bool _inputNormalized, bgfx_attrib_t _attr, const bgfx_vertex_decl_t* _decl, void* _data, uint32_t _index)
{
	bgfx::VertexDecl& decl = *(bgfx::VertexDecl*)_decl;
	bgfx::vertexPack(_input, _inputNormalized, bgfx::Attrib::Enum(_attr), decl, _data, _index);
}

BGFX_C_API void bgfx_vertex_unpack(float _output[4], bgfx_attrib_t _attr, const bgfx_vertex_decl_t* _decl, const void* _data, uint32_t _index)
{
	bgfx::VertexDecl& decl = *(bgfx::VertexDecl*)_decl;
	bgfx::vertexUnpack(_output, bgfx::Attrib::Enum(_attr), decl, _data, _index);
}

BGFX_C_API void bgfx_vertex_convert(const bgfx_vertex_decl_t* _destDecl, void* _destData, const bgfx_vertex_decl_t* _srcDecl, const void* _srcData, uint32_t _num)
{
	bgfx::VertexDecl& destDecl = *(bgfx::VertexDecl*)_destDecl;
	bgfx::VertexDecl& srcDecl  = *(bgfx::VertexDecl*)_srcDecl;
	bgfx::vertexConvert(destDecl, _destData, srcDecl, _srcData, _num);
}

BGFX_C_API uint16_t bgfx_weld_vertices(uint16_t* _output, const bgfx_vertex_decl_t* _decl, const void* _data, uint16_t _num, float _epsilon)
{
	bgfx::VertexDecl& decl = *(bgfx::VertexDecl*)_decl;
	return bgfx::weldVertices(_output, decl, _data, _num, _epsilon);
}

BGFX_C_API void bgfx_image_swizzle_bgra8(uint32_t _width, uint32_t _height, uint32_t _pitch, const void* _src, void* _dst)
{
	bgfx::imageSwizzleBgra8(_width, _height, _pitch, _src, _dst);
}

BGFX_C_API void bgfx_image_rgba8_downsample_2x2(uint32_t _width, uint32_t _height, uint32_t _pitch, const void* _src, void* _dst)
{
	bgfx::imageRgba8Downsample2x2(_width, _height, _pitch, _src, _dst);
}

BGFX_C_API uint8_t bgfx_get_supported_renderers(bgfx_renderer_type_t _enum[BGFX_RENDERER_TYPE_COUNT])
{
	return bgfx::getSupportedRenderers( (bgfx::RendererType::Enum*)_enum);
}

BGFX_C_API const char* bgfx_get_renderer_name(bgfx_renderer_type_t _type)
{
	return bgfx::getRendererName(bgfx::RendererType::Enum(_type) );
}

BGFX_C_API void bgfx_init(bgfx_renderer_type_t _type, struct bgfx_callback_interface* _callback, struct bgfx_reallocator_interface* _allocator)
{
	return bgfx::init(bgfx::RendererType::Enum(_type)
		, reinterpret_cast<bgfx::CallbackI*>(_callback)
		, reinterpret_cast<bx::ReallocatorI*>(_allocator)
		);
}

BGFX_C_API void bgfx_shutdown()
{
	return bgfx::shutdown();
}

BGFX_C_API void bgfx_reset(uint32_t _width, uint32_t _height, uint32_t _flags)
{
	bgfx::reset(_width, _height, _flags);
}

BGFX_C_API uint32_t bgfx_frame()
{
	return bgfx::frame();
}

BGFX_C_API bgfx_renderer_type_t bgfx_get_renderer_type()
{
	return bgfx_renderer_type_t(bgfx::getRendererType() );
}

BGFX_C_API bgfx_caps_t* bgfx_get_caps()
{
	return (bgfx_caps_t*)bgfx::getCaps();
}

BGFX_C_API const bgfx_memory_t* bgfx_alloc(uint32_t _size)
{
	return (const bgfx_memory_t*)bgfx::alloc(_size);
}

BGFX_C_API const bgfx_memory_t* bgfx_copy(const void* _data, uint32_t _size)
{
	return (const bgfx_memory_t*)bgfx::copy(_data, _size);
}

BGFX_C_API const bgfx_memory_t* bgfx_make_ref(const void* _data, uint32_t _size)
{
	return (const bgfx_memory_t*)bgfx::makeRef(_data, _size);
}

BGFX_C_API void bgfx_set_debug(uint32_t _debug)
{
	bgfx::setDebug(_debug);
}

BGFX_C_API void bgfx_dbg_text_clear(uint8_t _attr, bool _small)
{
	bgfx::dbgTextClear(_attr, _small);
}

BGFX_C_API void bgfx_dbg_text_printf(uint16_t _x, uint16_t _y, uint8_t _attr, const char* _format, ...)
{
	va_list argList;
	va_start(argList, _format);
	bgfx::dbgTextPrintfVargs(_x, _y, _attr, _format, argList);
	va_end(argList);
}

BGFX_C_API bgfx_index_buffer_handle_t bgfx_create_index_buffer(const bgfx_memory_t* _mem)
{
	union { bgfx_index_buffer_handle_t c; bgfx::IndexBufferHandle cpp; } handle;
	handle.cpp = bgfx::createIndexBuffer( (const bgfx::Memory*)_mem);
	return handle.c;
}

BGFX_C_API void bgfx_destroy_index_buffer(bgfx_index_buffer_handle_t _handle)
{
	union { bgfx_index_buffer_handle_t c; bgfx::IndexBufferHandle cpp; } handle = { _handle };
	bgfx::destroyIndexBuffer(handle.cpp);
}

BGFX_C_API bgfx_vertex_buffer_handle_t bgfx_create_vertex_buffer(const bgfx_memory_t* _mem, const bgfx_vertex_decl_t* _decl)
{
	const bgfx::VertexDecl& decl = *(const bgfx::VertexDecl*)_decl;
	union { bgfx_vertex_buffer_handle_t c; bgfx::VertexBufferHandle cpp; } handle;
	handle.cpp = bgfx::createVertexBuffer( (const bgfx::Memory*)_mem, decl);
	return handle.c;
}

BGFX_C_API void bgfx_destroy_vertex_buffer(bgfx_vertex_buffer_handle_t _handle)
{
	union { bgfx_vertex_buffer_handle_t c; bgfx::VertexBufferHandle cpp; } handle = { _handle };
	bgfx::destroyVertexBuffer(handle.cpp);
}

BGFX_C_API bgfx_dynamic_index_buffer_handle_t bgfx_create_dynamic_index_buffer(uint32_t _num)
{
	union { bgfx_dynamic_index_buffer_handle_t c; bgfx::DynamicIndexBufferHandle cpp; } handle;
	handle.cpp = bgfx::createDynamicIndexBuffer(_num);
	return handle.c;
}

BGFX_C_API bgfx_dynamic_index_buffer_handle_t bgfx_create_dynamic_index_buffer_mem(const bgfx_memory_t* _mem)
{
	union { bgfx_dynamic_index_buffer_handle_t c; bgfx::DynamicIndexBufferHandle cpp; } handle;
	handle.cpp = bgfx::createDynamicIndexBuffer( (const bgfx::Memory*)_mem);
	return handle.c;
}

BGFX_C_API void bgfx_update_dynamic_index_buffer(bgfx_dynamic_index_buffer_handle_t _handle, const bgfx_memory_t* _mem)
{
	union { bgfx_dynamic_index_buffer_handle_t c; bgfx::DynamicIndexBufferHandle cpp; } handle = { _handle };
	bgfx::updateDynamicIndexBuffer(handle.cpp, (const bgfx::Memory*)_mem);
}

BGFX_C_API void bgfx_destroy_dynamic_index_buffer(bgfx_dynamic_index_buffer_handle_t _handle)
{
	union { bgfx_dynamic_index_buffer_handle_t c; bgfx::DynamicIndexBufferHandle cpp; } handle = { _handle };
	bgfx::destroyDynamicIndexBuffer(handle.cpp);
}

BGFX_C_API bgfx_dynamic_vertex_buffer_handle_t bgfx_create_dynamic_vertex_buffer(uint16_t _num, const bgfx_vertex_decl_t* _decl)
{
	const bgfx::VertexDecl& decl = *(const bgfx::VertexDecl*)_decl;
	union { bgfx_dynamic_vertex_buffer_handle_t c; bgfx::DynamicVertexBufferHandle cpp; } handle;
	handle.cpp = bgfx::createDynamicVertexBuffer(_num, decl);
	return handle.c;
}

BGFX_C_API bgfx_dynamic_vertex_buffer_handle_t bgfx_create_dynamic_vertex_buffer_mem(const bgfx_memory_t* _mem, const bgfx_vertex_decl_t* _decl)
{
	const bgfx::VertexDecl& decl = *(const bgfx::VertexDecl*)_decl;
	union { bgfx_dynamic_vertex_buffer_handle_t c; bgfx::DynamicVertexBufferHandle cpp; } handle;
	handle.cpp = bgfx::createDynamicVertexBuffer( (const bgfx::Memory*)_mem, decl);
	return handle.c;
}

BGFX_C_API void bgfx_update_dynamic_vertex_buffer(bgfx_dynamic_vertex_buffer_handle_t _handle, const bgfx_memory_t* _mem)
{
	union { bgfx_dynamic_vertex_buffer_handle_t c; bgfx::DynamicVertexBufferHandle cpp; } handle = { _handle };
	bgfx::updateDynamicVertexBuffer(handle.cpp, (const bgfx::Memory*)_mem);
}

BGFX_C_API void bgfx_destroy_dynamic_vertex_buffer(bgfx_dynamic_vertex_buffer_handle_t _handle)
{
	union { bgfx_dynamic_vertex_buffer_handle_t c; bgfx::DynamicVertexBufferHandle cpp; } handle = { _handle };
	bgfx::destroyDynamicVertexBuffer(handle.cpp);
}

BGFX_C_API bool bgfx_check_avail_transient_index_buffer(uint32_t _num)
{
	return bgfx::checkAvailTransientIndexBuffer(_num);
}

BGFX_C_API bool bgfx_check_avail_transient_vertex_buffer(uint32_t _num, const bgfx_vertex_decl_t* _decl)
{
	const bgfx::VertexDecl& decl = *(const bgfx::VertexDecl*)_decl;
	return bgfx::checkAvailTransientVertexBuffer(_num, decl);
}

BGFX_C_API bool bgfx_check_avail_instance_data_buffer(uint32_t _num, uint16_t _stride)
{
	return bgfx::checkAvailInstanceDataBuffer(_num, _stride);
}

BGFX_C_API bool bgfx_check_avail_transient_buffers(uint32_t _numVertices, const bgfx_vertex_decl_t* _decl, uint32_t _numIndices)
{
	const bgfx::VertexDecl& decl = *(const bgfx::VertexDecl*)_decl;
	return bgfx::checkAvailTransientBuffers(_numVertices, decl, _numIndices);
}

BGFX_C_API void bgfx_alloc_transient_index_buffer(bgfx_transient_index_buffer_t* _tib, uint32_t _num)
{
	bgfx::allocTransientIndexBuffer( (bgfx::TransientIndexBuffer*)_tib, _num);
}

BGFX_C_API void bgfx_alloc_transient_vertex_buffer(bgfx_transient_vertex_buffer_t* _tvb, uint32_t _num, const bgfx_vertex_decl_t* _decl)
{
	const bgfx::VertexDecl& decl = *(const bgfx::VertexDecl*)_decl;
	bgfx::allocTransientVertexBuffer( (bgfx::TransientVertexBuffer*)_tvb, _num, decl);
}

BGFX_C_API bool bgfx_alloc_transient_buffers(bgfx_transient_vertex_buffer_t* _tvb, const bgfx_vertex_decl_t* _decl, uint16_t _numVertices, bgfx_transient_index_buffer_t* _tib, uint16_t _numIndices)
{
	const bgfx::VertexDecl& decl = *(const bgfx::VertexDecl*)_decl;
	return bgfx::allocTransientBuffers( (bgfx::TransientVertexBuffer*)_tvb, decl, _numVertices, (bgfx::TransientIndexBuffer*)_tib, _numIndices);
}

BGFX_C_API const bgfx_instance_data_buffer_t* bgfx_alloc_instance_data_buffer(uint32_t _num, uint16_t _stride)
{
	return (bgfx_instance_data_buffer_t*)bgfx::allocInstanceDataBuffer(_num, _stride);
}

BGFX_C_API bgfx_shader_handle_t bgfx_create_shader(const bgfx_memory_t* _mem)
{
	union { bgfx_shader_handle_t c; bgfx::ShaderHandle cpp; } handle;
	handle.cpp = bgfx::createShader( (const bgfx::Memory*)_mem);
	return handle.c;
}

BGFX_C_API uint16_t bgfx_get_shader_uniforms(bgfx_shader_handle_t _handle, bgfx_uniform_handle_t* _uniforms, uint16_t _max)
{
	union { bgfx_shader_handle_t c; bgfx::ShaderHandle cpp; } handle = { _handle };
	return bgfx::getShaderUniforms(handle.cpp, (bgfx::UniformHandle*)_uniforms, _max);
}

BGFX_C_API void bgfx_destroy_shader(bgfx_shader_handle_t _handle)
{
	union { bgfx_shader_handle_t c; bgfx::ShaderHandle cpp; } handle = { _handle };
	bgfx::destroyShader(handle.cpp);
}

BGFX_C_API bgfx_program_handle_t bgfx_create_program(bgfx_shader_handle_t _vsh, bgfx_shader_handle_t _fsh, bool _destroyShaders)
{
	union { bgfx_shader_handle_t c; bgfx::ShaderHandle cpp; } vsh = { _vsh };
	union { bgfx_shader_handle_t c; bgfx::ShaderHandle cpp; } fsh = { _fsh };
	union { bgfx_program_handle_t c; bgfx::ProgramHandle cpp; } handle;
	handle.cpp = bgfx::createProgram(vsh.cpp, fsh.cpp, _destroyShaders);
	return handle.c;
}

BGFX_C_API void bgfx_destroy_program(bgfx_program_handle_t _handle)
{
	union { bgfx_program_handle_t c; bgfx::ProgramHandle cpp; } handle = { _handle };
	bgfx::destroyProgram(handle.cpp);
}

BGFX_C_API void bgfx_calc_texture_size(bgfx_texture_info_t* _info, uint16_t _width, uint16_t _height, uint16_t _depth, uint8_t _numMips, bgfx_texture_format_t _format)
{
	bgfx::TextureInfo& info = *(bgfx::TextureInfo*)_info;
	bgfx::calcTextureSize(info, _width, _height, _depth, _numMips, bgfx::TextureFormat::Enum(_format) );
}

BGFX_C_API bgfx_texture_handle_t bgfx_create_texture(const bgfx_memory_t* _mem, uint32_t _flags, uint8_t _skip, bgfx_texture_info_t* _info)
{
	union { bgfx_texture_handle_t c; bgfx::TextureHandle cpp; } handle;
	bgfx::TextureInfo* info = (bgfx::TextureInfo*)_info;
	handle.cpp = bgfx::createTexture( (const bgfx::Memory*)_mem, _flags, _skip, info);
	return handle.c;
}

BGFX_C_API bgfx_texture_handle_t bgfx_create_texture_2d(uint16_t _width, uint16_t _height, uint8_t _numMips, bgfx_texture_format_t _format, uint32_t _flags, const bgfx_memory_t* _mem)
{
	union { bgfx_texture_handle_t c; bgfx::TextureHandle cpp; } handle;
	handle.cpp = bgfx::createTexture2D(_width, _height, _numMips, bgfx::TextureFormat::Enum(_format), _flags, (const bgfx::Memory*)_mem);
	return handle.c;
}

BGFX_C_API bgfx_texture_handle_t bgfx_create_texture_3d(uint16_t _width, uint16_t _height, uint16_t _depth, uint8_t _numMips, bgfx_texture_format_t _format, uint32_t _flags, const bgfx_memory_t* _mem)
{
	union { bgfx_texture_handle_t c; bgfx::TextureHandle cpp; } handle;
	handle.cpp = bgfx::createTexture3D(_width, _height, _depth, _numMips, bgfx::TextureFormat::Enum(_format), _flags, (const bgfx::Memory*)_mem);
	return handle.c;
}

BGFX_C_API bgfx_texture_handle_t bgfx_create_texture_cube(uint16_t _size, uint8_t _numMips, bgfx_texture_format_t _format, uint32_t _flags, const bgfx_memory_t* _mem)
{
	union { bgfx_texture_handle_t c; bgfx::TextureHandle cpp; } handle;
	handle.cpp = bgfx::createTextureCube(_size, _numMips, bgfx::TextureFormat::Enum(_format), _flags, (const bgfx::Memory*)_mem);
	return handle.c;
}

BGFX_C_API void bgfx_update_texture_2d(bgfx_texture_handle_t _handle, uint8_t _mip, uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height, const bgfx_memory_t* _mem, uint16_t _pitch)
{
	union { bgfx_texture_handle_t c; bgfx::TextureHandle cpp; } handle = { _handle };
	bgfx::updateTexture2D(handle.cpp, _mip, _x, _y, _width, _height, (const bgfx::Memory*)_mem, _pitch);
}

BGFX_C_API void bgfx_update_texture_3d(bgfx_texture_handle_t _handle, uint8_t _mip, uint16_t _x, uint16_t _y, uint16_t _z, uint16_t _width, uint16_t _height, uint16_t _depth, const bgfx_memory_t* _mem)
{
	union { bgfx_texture_handle_t c; bgfx::TextureHandle cpp; } handle = { _handle };
	bgfx::updateTexture3D(handle.cpp, _mip, _x, _y, _z, _width, _height, _depth, (const bgfx::Memory*)_mem);
}

BGFX_C_API void bgfx_update_texture_cube(bgfx_texture_handle_t _handle, uint8_t _side, uint8_t _mip, uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height, const bgfx_memory_t* _mem, uint16_t _pitch)
{
	union { bgfx_texture_handle_t c; bgfx::TextureHandle cpp; } handle = { _handle };
	bgfx::updateTextureCube(handle.cpp, _side, _mip, _x, _y, _width, _height, (const bgfx::Memory*)_mem, _pitch);
}

BGFX_C_API void bgfx_destroy_texture(bgfx_texture_handle_t _handle)
{
	union { bgfx_texture_handle_t c; bgfx::TextureHandle cpp; } handle = { _handle };
	bgfx::destroyTexture(handle.cpp);
}

BGFX_C_API bgfx_frame_buffer_handle_t bgfx_create_frame_buffer(uint16_t _width, uint16_t _height, bgfx_texture_format_t _format, uint32_t _textureFlags)
{
	union { bgfx_frame_buffer_handle_t c; bgfx::FrameBufferHandle cpp; } handle;
	handle.cpp = bgfx::createFrameBuffer(_width, _height, bgfx::TextureFormat::Enum(_format), _textureFlags);
	return handle.c;
}

BGFX_C_API bgfx_frame_buffer_handle_t bgfx_create_frame_buffer_from_handles(uint8_t _num, bgfx_texture_handle_t* _handles, bool _destroyTextures)
{
	union { bgfx_frame_buffer_handle_t c; bgfx::FrameBufferHandle cpp; } handle;
	handle.cpp = bgfx::createFrameBuffer(_num, (bgfx::TextureHandle*)_handles, _destroyTextures);
	return handle.c;
}

BGFX_C_API bgfx_frame_buffer_handle_t bgfx_create_frame_buffer_from_nwh(void* _nwh, uint16_t _width, uint16_t _height, bgfx_texture_format_t _depthFormat)
{
	union { bgfx_frame_buffer_handle_t c; bgfx::FrameBufferHandle cpp; } handle;
	handle.cpp = bgfx::createFrameBuffer(_nwh, _width, _height, bgfx::TextureFormat::Enum(_depthFormat) );
	return handle.c;
}

BGFX_C_API void bgfx_destroy_frame_buffer(bgfx_frame_buffer_handle_t _handle)
{
	union { bgfx_frame_buffer_handle_t c; bgfx::FrameBufferHandle cpp; } handle = { _handle };
	bgfx::destroyFrameBuffer(handle.cpp);
}

BGFX_C_API bgfx_uniform_handle_t bgfx_create_uniform(const char* _name, bgfx_uniform_type_t _type, uint16_t _num)
{
	union { bgfx_uniform_handle_t c; bgfx::UniformHandle cpp; } handle;
	handle.cpp = bgfx::createUniform(_name, bgfx::UniformType::Enum(_type), _num);
	return handle.c;
}

BGFX_C_API void bgfx_destroy_uniform(bgfx_uniform_handle_t _handle)
{
	union { bgfx_uniform_handle_t c; bgfx::UniformHandle cpp; } handle = { _handle };
	bgfx::destroyUniform(handle.cpp);
}

BGFX_C_API void bgfx_set_clear_color(uint8_t _index, const float _rgba[4])
{
	bgfx::setClearColor(_index, _rgba);
}

BGFX_C_API void bgfx_set_view_name(uint8_t _id, const char* _name)
{
	bgfx::setViewName(_id, _name);
}

BGFX_C_API void bgfx_set_view_rect(uint8_t _id, uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height)
{
	bgfx::setViewRect(_id, _x, _y, _width, _height);
}

BGFX_C_API void bgfx_set_view_scissor(uint8_t _id, uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height)
{
	bgfx::setViewScissor(_id, _x, _y, _width, _height);
}

BGFX_C_API void bgfx_set_view_clear(uint8_t _id, uint8_t _flags, uint32_t _rgba, float _depth, uint8_t _stencil)
{
	bgfx::setViewClear(_id, _flags, _rgba, _depth, _stencil);
}

BGFX_C_API void bgfx_set_view_clear_mrt(uint8_t _id, uint8_t _flags, float _depth, uint8_t _stencil, uint8_t _0, uint8_t _1, uint8_t _2, uint8_t _3, uint8_t _4, uint8_t _5, uint8_t _6, uint8_t _7)
{
	bgfx::setViewClear(_id, _flags, _depth, _stencil, _0, _1, _2, _3, _4, _5, _6, _7);
}

BGFX_C_API void bgfx_set_view_seq(uint8_t _id, bool _enabled)
{
	bgfx::setViewSeq(_id, _enabled);
}

BGFX_C_API void bgfx_set_view_frame_buffer(uint8_t _id, bgfx_frame_buffer_handle_t _handle)
{
	union { bgfx_frame_buffer_handle_t c; bgfx::FrameBufferHandle cpp; } handle = { _handle };
	bgfx::setViewFrameBuffer(_id, handle.cpp);
}

BGFX_C_API void bgfx_set_view_transform(uint8_t _id, const void* _view, const void* _proj)
{
	bgfx::setViewTransform(_id, _view, _proj);
}

BGFX_C_API void bgfx_set_marker(const char* _marker)
{
	bgfx::setMarker(_marker);
}

BGFX_C_API void bgfx_set_state(uint64_t _state, uint32_t _rgba)
{
	bgfx::setState(_state, _rgba);
}

BGFX_C_API void bgfx_set_stencil(uint32_t _fstencil, uint32_t _bstencil)
{
	bgfx::setStencil(_fstencil, _bstencil);
}

BGFX_C_API uint16_t bgfx_set_scissor(uint16_t _x, uint16_t _y, uint16_t _width, uint16_t _height)
{
	return bgfx::setScissor(_x, _y, _width, _height);
}

BGFX_C_API void bgfx_set_scissor_cached(uint16_t _cache)
{
	bgfx::setScissor(_cache);
}

BGFX_C_API uint32_t bgfx_set_transform(const void* _mtx, uint16_t _num)
{
	return bgfx::setTransform(_mtx, _num);
}

BGFX_C_API uint32_t bgfx_alloc_transform(bgfx_transform_t* _transform, uint16_t _num)
{
	return bgfx::allocTransform( (bgfx::Transform*)_transform, _num);
}

BGFX_C_API void bgfx_set_transform_cached(uint32_t _cache, uint16_t _num)
{
	bgfx::setTransform(_cache, _num);
}

BGFX_C_API void bgfx_set_uniform(bgfx_uniform_handle_t _handle, const void* _value, uint16_t _num)
{
	union { bgfx_uniform_handle_t c; bgfx::UniformHandle cpp; } handle = { _handle };
	bgfx::setUniform(handle.cpp, _value, _num);
}

BGFX_C_API void bgfx_set_index_buffer(bgfx_index_buffer_handle_t _handle, uint32_t _firstIndex, uint32_t _numIndices)
{
	union { bgfx_index_buffer_handle_t c; bgfx::IndexBufferHandle cpp; } handle = { _handle };
	bgfx::setIndexBuffer(handle.cpp, _firstIndex, _numIndices);
}

BGFX_C_API void bgfx_set_dynamic_index_buffer(bgfx_dynamic_index_buffer_handle_t _handle, uint32_t _firstIndex, uint32_t _numIndices)
{
	union { bgfx_dynamic_index_buffer_handle_t c; bgfx::DynamicIndexBufferHandle cpp; } handle = { _handle };
	bgfx::setIndexBuffer(handle.cpp, _firstIndex, _numIndices);
}

BGFX_C_API void bgfx_set_transient_index_buffer(const bgfx_transient_index_buffer_t* _tib, uint32_t _firstIndex, uint32_t _numIndices)
{
	bgfx::setIndexBuffer( (const bgfx::TransientIndexBuffer*)_tib, _firstIndex, _numIndices);
}

BGFX_C_API void bgfx_set_vertex_buffer(bgfx_vertex_buffer_handle_t _handle, uint32_t _startVertex, uint32_t _numVertices)
{
	union { bgfx_vertex_buffer_handle_t c; bgfx::VertexBufferHandle cpp; } handle = { _handle };
	bgfx::setVertexBuffer(handle.cpp, _startVertex, _numVertices);
}

BGFX_C_API void bgfx_set_dynamic_vertex_buffer(bgfx_dynamic_vertex_buffer_handle_t _handle, uint32_t _numVertices)
{
	union { bgfx_dynamic_vertex_buffer_handle_t c; bgfx::DynamicVertexBufferHandle cpp; } handle = { _handle };
	bgfx::setVertexBuffer(handle.cpp, _numVertices);
}

BGFX_C_API void bgfx_set_transient_vertex_buffer(const bgfx_transient_vertex_buffer_t* _tvb, uint32_t _startVertex, uint32_t _numVertices)
{
	bgfx::setVertexBuffer( (const bgfx::TransientVertexBuffer*)_tvb, _startVertex, _numVertices);
}

BGFX_C_API void bgfx_set_instance_data_buffer(const bgfx_instance_data_buffer_t* _idb, uint16_t _num)
{
	bgfx::setInstanceDataBuffer( (const bgfx::InstanceDataBuffer*)_idb, _num);
}

BGFX_C_API void bgfx_set_program(bgfx_program_handle_t _handle)
{
	union { bgfx_program_handle_t c; bgfx::ProgramHandle cpp; } handle = { _handle };
	bgfx::setProgram(handle.cpp);
}

BGFX_C_API void bgfx_set_texture(uint8_t _stage, bgfx_uniform_handle_t _sampler, bgfx_texture_handle_t _handle, uint32_t _flags)
{
	union { bgfx_uniform_handle_t c; bgfx::UniformHandle cpp; } sampler = { _sampler };
	union { bgfx_texture_handle_t c; bgfx::TextureHandle cpp; } handle  = { _handle  };
	bgfx::setTexture(_stage, sampler.cpp, handle.cpp, _flags);
}

BGFX_C_API void bgfx_set_texture_from_frame_buffer(uint8_t _stage, bgfx_uniform_handle_t _sampler, bgfx_frame_buffer_handle_t _handle, uint8_t _attachment, uint32_t _flags)
{
	union { bgfx_uniform_handle_t c;      bgfx::UniformHandle cpp;     } sampler = { _sampler };
	union { bgfx_frame_buffer_handle_t c; bgfx::FrameBufferHandle cpp; } handle  = { _handle };
	bgfx::setTexture(_stage, sampler.cpp, handle.cpp, _attachment, _flags);
}

BGFX_C_API uint32_t bgfx_submit(uint8_t _id, int32_t _depth)
{
	return bgfx::submit(_id, _depth);
}

BGFX_C_API void bgfx_set_image(uint8_t _stage, bgfx_uniform_handle_t _sampler, bgfx_texture_handle_t _handle, uint8_t _mip, bgfx_texture_format_t _format, bgfx_access_t _access)
{
	union { bgfx_uniform_handle_t c; bgfx::UniformHandle cpp; } sampler = { _sampler };
	union { bgfx_texture_handle_t c; bgfx::TextureHandle cpp; } handle  = { _handle  };
	bgfx::setImage(_stage, sampler.cpp, handle.cpp, _mip, bgfx::TextureFormat::Enum(_format), bgfx::Access::Enum(_access) );
}

BGFX_C_API void bgfx_set_image_from_frame_buffer(uint8_t _stage, bgfx_uniform_handle_t _sampler, bgfx_frame_buffer_handle_t _handle, uint8_t _attachment, bgfx_texture_format_t _format, bgfx_access_t _access)
{
	union { bgfx_uniform_handle_t c;      bgfx::UniformHandle cpp;     } sampler = { _sampler };
	union { bgfx_frame_buffer_handle_t c; bgfx::FrameBufferHandle cpp; } handle  = { _handle };
	bgfx::setImage(_stage, sampler.cpp, handle.cpp, _attachment, bgfx::TextureFormat::Enum(_format), bgfx::Access::Enum(_access) );
}

BGFX_C_API void bgfx_dispatch(uint8_t _id, bgfx_program_handle_t _handle, uint16_t _numX, uint16_t _numY, uint16_t _numZ)
{
	union { bgfx_program_handle_t c; bgfx::ProgramHandle cpp; } handle = { _handle };
	bgfx::dispatch(_id, handle.cpp, _numX, _numY, _numZ);
}

BGFX_C_API void bgfx_discard()
{
	bgfx::discard();
}

BGFX_C_API void bgfx_save_screen_shot(const char* _filePath)
{
	bgfx::saveScreenShot(_filePath);
}

BGFX_C_API bgfx_render_frame_t bgfx_render_frame()
{
	return bgfx_render_frame_t(bgfx::renderFrame() );
}

#if BX_PLATFORM_ANDROID
BGFX_C_API void bgfx_android_set_window(ANativeWindow* _window)
{
	bgfx::androidSetWindow(_window);
}

#elif BX_PLATFORM_IOS
BGFX_C_API void bgfx_ios_set_eagl_layer(void* _layer)
{
	bgfx::iosSetEaglLayer(_layer);
}

#elif BX_PLATFORM_FREEBSD || BX_PLATFORM_LINUX || BX_PLATFORM_RPI
BGFX_C_API void bgfx_x11_set_display_window(::Display* _display, ::Window _window)
{
	bgfx::x11SetDisplayWindow(_display, _window);
}

#elif BX_PLATFORM_NACL
BGFX_C_API bool bgfx_nacl_set_interfaces(PP_Instance _instance, const PPB_Instance* _instInterface, const PPB_Graphics3D* _graphicsInterface, bgfx_post_swap_buffers_fn _postSwapBuffers)
{
	return bgfx::naclSetInterfaces(_instance, _instInterface, _graphicsInterface, _postSwapBuffers);
}

#elif BX_PLATFORM_OSX
BGFX_C_API void bgfx_osx_set_ns_window(void* _window)
{
	bgfx::osxSetNSWindow(_window);
}

#elif BX_PLATFORM_WINDOWS
BGFX_C_API void bgfx_win_set_hwnd(HWND _window)
{
	bgfx::winSetHwnd(_window);
}

#endif // BX_PLATFORM_*
/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "bgfx_p.h"

#if (BGFX_CONFIG_RENDERER_OPENGLES || BGFX_CONFIG_RENDERER_OPENGL)
#	include "renderer_gl.h"

#	if BGFX_USE_EGL

#		if BX_PLATFORM_RPI
#			include <bcm_host.h>
#		endif // BX_PLATFORM_RPI

#ifndef EGL_CONTEXT_MAJOR_VERSION_KHR
#	define EGL_CONTEXT_MAJOR_VERSION_KHR EGL_CONTEXT_CLIENT_VERSION
#endif // EGL_CONTEXT_MAJOR_VERSION_KHR

#ifndef EGL_CONTEXT_MINOR_VERSION_KHR
#	define EGL_CONTEXT_MINOR_VERSION_KHR 0x30FB
#endif // EGL_CONTEXT_MINOR_VERSION_KHR

namespace bgfx
{
#if BGFX_USE_GL_DYNAMIC_LIB

	typedef void (*EGLPROC)(void);

	typedef EGLPROC (EGLAPIENTRY* PFNEGLGETPROCADDRESSPROC)(const char *procname);
	typedef EGLBoolean (EGLAPIENTRY* PFNEGLSWAPINTERVALPROC)(EGLDisplay dpy, EGLint interval);
	typedef EGLBoolean (EGLAPIENTRY* PFNEGLMAKECURRENTPROC)(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
	typedef EGLContext (EGLAPIENTRY* PFNEGLCREATECONTEXTPROC)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list);
	typedef EGLSurface (EGLAPIENTRY* PFNEGLCREATEWINDOWSURFACEPROC)(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list);
	typedef EGLBoolean (EGLAPIENTRY* PFNEGLCHOOSECONFIGPROC)(EGLDisplay dpy, const EGLint *attrib_list,	EGLConfig *configs, EGLint config_size,	EGLint *num_config);
	typedef EGLBoolean (EGLAPIENTRY* PFNEGLINITIALIZEPROC)(EGLDisplay dpy, EGLint *major, EGLint *minor);
	typedef EGLint     (EGLAPIENTRY* PFNEGLGETERRORPROC)(void);
	typedef EGLDisplay (EGLAPIENTRY* PFNEGLGETDISPLAYPROC)(EGLNativeDisplayType display_id);
	typedef EGLBoolean (EGLAPIENTRY* PFNEGLTERMINATEPROC)(EGLDisplay dpy);
	typedef EGLBoolean (EGLAPIENTRY* PFNEGLDESTROYSURFACEPROC)(EGLDisplay dpy, EGLSurface surface);
	typedef EGLBoolean (EGLAPIENTRY* PFNEGLDESTROYCONTEXTPROC)(EGLDisplay dpy, EGLContext ctx);
	typedef EGLBoolean (EGLAPIENTRY* PFNEGLSWAPBUFFERSPROC)(EGLDisplay dpy, EGLSurface surface);

#define EGL_IMPORT \
			EGL_IMPORT_FUNC(PFNEGLGETPROCADDRESSPROC,      eglGetProcAddress); \
			EGL_IMPORT_FUNC(PFNEGLSWAPINTERVALPROC,        eglSwapInterval); \
			EGL_IMPORT_FUNC(PFNEGLMAKECURRENTPROC,         eglMakeCurrent); \
			EGL_IMPORT_FUNC(PFNEGLCREATECONTEXTPROC,       eglCreateContext); \
			EGL_IMPORT_FUNC(PFNEGLCREATEWINDOWSURFACEPROC, eglCreateWindowSurface); \
			EGL_IMPORT_FUNC(PFNEGLCHOOSECONFIGPROC,        eglChooseConfig); \
			EGL_IMPORT_FUNC(PFNEGLINITIALIZEPROC,          eglInitialize); \
			EGL_IMPORT_FUNC(PFNEGLGETERRORPROC,            eglGetError); \
			EGL_IMPORT_FUNC(PFNEGLGETDISPLAYPROC,          eglGetDisplay); \
			EGL_IMPORT_FUNC(PFNEGLTERMINATEPROC,           eglTerminate); \
			EGL_IMPORT_FUNC(PFNEGLDESTROYSURFACEPROC,      eglDestroySurface); \
			EGL_IMPORT_FUNC(PFNEGLDESTROYCONTEXTPROC,      eglDestroyContext); \
			EGL_IMPORT_FUNC(PFNEGLSWAPBUFFERSPROC,         eglSwapBuffers);

#define EGL_IMPORT_FUNC(_proto, _func) _proto _func
EGL_IMPORT
#undef EGL_IMPORT_FUNC

	void* eglOpen()
	{
		void* handle = bx::dlopen("libEGL.dll");
		BGFX_FATAL(NULL != handle, Fatal::UnableToInitialize, "Failed to load libEGL dynamic library.");

#define EGL_IMPORT_FUNC(_proto, _func) \
			_func = (_proto)bx::dlsym(handle, #_func); \
			BX_TRACE("%p " #_func, _func); \
			BGFX_FATAL(NULL != _func, Fatal::UnableToInitialize, "Failed get " #_func ".")
EGL_IMPORT
#undef EGL_IMPORT_FUNC

		return handle;
	}

	void eglClose(void* _handle)
	{
		bx::dlclose(_handle);

#define EGL_IMPORT_FUNC(_proto, _func) _func = NULL
EGL_IMPORT
#undef EGL_IMPORT_FUNC
	}

#else

	void* eglOpen()
	{
		return NULL;
	}

	void eglClose(void* /*_handle*/)
	{
	}
#endif // BGFX_USE_GL_DYNAMIC_LIB

#	define GL_IMPORT(_optional, _proto, _func, _import) _proto _func = NULL
#	include "glimports.h"

	static const EGLint s_contextAttrs[] =
	{
#	if BGFX_CONFIG_RENDERER_OPENGLES >= 30
		EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
#		if BGFX_CONFIG_RENDERER_OPENGLES >= 31
		EGL_CONTEXT_MINOR_VERSION_KHR, 1,
#		else
		//			EGL_CONTEXT_MINOR_VERSION_KHR, 0,
#		endif // BGFX_CONFIG_RENDERER_OPENGLES >= 31
#	elif BGFX_CONFIG_RENDERER_OPENGLES
		EGL_CONTEXT_MAJOR_VERSION_KHR, 2,
		//			EGL_CONTEXT_MINOR_VERSION_KHR, 0,
#	endif // BGFX_CONFIG_RENDERER_

		EGL_NONE
	};

	struct SwapChainGL
	{
		SwapChainGL(EGLDisplay _display, EGLConfig _config, EGLContext _context, EGLNativeWindowType _nwh)
			: m_nwh(_nwh)
			, m_display(_display)
		{
			m_surface = eglCreateWindowSurface(m_display, _config, _nwh, NULL);
			BGFX_FATAL(m_surface != EGL_NO_SURFACE, Fatal::UnableToInitialize, "Failed to create surface.");

			m_context = eglCreateContext(m_display, _config, _context, s_contextAttrs);
			BX_CHECK(NULL != m_context, "Create swap chain failed: %x", eglGetError() );

			makeCurrent();
			GL_CHECK(glClearColor(0.0f, 0.0f, 0.0f, 0.0f) );
			GL_CHECK(glClear(GL_COLOR_BUFFER_BIT) );
			swapBuffers();
			GL_CHECK(glClear(GL_COLOR_BUFFER_BIT) );
			swapBuffers();
		}

		~SwapChainGL()
		{
			eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			eglDestroyContext(m_display, m_context);
			eglDestroySurface(m_display, m_surface);
		}

		void makeCurrent()
		{
			eglMakeCurrent(m_display, m_surface, m_surface, m_context);
		}

		void swapBuffers()
		{
			eglSwapBuffers(m_display, m_surface);
		}

		EGLNativeWindowType m_nwh;
		EGLContext m_context;
		EGLDisplay m_display;
		EGLSurface m_surface;
	};

#	if BX_PLATFORM_RPI
	static EGL_DISPMANX_WINDOW_T s_dispmanWindow;

	void x11SetDisplayWindow(::Display* _display, ::Window _window)
	{
		// Noop for now...
		BX_UNUSED(_display, _window);
	}
#	endif // BX_PLATFORM_RPI

	void GlContext::create(uint32_t _width, uint32_t _height)
	{
#	if BX_PLATFORM_RPI
		bcm_host_init();
#	endif // BX_PLATFORM_RPI

		m_eglLibrary = eglOpen();

		BX_UNUSED(_width, _height);
		EGLNativeDisplayType ndt = EGL_DEFAULT_DISPLAY;
		EGLNativeWindowType nwh = (EGLNativeWindowType)NULL;
#	if BX_PLATFORM_WINDOWS
		ndt = GetDC(g_bgfxHwnd);
		nwh = g_bgfxHwnd;
#	endif // BX_PLATFORM_
		m_display = eglGetDisplay(ndt);
		BGFX_FATAL(m_display != EGL_NO_DISPLAY, Fatal::UnableToInitialize, "Failed to create display %p", m_display);

		EGLint major = 0;
		EGLint minor = 0;
		EGLBoolean success = eglInitialize(m_display, &major, &minor);
		BGFX_FATAL(success && major >= 1 && minor >= 3, Fatal::UnableToInitialize, "Failed to initialize %d.%d", major, minor);

		EGLint attrs[] =
		{
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,

#	if BX_PLATFORM_ANDROID
			EGL_DEPTH_SIZE, 16,
#	else
			EGL_DEPTH_SIZE, 24,
#	endif // BX_PLATFORM_
			EGL_STENCIL_SIZE, 8,

			EGL_NONE
		};

		EGLint numConfig = 0;
		success = eglChooseConfig(m_display, attrs, &m_config, 1, &numConfig);
		BGFX_FATAL(success, Fatal::UnableToInitialize, "eglChooseConfig");

#	if BX_PLATFORM_ANDROID
		EGLint format;
		eglGetConfigAttrib(m_display, m_config, EGL_NATIVE_VISUAL_ID, &format);
		ANativeWindow_setBuffersGeometry(g_bgfxAndroidWindow, _width, _height, format);
		nwh = g_bgfxAndroidWindow;
#	elif BX_PLATFORM_RPI
		DISPMANX_DISPLAY_HANDLE_T dispmanDisplay = vc_dispmanx_display_open(0);
		DISPMANX_UPDATE_HANDLE_T  dispmanUpdate  = vc_dispmanx_update_start(0);

		VC_RECT_T dstRect = { 0, 0, _width,        _height       };
		VC_RECT_T srcRect = { 0, 0, _width  << 16, _height << 16 };

		DISPMANX_ELEMENT_HANDLE_T dispmanElement = vc_dispmanx_element_add(dispmanUpdate
			, dispmanDisplay
			, 0
			, &dstRect
			, 0
			, &srcRect
			, DISPMANX_PROTECTION_NONE
			, NULL
			, NULL
			, DISPMANX_NO_ROTATE
			);

		s_dispmanWindow.element = dispmanElement;
		s_dispmanWindow.width   = _width;
		s_dispmanWindow.height  = _height;
		nwh = &s_dispmanWindow;

		vc_dispmanx_update_submit_sync(dispmanUpdate);
#	endif // BX_PLATFORM_ANDROID

		m_surface = eglCreateWindowSurface(m_display, m_config, nwh, NULL);
		BGFX_FATAL(m_surface != EGL_NO_SURFACE, Fatal::UnableToInitialize, "Failed to create surface.");

		m_context = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, s_contextAttrs);
		BGFX_FATAL(m_context != EGL_NO_CONTEXT, Fatal::UnableToInitialize, "Failed to create context.");

		success = eglMakeCurrent(m_display, m_surface, m_surface, m_context);
		BGFX_FATAL(success, Fatal::UnableToInitialize, "Failed to set context.");

		eglSwapInterval(m_display, 0);

#	if BX_PLATFORM_EMSCRIPTEN
		emscripten_set_canvas_size(_width, _height);
#	endif // BX_PLATFORM_EMSCRIPTEN

		import();
	}

	void GlContext::destroy()
	{
		eglMakeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroyContext(m_display, m_context);
		eglDestroySurface(m_display, m_surface);
		eglTerminate(m_display);
		m_context = NULL;

		eglClose(m_eglLibrary);

#	if BX_PLATFORM_RPI
		bcm_host_deinit();
#	endif // BX_PLATFORM_RPI
	}

	void GlContext::resize(uint32_t /*_width*/, uint32_t /*_height*/, bool _vsync)
	{
		eglSwapInterval(m_display, _vsync ? 1 : 0);
	}

	bool GlContext::isSwapChainSupported()
	{
		return BX_ENABLED(0
						| BX_PLATFORM_LINUX
						| BX_PLATFORM_WINDOWS
						);
	}

	SwapChainGL* GlContext::createSwapChain(void* _nwh)
	{
		return BX_NEW(g_allocator, SwapChainGL)(m_display, m_config, m_context, (EGLNativeWindowType)_nwh);
	}

	void GlContext::destorySwapChain(SwapChainGL* _swapChain)
	{
		BX_DELETE(g_allocator, _swapChain);
	}

	void GlContext::swap(SwapChainGL* _swapChain)
	{
		if (NULL == _swapChain)
		{
			eglMakeCurrent(m_display, m_surface, m_surface, m_context);
			eglSwapBuffers(m_display, m_surface);
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
			eglMakeCurrent(m_display, m_surface, m_surface, m_context);
		}
		else
		{
			_swapChain->makeCurrent();
		}
	}

	void GlContext::import()
	{
		BX_TRACE("Import:");
#	if BX_PLATFORM_WINDOWS
		void* glesv2 = bx::dlopen("libGLESv2.dll");
#		define GL_EXTENSION(_optional, _proto, _func, _import) \
					{ \
						if (NULL == _func) \
						{ \
							_func = (_proto)bx::dlsym(glesv2, #_import); \
							BX_TRACE("\t%p " #_func " (" #_import ")", _func); \
							BGFX_FATAL(_optional || NULL != _func, Fatal::UnableToInitialize, "Failed to create OpenGLES context. eglGetProcAddress(\"%s\")", #_import); \
						} \
					}
#	else
#		define GL_EXTENSION(_optional, _proto, _func, _import) \
					{ \
						if (NULL == _func) \
						{ \
							_func = (_proto)eglGetProcAddress(#_import); \
							BX_TRACE("\t%p " #_func " (" #_import ")", _func); \
							BGFX_FATAL(_optional || NULL != _func, Fatal::UnableToInitialize, "Failed to create OpenGLES context. eglGetProcAddress(\"%s\")", #_import); \
						} \
					}
#	endif // BX_PLATFORM_
#	include "glimports.h"
	}

} // namespace bgfx

#	endif // BGFX_USE_EGL
#endif // (BGFX_CONFIG_RENDERER_OPENGLES || BGFX_CONFIG_RENDERER_OPENGL)
/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "bgfx_p.h"

#if (BX_PLATFORM_FREEBSD || BX_PLATFORM_LINUX) && (BGFX_CONFIG_RENDERER_OPENGLES || BGFX_CONFIG_RENDERER_OPENGL)
#	include "renderer_gl.h"
#	define GLX_GLXEXT_PROTOTYPES
#	include <glx/glxext.h>

namespace bgfx
{
	typedef int (*PFNGLXSWAPINTERVALMESAPROC)(uint32_t _interval);

	PFNGLXCREATECONTEXTATTRIBSARBPROC glXCreateContextAttribsARB;
	PFNGLXSWAPINTERVALEXTPROC glXSwapIntervalEXT;
	PFNGLXSWAPINTERVALMESAPROC glXSwapIntervalMESA;
	PFNGLXSWAPINTERVALSGIPROC glXSwapIntervalSGI;

#	define GL_IMPORT(_optional, _proto, _func, _import) _proto _func
#	include "glimports.h"

	static ::Display* s_display;
	static ::Window   s_window;

	struct SwapChainGL
	{
		SwapChainGL(::Window _window, XVisualInfo* _visualInfo, GLXContext _context)
			: m_window(_window)
		{
			m_context = glXCreateContext(s_display, _visualInfo, _context, GL_TRUE);
		}

		~SwapChainGL()
		{
			glXMakeCurrent(s_display, 0, 0);
			glXDestroyContext(s_display, m_context);
		}

		void makeCurrent()
		{
			glXMakeCurrent(s_display, m_window, m_context);
		}

		void swapBuffers()
		{
			glXSwapBuffers(s_display, m_window);
		}

		Window m_window;
		GLXContext m_context;
	};

	void x11SetDisplayWindow(::Display* _display, ::Window _window)
	{
		s_display = _display;
		s_window  = _window;
	}

	void GlContext::create(uint32_t _width, uint32_t _height)
	{
		BX_UNUSED(_width, _height);
		XLockDisplay(s_display);

		int major, minor;
		bool version = glXQueryVersion(s_display, &major, &minor);
		BGFX_FATAL(version, Fatal::UnableToInitialize, "Failed to query GLX version");
		BGFX_FATAL( (major == 1 && minor >= 2) || major > 1
				, Fatal::UnableToInitialize
				, "GLX version is not >=1.2 (%d.%d)."
				, major
				, minor
				);

		int32_t screen = DefaultScreen(s_display);

		const char* extensions = glXQueryExtensionsString(s_display, screen);
		BX_TRACE("GLX extensions:");
		dumpExtensions(extensions);

		const int attrsGlx[] =
		{
			GLX_RENDER_TYPE, GLX_RGBA_BIT,
			GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
			GLX_DOUBLEBUFFER, true,
			GLX_RED_SIZE, 8,
			GLX_BLUE_SIZE, 8,
			GLX_GREEN_SIZE, 8,
//			GLX_ALPHA_SIZE, 8,
			GLX_DEPTH_SIZE, 24,
			GLX_STENCIL_SIZE, 8,
			0,
		};

		// Find suitable config
		GLXFBConfig bestConfig = NULL;

		int numConfigs;
		GLXFBConfig* configs = glXChooseFBConfig(s_display, screen, attrsGlx, &numConfigs);

		BX_TRACE("glX num configs %d", numConfigs);

		for (int ii = 0; ii < numConfigs; ++ii)
		{
			m_visualInfo = glXGetVisualFromFBConfig(s_display, configs[ii]);
			if (NULL != m_visualInfo)
			{
				BX_TRACE("---");
				bool valid = true;
				for (uint32_t attr = 6; attr < BX_COUNTOF(attrsGlx)-1 && attrsGlx[attr] != None; attr += 2)
				{
					int value;
					glXGetFBConfigAttrib(s_display, configs[ii], attrsGlx[attr], &value);
					BX_TRACE("glX %d/%d %2d: %4x, %8x (%8x%s)"
							, ii
							, numConfigs
							, attr/2
							, attrsGlx[attr]
							, value
							, attrsGlx[attr + 1]
							, value < attrsGlx[attr + 1] ? " *" : ""
							);

					if (value < attrsGlx[attr + 1])
					{
						valid = false;
#if !BGFX_CONFIG_DEBUG
						break;
#endif // BGFX_CONFIG_DEBUG
					}
				}

				if (valid)
				{
					bestConfig = configs[ii];
					BX_TRACE("Best config %d.", ii);
					break;
				}
			}

			XFree(m_visualInfo);
			m_visualInfo = NULL;
		}

		XFree(configs);
		BGFX_FATAL(m_visualInfo, Fatal::UnableToInitialize, "Failed to find a suitable X11 display configuration.");

		BX_TRACE("Create GL 2.1 context.");
		m_context = glXCreateContext(s_display, m_visualInfo, 0, GL_TRUE);
		BGFX_FATAL(NULL != m_context, Fatal::UnableToInitialize, "Failed to create GL 2.1 context.");

#if BGFX_CONFIG_RENDERER_OPENGL >= 31
		glXCreateContextAttribsARB = (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddress( (const GLubyte*)"glXCreateContextAttribsARB");

		if (NULL != glXCreateContextAttribsARB)
		{
			BX_TRACE("Create GL 3.1 context.");
			const int contextAttrs[] =
			{
				GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
				GLX_CONTEXT_MINOR_VERSION_ARB, 1,
				GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
				0,
			};

			GLXContext context = glXCreateContextAttribsARB(s_display, bestConfig, 0, true, contextAttrs);

			if (NULL != context)
			{
				glXDestroyContext(s_display, m_context);
				m_context = context;
			}
		}
#else
		BX_UNUSED(bestConfig);
#endif // BGFX_CONFIG_RENDERER_OPENGL >= 31

		XUnlockDisplay(s_display);

		import();

		glXMakeCurrent(s_display, s_window, m_context);

		glXSwapIntervalEXT = (PFNGLXSWAPINTERVALEXTPROC)glXGetProcAddress( (const GLubyte*)"glXSwapIntervalEXT");
		if (NULL != glXSwapIntervalEXT)
		{
			BX_TRACE("Using glXSwapIntervalEXT.");
			glXSwapIntervalEXT(s_display, s_window, 0);
		}
		else
		{
			glXSwapIntervalMESA = (PFNGLXSWAPINTERVALMESAPROC)glXGetProcAddress( (const GLubyte*)"glXSwapIntervalMESA");
			if (NULL != glXSwapIntervalMESA)
			{
				BX_TRACE("Using glXSwapIntervalMESA.");
				glXSwapIntervalMESA(0);
			}
			else
			{
				glXSwapIntervalSGI = (PFNGLXSWAPINTERVALSGIPROC)glXGetProcAddress( (const GLubyte*)"glXSwapIntervalSGI");
				if (NULL != glXSwapIntervalSGI)
				{
					BX_TRACE("Using glXSwapIntervalSGI.");
					glXSwapIntervalSGI(0);
				}
			}
		}

		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glXSwapBuffers(s_display, s_window);
	}

	void GlContext::destroy()
	{
		glXMakeCurrent(s_display, 0, 0);
		glXDestroyContext(s_display, m_context);
		XFree(m_visualInfo);
	}

	void GlContext::resize(uint32_t /*_width*/, uint32_t /*_height*/, bool _vsync)
	{
		int32_t interval = _vsync ? 1 : 0;

		if (NULL != glXSwapIntervalEXT)
		{
			glXSwapIntervalEXT(s_display, s_window, interval);
		}
		else if (NULL != glXSwapIntervalMESA)
		{
			glXSwapIntervalMESA(interval);
		}
		else if (NULL != glXSwapIntervalSGI)
		{
			glXSwapIntervalSGI(interval);
		}
	}

	bool GlContext::isSwapChainSupported()
	{
		return true;
	}

	SwapChainGL* GlContext::createSwapChain(void* _nwh)
	{
		return BX_NEW(g_allocator, SwapChainGL)( (::Window)_nwh, m_visualInfo, m_context);
	}

	void GlContext::destorySwapChain(SwapChainGL* _swapChain)
	{
		BX_DELETE(g_allocator, _swapChain);
	}

	void GlContext::swap(SwapChainGL* _swapChain)
	{
		if (NULL == _swapChain)
		{
			glXMakeCurrent(s_display, s_window, m_context);
			glXSwapBuffers(s_display, s_window);
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
			glXMakeCurrent(s_display, s_window, m_context);
		}
		else
		{
			_swapChain->makeCurrent();
		}
	}

	void GlContext::import()
	{
#	define GL_EXTENSION(_optional, _proto, _func, _import) \
				{ \
					if (NULL == _func) \
					{ \
						_func = (_proto)glXGetProcAddress( (const GLubyte*)#_import); \
						BX_TRACE("%p " #_func " (" #_import ")", _func); \
						BGFX_FATAL(_optional || NULL != _func, Fatal::UnableToInitialize, "Failed to create OpenGL context. glXGetProcAddress %s", #_import); \
					} \
				}
#	include "glimports.h"
	}

} // namespace bgfx

#endif // (BX_PLATFORM_FREEBSD || BX_PLATFORM_LINUX) && (BGFX_CONFIG_RENDERER_OPENGLES || BGFX_CONFIG_RENDERER_OPENGL)
/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "bgfx_p.h"

#if BX_PLATFORM_NACL && (BGFX_CONFIG_RENDERER_OPENGLES || BGFX_CONFIG_RENDERER_OPENGL)
#	include <bgfxplatform.h>
#	include "renderer_gl.h"

namespace bgfx
{
#	define GL_IMPORT(_optional, _proto, _func, _import) _proto _func
#	include "glimports.h"

	void naclSwapCompleteCb(void* /*_data*/, int32_t /*_result*/);

	PP_CompletionCallback naclSwapComplete = 
	{
		naclSwapCompleteCb,
		NULL,
		PP_COMPLETIONCALLBACK_FLAG_NONE
	};

	struct Ppapi
	{
		Ppapi()
			: m_context(0)
			, m_instance(0)
			, m_instInterface(NULL)
			, m_graphicsInterface(NULL)
			, m_instancedArrays(NULL)
			, m_postSwapBuffers(NULL)
			, m_forceSwap(true)
		{
		}

		bool setInterfaces(PP_Instance _instance, const PPB_Instance* _instInterface, const PPB_Graphics3D* _graphicsInterface, PostSwapBuffersFn _postSwapBuffers);

		void resize(uint32_t _width, uint32_t _height, bool /*_vsync*/)
		{
			m_graphicsInterface->ResizeBuffers(m_context, _width, _height);
		}

		void swap()
		{
			glSetCurrentContextPPAPI(m_context);
			m_graphicsInterface->SwapBuffers(m_context, naclSwapComplete);
		}

		bool isValid() const
		{
			return 0 != m_context;
		}

		PP_Resource m_context;
		PP_Instance m_instance;
		const PPB_Instance* m_instInterface;
		const PPB_Graphics3D* m_graphicsInterface;
		const PPB_OpenGLES2InstancedArrays* m_instancedArrays;
		PostSwapBuffersFn m_postSwapBuffers;
		bool m_forceSwap;
	};
	
	static Ppapi s_ppapi;

	void naclSwapCompleteCb(void* /*_data*/, int32_t /*_result*/)
	{
		// For NaCl bgfx doesn't create render thread, but rendering is always
		// multithreaded. Frame rendering is done on main thread, and context
		// is initialized when PPAPI interfaces are set. Force swap is there to
		// keep calling swap complete callback, so that initialization doesn't
		// deadlock on semaphores.
		if (s_ppapi.m_forceSwap)
		{
			s_ppapi.swap();
		}

		renderFrame();
	}

	static void GL_APIENTRY naclVertexAttribDivisor(GLuint _index, GLuint _divisor)
	{
		s_ppapi.m_instancedArrays->VertexAttribDivisorANGLE(s_ppapi.m_context, _index, _divisor);
	}

	static void GL_APIENTRY naclDrawArraysInstanced(GLenum _mode, GLint _first, GLsizei _count, GLsizei _primcount)
	{
		s_ppapi.m_instancedArrays->DrawArraysInstancedANGLE(s_ppapi.m_context, _mode, _first, _count, _primcount);
	}

	static void GL_APIENTRY naclDrawElementsInstanced(GLenum _mode, GLsizei _count, GLenum _type, const GLvoid* _indices, GLsizei _primcount)
	{
		s_ppapi.m_instancedArrays->DrawElementsInstancedANGLE(s_ppapi.m_context, _mode, _count, _type, _indices, _primcount);
	}

	bool naclSetInterfaces(PP_Instance _instance, const PPB_Instance* _instInterface, const PPB_Graphics3D* _graphicsInterface, PostSwapBuffersFn _postSwapBuffers)
	{
		return s_ppapi.setInterfaces( _instance, _instInterface, _graphicsInterface, _postSwapBuffers);
	}

	bool Ppapi::setInterfaces(PP_Instance _instance, const PPB_Instance* _instInterface, const PPB_Graphics3D* _graphicsInterface, PostSwapBuffersFn _postSwapBuffers)
	{
		BX_TRACE("PPAPI Interfaces");

		m_instance = _instance;
		m_instInterface = _instInterface;
		m_graphicsInterface = _graphicsInterface;
		m_instancedArrays = glGetInstancedArraysInterfacePPAPI();
		m_postSwapBuffers = _postSwapBuffers;

		int32_t attribs[] =
		{
			PP_GRAPHICS3DATTRIB_ALPHA_SIZE, 8,
			PP_GRAPHICS3DATTRIB_DEPTH_SIZE, 24,
			PP_GRAPHICS3DATTRIB_STENCIL_SIZE, 8,
			PP_GRAPHICS3DATTRIB_SAMPLES, 0,
			PP_GRAPHICS3DATTRIB_SAMPLE_BUFFERS, 0,
			PP_GRAPHICS3DATTRIB_WIDTH, BGFX_DEFAULT_WIDTH,
			PP_GRAPHICS3DATTRIB_HEIGHT, BGFX_DEFAULT_HEIGHT,
			PP_GRAPHICS3DATTRIB_NONE
		};

		m_context = m_graphicsInterface->Create(m_instance, 0, attribs);
		if (0 == m_context)
		{
			BX_TRACE("Failed to create context!");
			return false;
		}

		m_instInterface->BindGraphics(m_instance, m_context);
		glSetCurrentContextPPAPI(m_context);
		m_graphicsInterface->SwapBuffers(m_context, naclSwapComplete);

		glVertexAttribDivisor   = naclVertexAttribDivisor;
		glDrawArraysInstanced   = naclDrawArraysInstanced;
		glDrawElementsInstanced = naclDrawElementsInstanced;

		// Prevent render thread creation.
		RenderFrame::Enum result = renderFrame();
		return RenderFrame::NoContext == result;
	}

	void GlContext::create(uint32_t _width, uint32_t _height)
	{
		BX_UNUSED(_width, _height);
		BX_TRACE("GlContext::create");
	}

	void GlContext::destroy()
	{
	}

	void GlContext::resize(uint32_t _width, uint32_t _height, bool _vsync)
	{
		s_ppapi.m_forceSwap = false;
		s_ppapi.resize(_width, _height, _vsync);
	}

	bool GlContext::isSwapChainSupported()
	{
		return false;
	}

	SwapChainGL* GlContext::createSwapChain(void* /*_nwh*/)
	{
		BX_CHECK(false, "Shouldn't be called!");
		return NULL;
	}

	void GlContext::destorySwapChain(SwapChainGL*  /*_swapChain*/)
	{
		BX_CHECK(false, "Shouldn't be called!");
	}

	void GlContext::swap(SwapChainGL* /*_swapChain*/)
	{
		s_ppapi.swap();
	}

	void GlContext::makeCurrent(SwapChainGL* /*_swapChain*/)
	{
	}

	void GlContext::import()
	{
	}

	bool GlContext::isValid() const
	{
		return s_ppapi.isValid();
	}

} // namespace bgfx

#endif // BX_PLATFORM_NACL && (BGFX_CONFIG_RENDERER_OPENGLES || BGFX_CONFIG_RENDERER_OPENGL)
/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "bgfx_p.h"

#if (BGFX_CONFIG_RENDERER_OPENGLES|BGFX_CONFIG_RENDERER_OPENGL)
#	include "renderer_gl.h"

#	if BGFX_USE_WGL

namespace bgfx
{
	PFNWGLGETPROCADDRESSPROC wglGetProcAddress;
	PFNWGLMAKECURRENTPROC wglMakeCurrent;
	PFNWGLCREATECONTEXTPROC wglCreateContext;
	PFNWGLDELETECONTEXTPROC wglDeleteContext;
	PFNWGLGETEXTENSIONSSTRINGARBPROC wglGetExtensionsStringARB;
	PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB;
	PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB;
	PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;

#	define GL_IMPORT(_optional, _proto, _func, _import) _proto _func
#	include "glimports.h"

	struct SwapChainGL
	{
		SwapChainGL(void* _nwh)
			: m_hwnd( (HWND)_nwh)
		{
			m_hdc = GetDC(m_hwnd);
		}

		~SwapChainGL()
		{
			wglMakeCurrent(NULL, NULL);
			wglDeleteContext(m_context);
			ReleaseDC(m_hwnd, m_hdc);
		}

		void makeCurrent()
		{
			wglMakeCurrent(m_hdc, m_context);
		}

		void swapBuffers()
		{
			SwapBuffers(m_hdc);
		}

		HWND  m_hwnd;
		HDC   m_hdc;
		HGLRC m_context;
	};

	static HGLRC createContext(HDC _hdc)
	{
		PIXELFORMATDESCRIPTOR pfd;
		memset(&pfd, 0, sizeof(pfd) );
		pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
		pfd.nVersion = 1;
		pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pfd.iPixelType = PFD_TYPE_RGBA;
		pfd.cColorBits = 32;
		pfd.cAlphaBits = 8;
		pfd.cDepthBits = 24;
		pfd.cStencilBits = 8;
		pfd.iLayerType = PFD_MAIN_PLANE;

		int pixelFormat = ChoosePixelFormat(_hdc, &pfd);
		BGFX_FATAL(0 != pixelFormat, Fatal::UnableToInitialize, "ChoosePixelFormat failed!");

		DescribePixelFormat(_hdc, pixelFormat, sizeof(PIXELFORMATDESCRIPTOR), &pfd);

		BX_TRACE("Pixel format:\n"
			"\tiPixelType %d\n"
			"\tcColorBits %d\n"
			"\tcAlphaBits %d\n"
			"\tcDepthBits %d\n"
			"\tcStencilBits %d\n"
			, pfd.iPixelType
			, pfd.cColorBits
			, pfd.cAlphaBits
			, pfd.cDepthBits
			, pfd.cStencilBits
			);

		int result;
		result = SetPixelFormat(_hdc, pixelFormat, &pfd);
		BGFX_FATAL(0 != result, Fatal::UnableToInitialize, "SetPixelFormat failed!");

		HGLRC context = wglCreateContext(_hdc);
		BGFX_FATAL(NULL != context, Fatal::UnableToInitialize, "wglCreateContext failed!");

		result = wglMakeCurrent(_hdc, context);
		BGFX_FATAL(0 != result, Fatal::UnableToInitialize, "wglMakeCurrent failed!");

		return context;
	}

	void GlContext::create(uint32_t /*_width*/, uint32_t /*_height*/)
	{
		m_opengl32dll = bx::dlopen("opengl32.dll");
		BGFX_FATAL(NULL != m_opengl32dll, Fatal::UnableToInitialize, "Failed to load opengl32.dll.");

		wglGetProcAddress = (PFNWGLGETPROCADDRESSPROC)bx::dlsym(m_opengl32dll, "wglGetProcAddress");
		BGFX_FATAL(NULL != wglGetProcAddress, Fatal::UnableToInitialize, "Failed get wglGetProcAddress.");

		// If g_bgfxHwnd is NULL, the assumption is that GL context was created
		// by user (for example, using SDL, GLFW, etc.)
		BX_WARN(NULL != g_bgfxHwnd
			, "bgfx::winSetHwnd with valid window is not called. This might "
			  "be intentional when GL context is created by the user."
			);

		if (NULL != g_bgfxHwnd)
		{
			wglMakeCurrent = (PFNWGLMAKECURRENTPROC)bx::dlsym(m_opengl32dll, "wglMakeCurrent");
			BGFX_FATAL(NULL != wglMakeCurrent, Fatal::UnableToInitialize, "Failed get wglMakeCurrent.");

			wglCreateContext = (PFNWGLCREATECONTEXTPROC)bx::dlsym(m_opengl32dll, "wglCreateContext");
			BGFX_FATAL(NULL != wglCreateContext, Fatal::UnableToInitialize, "Failed get wglCreateContext.");

			wglDeleteContext = (PFNWGLDELETECONTEXTPROC)bx::dlsym(m_opengl32dll, "wglDeleteContext");
			BGFX_FATAL(NULL != wglDeleteContext, Fatal::UnableToInitialize, "Failed get wglDeleteContext.");

			m_hdc = GetDC(g_bgfxHwnd);
			BGFX_FATAL(NULL != m_hdc, Fatal::UnableToInitialize, "GetDC failed!");

			// Dummy window to peek into WGL functionality.
			//
			// An application can only set the pixel format of a window one time.
			// Once a window's pixel format is set, it cannot be changed.
			// MSDN: http://msdn.microsoft.com/en-us/library/windows/desktop/dd369049%28v=vs.85%29.aspx
			HWND hwnd = CreateWindowA("STATIC"
				, ""
				, WS_POPUP|WS_DISABLED
				, -32000
				, -32000
				, 0
				, 0
				, NULL
				, NULL
				, GetModuleHandle(NULL)
				, 0
				);

			HDC hdc = GetDC(hwnd);
			BGFX_FATAL(NULL != hdc, Fatal::UnableToInitialize, "GetDC failed!");

			HGLRC context = createContext(hdc);

			wglGetExtensionsStringARB  = (PFNWGLGETEXTENSIONSSTRINGARBPROC)wglGetProcAddress("wglGetExtensionsStringARB");
			wglChoosePixelFormatARB    = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
			wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
			wglSwapIntervalEXT         = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");

			if (NULL != wglGetExtensionsStringARB)
			{
				const char* extensions = (const char*)wglGetExtensionsStringARB(hdc);
				BX_TRACE("WGL extensions:");
				dumpExtensions(extensions);
			}

			if (NULL != wglChoosePixelFormatARB
			&&  NULL != wglCreateContextAttribsARB)
			{
				int32_t attrs[] =
				{
					WGL_SAMPLE_BUFFERS_ARB, 0,
					WGL_SAMPLES_ARB, 0,
					WGL_SUPPORT_OPENGL_ARB, true,
					WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
					WGL_DRAW_TO_WINDOW_ARB, true,
					WGL_DOUBLE_BUFFER_ARB, true,
					WGL_COLOR_BITS_ARB, 32,
					WGL_DEPTH_BITS_ARB, 24,
					WGL_STENCIL_BITS_ARB, 8,
					0
				};

				int result;
				uint32_t numFormats = 0;
				do 
				{
					result = wglChoosePixelFormatARB(m_hdc, attrs, NULL, 1, &m_pixelFormat, &numFormats);
					if (0 == result
						||  0 == numFormats)
					{
						attrs[3] >>= 1;
						attrs[1] = attrs[3] == 0 ? 0 : 1;
					}

				} while (0 == numFormats);

				DescribePixelFormat(m_hdc, m_pixelFormat, sizeof(PIXELFORMATDESCRIPTOR), &m_pfd);

				BX_TRACE("Pixel format:\n"
					"\tiPixelType %d\n"
					"\tcColorBits %d\n"
					"\tcAlphaBits %d\n"
					"\tcDepthBits %d\n"
					"\tcStencilBits %d\n"
					, m_pfd.iPixelType
					, m_pfd.cColorBits
					, m_pfd.cAlphaBits
					, m_pfd.cDepthBits
					, m_pfd.cStencilBits
					);

				result = SetPixelFormat(m_hdc, m_pixelFormat, &m_pfd);
				// When window is created by SDL and SDL_WINDOW_OPENGL is set SetPixelFormat
				// will fail. Just warn and continue. In case it failed for some other reason
				// create context will fail and it will error out there.
				BX_WARN(result, "SetPixelFormat failed (last err: 0x%08x)!", GetLastError() );

				uint32_t flags = BGFX_CONFIG_DEBUG ? WGL_CONTEXT_DEBUG_BIT_ARB : 0;
				BX_UNUSED(flags);
				int32_t contextAttrs[9] =
				{
#if BGFX_CONFIG_RENDERER_OPENGL >= 31
					WGL_CONTEXT_MAJOR_VERSION_ARB, BGFX_CONFIG_RENDERER_OPENGL / 10,
					WGL_CONTEXT_MINOR_VERSION_ARB, BGFX_CONFIG_RENDERER_OPENGL % 10,
					WGL_CONTEXT_FLAGS_ARB, flags,
					WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
#else
					WGL_CONTEXT_MAJOR_VERSION_ARB, 2,
					WGL_CONTEXT_MINOR_VERSION_ARB, 1,
					0, 0,
					0, 0,
#endif // BGFX_CONFIG_RENDERER_OPENGL >= 31
					0
				};

				m_context = wglCreateContextAttribsARB(m_hdc, 0, contextAttrs);
				if (NULL == m_context)
				{
					// nVidia doesn't like context profile mask for contexts below 3.2?
					contextAttrs[6] = WGL_CONTEXT_PROFILE_MASK_ARB == contextAttrs[6] ? 0 : contextAttrs[6];
					m_context = wglCreateContextAttribsARB(m_hdc, 0, contextAttrs);
				}
				BGFX_FATAL(NULL != m_context, Fatal::UnableToInitialize, "Failed to create context 0x%08x.", GetLastError() );

				BX_STATIC_ASSERT(sizeof(contextAttrs) == sizeof(m_contextAttrs) );
				memcpy(m_contextAttrs, contextAttrs, sizeof(contextAttrs) );
			}

			wglMakeCurrent(NULL, NULL);
			wglDeleteContext(context);
			DestroyWindow(hwnd);

			if (NULL == m_context)
			{
				m_context = createContext(m_hdc);
			}

			int result = wglMakeCurrent(m_hdc, m_context);
			BGFX_FATAL(0 != result, Fatal::UnableToInitialize, "wglMakeCurrent failed!");

			if (NULL != wglSwapIntervalEXT)
			{
				wglSwapIntervalEXT(0);
			}
		}

		import();
	}

	void GlContext::destroy()
	{
		if (NULL != g_bgfxHwnd)
		{
			wglMakeCurrent(NULL, NULL);

			wglDeleteContext(m_context);
			m_context = NULL;

			ReleaseDC(g_bgfxHwnd, m_hdc);
			m_hdc = NULL;
		}

		bx::dlclose(m_opengl32dll);
		m_opengl32dll = NULL;
	}

	void GlContext::resize(uint32_t /*_width*/, uint32_t /*_height*/, bool _vsync)
	{
		if (NULL != wglSwapIntervalEXT)
		{
			wglSwapIntervalEXT(_vsync ? 1 : 0);
		}
	}

	bool GlContext::isSwapChainSupported()
	{
		return true;
	}

	SwapChainGL* GlContext::createSwapChain(void* _nwh)
	{
		SwapChainGL* swapChain = BX_NEW(g_allocator, SwapChainGL)(_nwh);

		int result = SetPixelFormat(swapChain->m_hdc, m_pixelFormat, &m_pfd);
		BX_WARN(result, "SetPixelFormat failed (last err: 0x%08x)!", GetLastError() ); BX_UNUSED(result);

		swapChain->m_context = wglCreateContextAttribsARB(swapChain->m_hdc, m_context, m_contextAttrs);
		BX_CHECK(NULL != swapChain->m_context, "Create swap chain failed: %x", glGetError() );
		return swapChain;
	}

	void GlContext::destorySwapChain(SwapChainGL*  _swapChain)
	{
		BX_DELETE(g_allocator, _swapChain);
	}

	void GlContext::makeCurrent(SwapChainGL* _swapChain)
	{
		if (NULL == _swapChain)
		{
			wglMakeCurrent(m_hdc, m_context);
		}
		else
		{
			_swapChain->makeCurrent();
		}
	}

	void GlContext::swap(SwapChainGL* _swapChain)
	{
		if (NULL == _swapChain)
		{
			if (NULL != g_bgfxHwnd)
			{
				wglMakeCurrent(m_hdc, m_context);
				SwapBuffers(m_hdc);
			}
		}
		else
		{
			_swapChain->makeCurrent();
			_swapChain->swapBuffers();
		}
	}

	void GlContext::import()
	{
		BX_TRACE("Import:");
#	define GL_EXTENSION(_optional, _proto, _func, _import) \
				{ \
					if (NULL == _func) \
					{ \
						_func = (_proto)wglGetProcAddress(#_import); \
						if (_func == NULL) \
						{ \
							_func = (_proto)bx::dlsym(m_opengl32dll, #_import); \
							BX_TRACE("    %p " #_func " (" #_import ")", _func); \
						} \
						else \
						{ \
							BX_TRACE("wgl %p " #_func " (" #_import ")", _func); \
						} \
						BGFX_FATAL(BX_IGNORE_C4127(_optional) || NULL != _func, Fatal::UnableToInitialize, "Failed to create OpenGL context. wglGetProcAddress(\"%s\")", #_import); \
					} \
				}
#	include "glimports.h"
	}

} // namespace bgfx

#	endif // BGFX_USE_WGL
#endif // (BGFX_CONFIG_RENDERER_OPENGLES|BGFX_CONFIG_RENDERER_OPENGL)
/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "bgfx_p.h"
#include <math.h> // powf, sqrtf

#include "image.h"

namespace bgfx
{
	static const ImageBlockInfo s_imageBlockInfo[] =
	{
		{   4, 4, 4,  8 }, // BC1
		{   8, 4, 4, 16 }, // BC2
		{   8, 4, 4, 16 }, // BC3
		{   4, 4, 4,  8 }, // BC4
		{   8, 4, 4, 16 }, // BC5
		{   8, 4, 4, 16 }, // BC6H
		{   8, 4, 4, 16 }, // BC7
		{   4, 4, 4,  8 }, // ETC1
		{   4, 4, 4,  8 }, // ETC2
		{   8, 4, 4, 16 }, // ETC2A
		{   4, 4, 4,  8 }, // ETC2A1
		{   2, 8, 4,  8 }, // PTC12
		{   4, 4, 4,  8 }, // PTC14
		{   2, 8, 4,  8 }, // PTC12A
		{   4, 4, 4,  8 }, // PTC14A
		{   2, 8, 4,  8 }, // PTC22
		{   4, 4, 4,  8 }, // PTC24
		{   0, 0, 0,  0 }, // Unknown
		{   1, 8, 1,  1 }, // R1
		{   8, 1, 1,  1 }, // R8
		{  16, 1, 1,  2 }, // R16
		{  16, 1, 1,  2 }, // R16F
		{  32, 1, 1,  4 }, // R32
		{  32, 1, 1,  4 }, // R32F
		{  16, 1, 1,  2 }, // RG8
		{  32, 1, 1,  4 }, // RG16
		{  32, 1, 1,  4 }, // RG16F
		{  64, 1, 1,  8 }, // RG32
		{  64, 1, 1,  8 }, // RG32F
		{  32, 1, 1,  4 }, // BGRA8
		{  64, 1, 1,  8 }, // RGBA16
		{  64, 1, 1,  8 }, // RGBA16F
		{ 128, 1, 1, 16 }, // RGBA32
		{ 128, 1, 1, 16 }, // RGBA32F
		{  16, 1, 1,  2 }, // R5G6B5
		{  16, 1, 1,  2 }, // RGBA4
		{  16, 1, 1,  2 }, // RGB5A1
		{  32, 1, 1,  4 }, // RGB10A2
		{  32, 1, 1,  4 }, // R11G11B10F
		{   0, 0, 0,  0 }, // UnknownDepth
		{  16, 1, 1,  2 }, // D16
		{  24, 1, 1,  3 }, // D24
		{  32, 1, 1,  4 }, // D24S8
		{  32, 1, 1,  4 }, // D32
		{  16, 1, 1,  2 }, // D16F
		{  24, 1, 1,  3 }, // D24F
		{  32, 1, 1,  4 }, // D32F
		{   8, 1, 1,  1 }, // D0S8
	};
	BX_STATIC_ASSERT(TextureFormat::Count == BX_COUNTOF(s_imageBlockInfo) );

	static const char* s_textureFormatName[] =
	{
		"BC1",        // BC1
		"BC2",        // BC2
		"BC3",        // BC3
		"BC4",        // BC4
		"BC5",        // BC5
		"BC6H",       // BC6H
		"BC7",        // BC7
		"ETC1",       // ETC1
		"ETC2",       // ETC2
		"ETC2A",      // ETC2A
		"ETC2A1",     // ETC2A1
		"PTC12",      // PTC12
		"PTC14",      // PTC14
		"PTC12A",     // PTC12A
		"PTC14A",     // PTC14A
		"PTC22",      // PTC22
		"PTC24",      // PTC24
		"<unknown>",  // Unknown
		"R1",         // R1
		"R8",         // R8
		"R16",        // R16
		"R16F",       // R16F
		"R32",        // R32
		"R32F",       // R32F
		"RG8",        // RG8
		"RG16",       // RG16
		"RG16F",      // RG16F
		"RG32",       // RG32
		"RG32F",      // RG32F
		"BGRA8",      // BGRA8
		"RGBA16",     // RGBA16
		"RGBA16F",    // RGBA16F
		"RGBA32",     // RGBA32
		"RGBA32F",    // RGBA32F
		"R5G6B5",     // R5G6B5
		"RGBA4",      // RGBA4
		"RGB5A1",     // RGB5A1
		"RGB10A2",    // RGB10A2
		"R11G11B10F", // R11G11B10F
		"<unknown>",  // UnknownDepth
		"D16",        // D16
		"D24",        // D24
		"D24S8",      // D24S8
		"D32",        // D32
		"D16F",       // D16F
		"D24F",       // D24F
		"D32F",       // D32F
		"D0S8",       // D0S8
	};
	BX_STATIC_ASSERT(TextureFormat::Count == BX_COUNTOF(s_textureFormatName) );

	bool isCompressed(TextureFormat::Enum _format)
	{
		return _format < TextureFormat::Unknown;
	}

	bool isColor(TextureFormat::Enum _format)
	{
		return _format > TextureFormat::Unknown
			&& _format < TextureFormat::UnknownDepth
			;
	}

	bool isDepth(TextureFormat::Enum _format)
	{
		return _format > TextureFormat::UnknownDepth
			&& _format < TextureFormat::Count
			;
	}

	uint8_t getBitsPerPixel(TextureFormat::Enum _format)
	{
		return s_imageBlockInfo[_format].bitsPerPixel;
	}

	const ImageBlockInfo& getBlockInfo(TextureFormat::Enum _format)
	{
		return s_imageBlockInfo[_format];
	}

	uint8_t getBlockSize(TextureFormat::Enum _format)
	{
		return s_imageBlockInfo[_format].blockSize;
	}

	const char* getName(TextureFormat::Enum _format)
	{
		return s_textureFormatName[_format];
	}

	void imageSolid(uint32_t _width, uint32_t _height, uint32_t _solid, void* _dst)
	{
		uint32_t* dst = (uint32_t*)_dst;
		for (uint32_t ii = 0, num = _width*_height; ii < num; ++ii)
		{
			*dst++ = _solid;
		}
	}

	void imageCheckerboard(uint32_t _width, uint32_t _height, uint32_t _step, uint32_t _0, uint32_t _1, void* _dst)
	{
		uint32_t* dst = (uint32_t*)_dst;
		for (uint32_t yy = 0; yy < _height; ++yy)
		{
			for (uint32_t xx = 0; xx < _width; ++xx)
			{
				uint32_t abgr = ( (xx/_step)&1) ^ ( (yy/_step)&1) ? _1 : _0;
				*dst++ = abgr;
			}
		}
	}

	void imageRgba8Downsample2x2Ref(uint32_t _width, uint32_t _height, uint32_t _srcPitch, const void* _src, void* _dst)
	{
		const uint32_t dstwidth  = _width/2;
		const uint32_t dstheight = _height/2;

		if (0 == dstwidth
		||  0 == dstheight)
		{
			return;
		}

		uint8_t* dst = (uint8_t*)_dst;
		const uint8_t* src = (const uint8_t*)_src;
		
		for (uint32_t yy = 0, ystep = _srcPitch*2; yy < dstheight; ++yy, src += ystep)
		{
			const uint8_t* rgba = src;
			for (uint32_t xx = 0; xx < dstwidth; ++xx, rgba += 8, dst += 4)
			{
				float rr = powf(rgba[          0], 2.2f);
				float gg = powf(rgba[          1], 2.2f);
				float bb = powf(rgba[          2], 2.2f);
				float aa =      rgba[          3];
				rr      += powf(rgba[          4], 2.2f);
				gg      += powf(rgba[          5], 2.2f);
				bb      += powf(rgba[          6], 2.2f);
				aa      +=      rgba[          7];
				rr      += powf(rgba[_srcPitch+0], 2.2f);
				gg      += powf(rgba[_srcPitch+1], 2.2f);
				bb      += powf(rgba[_srcPitch+2], 2.2f);
				aa      +=      rgba[_srcPitch+3];
				rr      += powf(rgba[_srcPitch+4], 2.2f);
				gg      += powf(rgba[_srcPitch+5], 2.2f);
				bb      += powf(rgba[_srcPitch+6], 2.2f);
				aa      +=      rgba[_srcPitch+7];

				rr *= 0.25f;
				gg *= 0.25f;
				bb *= 0.25f;
				aa *= 0.25f;
				rr = powf(rr, 1.0f/2.2f);
				gg = powf(gg, 1.0f/2.2f);
				bb = powf(bb, 1.0f/2.2f);
				dst[0] = (uint8_t)rr;
				dst[1] = (uint8_t)gg;
				dst[2] = (uint8_t)bb;
				dst[3] = (uint8_t)aa;
			}
		}
	}

	void imageRgba8Downsample2x2(uint32_t _width, uint32_t _height, uint32_t _srcPitch, const void* _src, void* _dst)
	{
		const uint32_t dstwidth  = _width/2;
		const uint32_t dstheight = _height/2;

		if (0 == dstwidth
		||  0 == dstheight)
		{
			return;
		}

		uint8_t* dst = (uint8_t*)_dst;
		const uint8_t* src = (const uint8_t*)_src;

		using namespace bx;
		const float4_t unpack = float4_ld(1.0f, 1.0f/256.0f, 1.0f/65536.0f, 1.0f/16777216.0f);
		const float4_t pack   = float4_ld(1.0f, 256.0f*0.5f, 65536.0f, 16777216.0f*0.5f);
		const float4_t umask  = float4_ild(0xff, 0xff00, 0xff0000, 0xff000000);
		const float4_t pmask  = float4_ild(0xff, 0x7f80, 0xff0000, 0x7f800000);
		const float4_t wflip  = float4_ild(0, 0, 0, 0x80000000);
		const float4_t wadd   = float4_ld(0.0f, 0.0f, 0.0f, 32768.0f*65536.0f);
		const float4_t gamma  = float4_ld(1.0f/2.2f, 1.0f/2.2f, 1.0f/2.2f, 1.0f);
		const float4_t linear = float4_ld(2.2f, 2.2f, 2.2f, 1.0f);
		const float4_t quater = float4_splat(0.25f);

		for (uint32_t yy = 0, ystep = _srcPitch*2; yy < dstheight; ++yy, src += ystep)
		{
			const uint8_t* rgba = src;
			for (uint32_t xx = 0; xx < dstwidth; ++xx, rgba += 8, dst += 4)
			{
				const float4_t abgr0  = float4_splat(rgba);
				const float4_t abgr1  = float4_splat(rgba+4);
				const float4_t abgr2  = float4_splat(rgba+_srcPitch);
				const float4_t abgr3  = float4_splat(rgba+_srcPitch+4);

				const float4_t abgr0m = float4_and(abgr0, umask);
				const float4_t abgr1m = float4_and(abgr1, umask);
				const float4_t abgr2m = float4_and(abgr2, umask);
				const float4_t abgr3m = float4_and(abgr3, umask);
				const float4_t abgr0x = float4_xor(abgr0m, wflip);
				const float4_t abgr1x = float4_xor(abgr1m, wflip);
				const float4_t abgr2x = float4_xor(abgr2m, wflip);
				const float4_t abgr3x = float4_xor(abgr3m, wflip);
				const float4_t abgr0f = float4_itof(abgr0x);
				const float4_t abgr1f = float4_itof(abgr1x);
				const float4_t abgr2f = float4_itof(abgr2x);
				const float4_t abgr3f = float4_itof(abgr3x);
				const float4_t abgr0c = float4_add(abgr0f, wadd);
				const float4_t abgr1c = float4_add(abgr1f, wadd);
				const float4_t abgr2c = float4_add(abgr2f, wadd);
				const float4_t abgr3c = float4_add(abgr3f, wadd);
				const float4_t abgr0n = float4_mul(abgr0c, unpack);
				const float4_t abgr1n = float4_mul(abgr1c, unpack);
				const float4_t abgr2n = float4_mul(abgr2c, unpack);
				const float4_t abgr3n = float4_mul(abgr3c, unpack);

				const float4_t abgr0l = float4_pow(abgr0n, linear);
				const float4_t abgr1l = float4_pow(abgr1n, linear);
				const float4_t abgr2l = float4_pow(abgr2n, linear);
				const float4_t abgr3l = float4_pow(abgr3n, linear);

				const float4_t sum0   = float4_add(abgr0l, abgr1l);
				const float4_t sum1   = float4_add(abgr2l, abgr3l);
				const float4_t sum2   = float4_add(sum0, sum1);
				const float4_t avg0   = float4_mul(sum2, quater);
				const float4_t avg1   = float4_pow(avg0, gamma);

				const float4_t avg2   = float4_mul(avg1, pack);
				const float4_t ftoi0  = float4_ftoi(avg2);
				const float4_t ftoi1  = float4_and(ftoi0, pmask);
				const float4_t zwxy   = float4_swiz_zwxy(ftoi1);
				const float4_t tmp0   = float4_or(ftoi1, zwxy);
				const float4_t yyyy   = float4_swiz_yyyy(tmp0);
				const float4_t tmp1   = float4_iadd(yyyy, yyyy);
				const float4_t result = float4_or(tmp0, tmp1);

				float4_stx(dst, result);
			}
		}
	}

	void imageSwizzleBgra8Ref(uint32_t _width, uint32_t _height, uint32_t _srcPitch, const void* _src, void* _dst)
	{
		const uint8_t* src = (uint8_t*) _src;
		const uint8_t* next = src + _srcPitch;
		uint8_t* dst = (uint8_t*)_dst;

		for (uint32_t yy = 0; yy < _height; ++yy, src = next, next += _srcPitch)
		{
			for (uint32_t xx = 0; xx < _width; ++xx, src += 4, dst += 4)
			{
				uint8_t rr = src[0];
				uint8_t gg = src[1];
				uint8_t bb = src[2];
				uint8_t aa = src[3];
				dst[0] = bb;
				dst[1] = gg;
				dst[2] = rr;
				dst[3] = aa;
			}
		}
	}

	void imageSwizzleBgra8(uint32_t _width, uint32_t _height, uint32_t _srcPitch, const void* _src, void* _dst)
	{
		// Test can we do four 4-byte pixels at the time.
		if (0 != (_width&0x3)
		||  _width < 4
		||  !bx::isPtrAligned(_src, 16)
		||  !bx::isPtrAligned(_dst, 16) )
		{
			BX_WARN(false, "Image swizzle is taking slow path.");
			BX_WARN(bx::isPtrAligned(_src, 16), "Source %p is not 16-byte aligned.", _src);
			BX_WARN(bx::isPtrAligned(_dst, 16), "Destination %p is not 16-byte aligned.", _dst);
			BX_WARN(_width < 4, "Image width must be multiple of 4 (width %d).", _width);
			imageSwizzleBgra8Ref(_width, _height, _srcPitch, _src, _dst);
			return;
		}

		using namespace bx;

		const float4_t mf0f0 = float4_isplat(0xff00ff00);
		const float4_t m0f0f = float4_isplat(0x00ff00ff);
		const uint8_t* src = (uint8_t*) _src;
		const uint8_t* next = src + _srcPitch;
		uint8_t* dst = (uint8_t*)_dst;

		const uint32_t width = _width/4;

		for (uint32_t yy = 0; yy < _height; ++yy, src = next, next += _srcPitch)
		{
			for (uint32_t xx = 0; xx < width; ++xx, src += 16, dst += 16)
			{
				const float4_t tabgr = float4_ld(src);
				const float4_t t00ab = float4_srl(tabgr, 16);
				const float4_t tgr00 = float4_sll(tabgr, 16);
				const float4_t tgrab = float4_or(t00ab, tgr00);
				const float4_t ta0g0 = float4_and(tabgr, mf0f0);
				const float4_t t0r0b = float4_and(tgrab, m0f0f);
				const float4_t targb = float4_or(ta0g0, t0r0b);
				float4_st(dst, targb);
			}
		}
	}

	void imageCopy(uint32_t _width, uint32_t _height, uint32_t _bpp, uint32_t _srcPitch, const void* _src, void* _dst)
	{
		const uint32_t pitch = _width*_bpp/8;
		const uint8_t* src = (uint8_t*) _src;
		const uint8_t* next = src + _srcPitch;
		uint8_t* dst = (uint8_t*)_dst;

		for (uint32_t yy = 0; yy < _height; ++yy, src = next, next += _srcPitch, dst += pitch)
		{
			memcpy(dst, src, pitch);
		}
	}

	void imageWriteTga(bx::WriterI* _writer, uint32_t _width, uint32_t _height, uint32_t _srcPitch, const void* _src, bool _grayscale, bool _yflip)
	{
		uint8_t type = _grayscale ? 3 : 2;
		uint8_t bpp = _grayscale ? 8 : 32;

		uint8_t header[18] = {};
		header[2] = type;
		header[12] = _width&0xff;
		header[13] = (_width>>8)&0xff;
		header[14] = _height&0xff;
		header[15] = (_height>>8)&0xff;
		header[16] = bpp;
		header[17] = 32;

		bx::write(_writer, header, sizeof(header) );

		uint32_t dstPitch = _width*bpp/8;
		if (_yflip)
		{
			uint8_t* data = (uint8_t*)_src + _srcPitch*_height - _srcPitch;
			for (uint32_t yy = 0; yy < _height; ++yy)
			{
				bx::write(_writer, data, dstPitch);
				data -= _srcPitch;
			}
		}
		else if (_srcPitch == dstPitch)
		{
			bx::write(_writer, _src, _height*_srcPitch);
		}
		else
		{
			uint8_t* data = (uint8_t*)_src;
			for (uint32_t yy = 0; yy < _height; ++yy)
			{
				bx::write(_writer, data, dstPitch);
				data += _srcPitch;
			}
		}
	}

	uint32_t bitRangeConvert(uint32_t _in, uint32_t _from, uint32_t _to)
	{
		using namespace bx;
		uint32_t tmp0   = uint32_sll(1, _to);
		uint32_t tmp1   = uint32_sll(1, _from);
		uint32_t tmp2   = uint32_dec(tmp0);
		uint32_t tmp3   = uint32_dec(tmp1);
		uint32_t tmp4   = uint32_mul(_in, tmp2);
		uint32_t tmp5   = uint32_add(tmp3, tmp4);
		uint32_t tmp6   = uint32_srl(tmp5, _from);
		uint32_t tmp7   = uint32_add(tmp5, tmp6);
		uint32_t result = uint32_srl(tmp7, _from);

		return result;
	}

	void decodeBlockDxt(uint8_t _dst[16*4], const uint8_t _src[8])
	{
		uint8_t colors[4*3];

		uint32_t c0 = _src[0] | (_src[1] << 8);
		colors[0] = bitRangeConvert( (c0>> 0)&0x1f, 5, 8);
		colors[1] = bitRangeConvert( (c0>> 5)&0x3f, 6, 8);
		colors[2] = bitRangeConvert( (c0>>11)&0x1f, 5, 8);

		uint32_t c1 = _src[2] | (_src[3] << 8);
		colors[3] = bitRangeConvert( (c1>> 0)&0x1f, 5, 8);
		colors[4] = bitRangeConvert( (c1>> 5)&0x3f, 6, 8);
		colors[5] = bitRangeConvert( (c1>>11)&0x1f, 5, 8);

		colors[6] = (2*colors[0] + colors[3]) / 3;
		colors[7] = (2*colors[1] + colors[4]) / 3;
		colors[8] = (2*colors[2] + colors[5]) / 3;

		colors[ 9] = (colors[0] + 2*colors[3]) / 3;
		colors[10] = (colors[1] + 2*colors[4]) / 3;
		colors[11] = (colors[2] + 2*colors[5]) / 3;

		for (uint32_t ii = 0, next = 8*4; ii < 16*4; ii += 4, next += 2)
		{
			int idx = ( (_src[next>>3] >> (next & 7) ) & 3) * 3;
			_dst[ii+0] = colors[idx+0];
			_dst[ii+1] = colors[idx+1];
			_dst[ii+2] = colors[idx+2];
		}
	}

	void decodeBlockDxt1(uint8_t _dst[16*4], const uint8_t _src[8])
	{
		uint8_t colors[4*4];

		uint32_t c0 = _src[0] | (_src[1] << 8);
		colors[0] = bitRangeConvert( (c0>> 0)&0x1f, 5, 8);
		colors[1] = bitRangeConvert( (c0>> 5)&0x3f, 6, 8);
		colors[2] = bitRangeConvert( (c0>>11)&0x1f, 5, 8);
		colors[3] = 255;

		uint32_t c1 = _src[2] | (_src[3] << 8);
		colors[4] = bitRangeConvert( (c1>> 0)&0x1f, 5, 8);
		colors[5] = bitRangeConvert( (c1>> 5)&0x3f, 6, 8);
		colors[6] = bitRangeConvert( (c1>>11)&0x1f, 5, 8);
		colors[7] = 255;

		if (c0 > c1)
		{
			colors[ 8] = (2*colors[0] + colors[4]) / 3;
			colors[ 9] = (2*colors[1] + colors[5]) / 3;
			colors[10] = (2*colors[2] + colors[6]) / 3;
			colors[11] = 255;

			colors[12] = (colors[0] + 2*colors[4]) / 3;
			colors[13] = (colors[1] + 2*colors[5]) / 3;
			colors[14] = (colors[2] + 2*colors[6]) / 3;
			colors[15] = 255;
		}
		else
		{
			colors[ 8] = (colors[0] + colors[4]) / 2;
			colors[ 9] = (colors[1] + colors[5]) / 2;
			colors[10] = (colors[2] + colors[6]) / 2;
			colors[11] = 255;
			
			colors[12] = 0;
			colors[13] = 0;
			colors[14] = 0;
			colors[15] = 0;
		}

		for (uint32_t ii = 0, next = 8*4; ii < 16*4; ii += 4, next += 2)
		{
			int idx = ( (_src[next>>3] >> (next & 7) ) & 3) * 4;
			_dst[ii+0] = colors[idx+0];
			_dst[ii+1] = colors[idx+1];
			_dst[ii+2] = colors[idx+2];
			_dst[ii+3] = colors[idx+3];
		}
	}

	void decodeBlockDxt23A(uint8_t _dst[16*4], const uint8_t _src[8])
	{
		for (uint32_t ii = 0, next = 0; ii < 16*4; ii += 4, next += 4)
		{
			uint32_t c0 = (_src[next>>3] >> (next&7) ) & 0xf;
			_dst[ii] = bitRangeConvert(c0, 4, 8);
		}
	}

	void decodeBlockDxt45A(uint8_t _dst[16*4], const uint8_t _src[8])
	{
		uint8_t alpha[8];
		alpha[0] = _src[0];
		alpha[1] = _src[1];

		if (alpha[0] > alpha[1])
		{
			alpha[2] = (6*alpha[0] + 1*alpha[1]) / 7;
			alpha[3] = (5*alpha[0] + 2*alpha[1]) / 7;
			alpha[4] = (4*alpha[0] + 3*alpha[1]) / 7;
			alpha[5] = (3*alpha[0] + 4*alpha[1]) / 7;
			alpha[6] = (2*alpha[0] + 5*alpha[1]) / 7;
			alpha[7] = (1*alpha[0] + 6*alpha[1]) / 7;
		}
		else
		{
			alpha[2] = (4*alpha[0] + 1*alpha[1]) / 5;
			alpha[3] = (3*alpha[0] + 2*alpha[1]) / 5;
			alpha[4] = (2*alpha[0] + 3*alpha[1]) / 5;
			alpha[5] = (1*alpha[0] + 4*alpha[1]) / 5;
			alpha[6] = 0;
			alpha[7] = 255;
		}

		uint32_t idx0 = _src[2];
		uint32_t idx1 = _src[5];
		idx0 |= uint32_t(_src[3])<<8;
		idx1 |= uint32_t(_src[6])<<8;
		idx0 |= uint32_t(_src[4])<<16;
		idx1 |= uint32_t(_src[7])<<16;
		for (uint32_t ii = 0; ii < 8*4; ii += 4)
		{
			_dst[ii]    = alpha[idx0&7];
			_dst[ii+32] = alpha[idx1&7];
			idx0 >>= 3;
			idx1 >>= 3;
		}
	}

	static const int32_t s_etc1Mod[8][4] =
	{
		{  2,   8,  -2,   -8},
		{  5,  17,  -5,  -17},
		{  9,  29,  -9,  -29},
		{ 13,  42, -13,  -42},
		{ 18,  60, -18,  -60},
		{ 24,  80, -24,  -80},
		{ 33, 106, -33, -106},
		{ 47, 183, -47, -183},
	};

	static const uint8_t s_etc2Mod[8] = { 3, 6, 11, 16, 23, 32, 41, 64 };

	uint8_t uint8_sat(int32_t _a)
	{
		using namespace bx;
		const uint32_t min    = uint32_imin(_a, 255);
		const uint32_t result = uint32_imax(min, 0);
		return (uint8_t)result;
	}

	uint8_t uint8_satadd(int32_t _a, int32_t _b)
	{
		const int32_t add = _a + _b;
		return uint8_sat(add);
	}

	void decodeBlockEtc2ModeT(uint8_t _dst[16*4], const uint8_t _src[8])
	{
		uint8_t rgb[16];

		// 0       1       2       3       4       5       6       7
		// 7654321076543210765432107654321076543210765432107654321076543210
		// ...rr.rrggggbbbbrrrrggggbbbbDDD.mmmmmmmmmmmmmmmmllllllllllllllll
		//    ^            ^           ^   ^               ^
		//    +-- c0       +-- c1      |   +-- msb         +-- lsb
		//                             +-- dist

		rgb[ 0] = ( (_src[0] >> 1) & 0xc)
			    | (_src[0] & 0x3)
			    ;
		rgb[ 1] = _src[1] >> 4;
		rgb[ 2] = _src[1] & 0xf;

		rgb[ 8] = _src[2] >> 4;
		rgb[ 9] = _src[2] & 0xf;
		rgb[10] = _src[3] >> 4;

		rgb[ 0] = bitRangeConvert(rgb[ 0], 4, 8);
		rgb[ 1] = bitRangeConvert(rgb[ 1], 4, 8);
		rgb[ 2] = bitRangeConvert(rgb[ 2], 4, 8);
		rgb[ 8] = bitRangeConvert(rgb[ 8], 4, 8);
		rgb[ 9] = bitRangeConvert(rgb[ 9], 4, 8);
		rgb[10] = bitRangeConvert(rgb[10], 4, 8);

		uint8_t dist = (_src[3] >> 1) & 0x7;
		int32_t mod = s_etc2Mod[dist];

		rgb[ 4] = uint8_satadd(rgb[ 8],  mod);
		rgb[ 5] = uint8_satadd(rgb[ 9],  mod);
		rgb[ 6] = uint8_satadd(rgb[10],  mod);

		rgb[12] = uint8_satadd(rgb[ 8], -mod);
		rgb[13] = uint8_satadd(rgb[ 9], -mod);
		rgb[14] = uint8_satadd(rgb[10], -mod);

		uint32_t indexMsb = (_src[4]<<8) | _src[5];
		uint32_t indexLsb = (_src[6]<<8) | _src[7];

		for (uint32_t ii = 0; ii < 16; ++ii)
		{
			const uint32_t idx  = (ii&0xc) | ( (ii & 0x3)<<4);
			const uint32_t lsbi = indexLsb & 1;
			const uint32_t msbi = (indexMsb & 1)<<1;
			const uint32_t pal  = (lsbi | msbi)<<2;

			_dst[idx + 0] = rgb[pal+2];
			_dst[idx + 1] = rgb[pal+1];
			_dst[idx + 2] = rgb[pal+0];
			_dst[idx + 3] = 255;

			indexLsb >>= 1;
			indexMsb >>= 1;
		}
	}

	void decodeBlockEtc2ModeH(uint8_t _dst[16*4], const uint8_t _src[8])
	{
		uint8_t rgb[16];

		// 0       1       2       3       4       5       6       7
		// 7654321076543210765432107654321076543210765432107654321076543210
		// .rrrrggg...gb.bbbrrrrggggbbbbDD.mmmmmmmmmmmmmmmmllllllllllllllll
		//  ^               ^           ^  ^               ^
		//  +-- c0          +-- c1      |  +-- msb         +-- lsb
		//                              +-- dist

		rgb[ 0] = (_src[0] >> 3) & 0xf;
		rgb[ 1] = ( (_src[0] << 1) & 0xe)
				| ( (_src[1] >> 4) & 0x1)
				;
		rgb[ 2] = (_src[1] & 0x8)
				| ( (_src[1] << 1) & 0x6)
				| (_src[2] >> 7)
				;

		rgb[ 8] = (_src[2] >> 3) & 0xf;
		rgb[ 9] = ( (_src[2] << 1) & 0xe)
				| (_src[3] >> 7)
				;
		rgb[10] = (_src[2] >> 3) & 0xf;

		rgb[ 0] = bitRangeConvert(rgb[ 0], 4, 8);
		rgb[ 1] = bitRangeConvert(rgb[ 1], 4, 8);
		rgb[ 2] = bitRangeConvert(rgb[ 2], 4, 8);
		rgb[ 8] = bitRangeConvert(rgb[ 8], 4, 8);
		rgb[ 9] = bitRangeConvert(rgb[ 9], 4, 8);
		rgb[10] = bitRangeConvert(rgb[10], 4, 8);

		uint32_t col0 = uint32_t(rgb[0]<<16) | uint32_t(rgb[1]<<8) | uint32_t(rgb[ 2]);
		uint32_t col1 = uint32_t(rgb[8]<<16) | uint32_t(rgb[9]<<8) | uint32_t(rgb[10]);
		uint8_t dist = (_src[3] & 0x6) | (col0 >= col1);
		int32_t mod = s_etc2Mod[dist];

		rgb[ 4] = uint8_satadd(rgb[ 0], -mod);
		rgb[ 5] = uint8_satadd(rgb[ 1], -mod);
		rgb[ 6] = uint8_satadd(rgb[ 2], -mod);

		rgb[ 0] = uint8_satadd(rgb[ 0],  mod);
		rgb[ 1] = uint8_satadd(rgb[ 1],  mod);
		rgb[ 2] = uint8_satadd(rgb[ 2],  mod);

		rgb[12] = uint8_satadd(rgb[ 8], -mod);
		rgb[13] = uint8_satadd(rgb[ 9], -mod);
		rgb[14] = uint8_satadd(rgb[10], -mod);

		rgb[ 8] = uint8_satadd(rgb[ 8],  mod);
		rgb[ 9] = uint8_satadd(rgb[ 9],  mod);
		rgb[10] = uint8_satadd(rgb[10],  mod);

		uint32_t indexMsb = (_src[4]<<8) | _src[5];
		uint32_t indexLsb = (_src[6]<<8) | _src[7];

		for (uint32_t ii = 0; ii < 16; ++ii)
		{
			const uint32_t idx  = (ii&0xc) | ( (ii & 0x3)<<4);
			const uint32_t lsbi = indexLsb & 1;
			const uint32_t msbi = (indexMsb & 1)<<1;
			const uint32_t pal  = (lsbi | msbi)<<2;

			_dst[idx + 0] = rgb[pal+2];
			_dst[idx + 1] = rgb[pal+1];
			_dst[idx + 2] = rgb[pal+0];
			_dst[idx + 3] = 255;

			indexLsb >>= 1;
			indexMsb >>= 1;
		}
	}

	void decodeBlockEtc2ModePlanar(uint8_t _dst[16*4], const uint8_t _src[8])
	{
		// 0       1       2       3       4       5       6       7
		// 7654321076543210765432107654321076543210765432107654321076543210
		// .rrrrrrg.ggggggb...bb.bbbrrrrr.rgggggggbbbbbbrrrrrrgggggggbbbbbb
		//  ^                       ^                   ^
		//  +-- c0                  +-- cH              +-- cV

		uint8_t c0[3];
		uint8_t cH[3];
		uint8_t cV[3];

		c0[0] = (_src[0] >> 1) & 0x3f;
		c0[1] = ( (_src[0] & 1) << 6) 
			  | ( (_src[1] >> 1) & 0x3f)
			  ;
		c0[2] = ( (_src[1] & 1) << 5)
			  | ( (_src[2] & 0x18) )
			  | ( (_src[2] << 1) & 6)
			  | ( (_src[3] >> 7) )
			  ;

		cH[0] = ( (_src[3] >> 1) & 0x3e)
			  | (_src[3] & 1)
			  ;
		cH[1] = _src[4] >> 1;
		cH[2] = ( (_src[4] & 1) << 5)
			  | (_src[5] >> 3)
			  ;

		cV[0] = ( (_src[5] & 0x7) << 3)
			  | (_src[6] >> 5)
			  ;
		cV[1] = ( (_src[6] & 0x1f) << 2)
			  | (_src[7] >> 5)
			  ;
		cV[2] = _src[7] & 0x3f;

		c0[0] = bitRangeConvert(c0[0], 6, 8);
		c0[1] = bitRangeConvert(c0[1], 7, 8);
		c0[2] = bitRangeConvert(c0[2], 6, 8);

		cH[0] = bitRangeConvert(cH[0], 6, 8);
		cH[1] = bitRangeConvert(cH[1], 7, 8);
		cH[2] = bitRangeConvert(cH[2], 6, 8);

		cV[0] = bitRangeConvert(cV[0], 6, 8);
		cV[1] = bitRangeConvert(cV[1], 7, 8);
		cV[2] = bitRangeConvert(cV[2], 6, 8);

		int16_t dy[3];
		dy[0] = cV[0] - c0[0];
		dy[1] = cV[1] - c0[1];
		dy[2] = cV[2] - c0[2];

		int16_t sx[3];
		sx[0] = int16_t(c0[0])<<2;
		sx[1] = int16_t(c0[1])<<2;
		sx[2] = int16_t(c0[2])<<2;

		int16_t ex[3];
		ex[0] = int16_t(cH[0])<<2;
		ex[1] = int16_t(cH[1])<<2;
		ex[2] = int16_t(cH[2])<<2;

		for (int32_t vv = 0; vv < 4; ++vv)
		{
			int16_t dx[3];
			dx[0] = (ex[0] - sx[0])>>2;
			dx[1] = (ex[1] - sx[1])>>2;
			dx[2] = (ex[2] - sx[2])>>2;

			for (int32_t hh = 0; hh < 4; ++hh)
			{
				const uint32_t idx  = (vv<<4) + (hh<<2);

				_dst[idx + 0] = uint8_sat( (sx[2] + dx[2]*hh)>>2);
				_dst[idx + 1] = uint8_sat( (sx[1] + dx[1]*hh)>>2);
				_dst[idx + 2] = uint8_sat( (sx[0] + dx[0]*hh)>>2);
				_dst[idx + 3] = 255;
			}

			sx[0] += dy[0];
			sx[1] += dy[1];
			sx[2] += dy[2];

			ex[0] += dy[0];
			ex[1] += dy[1];
			ex[2] += dy[2];
		}
	}

	void decodeBlockEtc12(uint8_t _dst[16*4], const uint8_t _src[8])
	{
		bool flipBit = 0 != (_src[3] & 0x1);
		bool diffBit = 0 != (_src[3] & 0x2);

		uint8_t rgb[8];

		if (diffBit)
		{
			rgb[0]  = _src[0] >> 3;
			rgb[1]  = _src[1] >> 3;
			rgb[2]  = _src[2] >> 3;

			int8_t diff[3];
			diff[0] = int8_t( (_src[0] & 0x7)<<5)>>5;
			diff[1] = int8_t( (_src[1] & 0x7)<<5)>>5;
			diff[2] = int8_t( (_src[2] & 0x7)<<5)>>5;

			int8_t rr = rgb[0] + diff[0];
			int8_t gg = rgb[1] + diff[1];
			int8_t bb = rgb[2] + diff[2];

			// Etc2 3-modes
			if (rr < 0 || rr > 31)
			{
				decodeBlockEtc2ModeT(_dst, _src);
				return;
			}
			if (gg < 0 || gg > 31)
			{
				decodeBlockEtc2ModeH(_dst, _src);
				return;
			}
			if (bb < 0 || bb > 31)
			{
				decodeBlockEtc2ModePlanar(_dst, _src);
				return;
			}

			// Etc1
			rgb[0] = bitRangeConvert(rgb[0], 5, 8);
			rgb[1] = bitRangeConvert(rgb[1], 5, 8);
			rgb[2] = bitRangeConvert(rgb[2], 5, 8);
			rgb[4] = bitRangeConvert(rr, 5, 8);
			rgb[5] = bitRangeConvert(gg, 5, 8);
			rgb[6] = bitRangeConvert(bb, 5, 8);
		}
		else
		{
			rgb[0] = _src[0] >> 4;
			rgb[1] = _src[1] >> 4;
			rgb[2] = _src[2] >> 4;

			rgb[4] = _src[0] & 0xf;
			rgb[5] = _src[1] & 0xf;
			rgb[6] = _src[2] & 0xf;

			rgb[0] = bitRangeConvert(rgb[0], 4, 8);
			rgb[1] = bitRangeConvert(rgb[1], 4, 8);
			rgb[2] = bitRangeConvert(rgb[2], 4, 8);
			rgb[4] = bitRangeConvert(rgb[4], 4, 8);
			rgb[5] = bitRangeConvert(rgb[5], 4, 8);
			rgb[6] = bitRangeConvert(rgb[6], 4, 8);
		}

		uint32_t table[2];
		table[0] = (_src[3] >> 5) & 0x7;
		table[1] = (_src[3] >> 2) & 0x7;

		uint32_t indexMsb = (_src[4]<<8) | _src[5];
		uint32_t indexLsb = (_src[6]<<8) | _src[7];

		if (flipBit)
		{
			for (uint32_t ii = 0; ii < 16; ++ii)
			{
				const uint32_t block = (ii>>1)&1;
				const uint32_t color = block<<2;
				const uint32_t idx   = (ii&0xc) | ( (ii & 0x3)<<4);
				const uint32_t lsbi  = indexLsb & 1;
				const uint32_t msbi  = (indexMsb & 1)<<1;
				const  int32_t mod   = s_etc1Mod[table[block] ][lsbi | msbi];

				_dst[idx + 0] = uint8_satadd(rgb[color+2], mod);
				_dst[idx + 1] = uint8_satadd(rgb[color+1], mod);
				_dst[idx + 2] = uint8_satadd(rgb[color+0], mod);
				_dst[idx + 3] = 255;

				indexLsb >>= 1;
				indexMsb >>= 1;
			}
		}
		else
		{
			for (uint32_t ii = 0; ii < 16; ++ii)
			{
				const uint32_t block = ii>>3;
				const uint32_t color = block<<2;
				const uint32_t idx   = (ii&0xc) | ( (ii & 0x3)<<4);
				const uint32_t lsbi  = indexLsb & 1;
				const uint32_t msbi  = (indexMsb & 1)<<1;
				const  int32_t mod   = s_etc1Mod[table[block] ][lsbi | msbi];

				_dst[idx + 0] = uint8_satadd(rgb[color+2], mod);
				_dst[idx + 1] = uint8_satadd(rgb[color+1], mod);
				_dst[idx + 2] = uint8_satadd(rgb[color+0], mod);
				_dst[idx + 3] = 255;

				indexLsb >>= 1;
				indexMsb >>= 1;
			}
		}
	}

// DDS
#define DDS_MAGIC             BX_MAKEFOURCC('D', 'D', 'S', ' ')
#define DDS_HEADER_SIZE       124

#define DDS_DXT1 BX_MAKEFOURCC('D', 'X', 'T', '1')
#define DDS_DXT2 BX_MAKEFOURCC('D', 'X', 'T', '2')
#define DDS_DXT3 BX_MAKEFOURCC('D', 'X', 'T', '3')
#define DDS_DXT4 BX_MAKEFOURCC('D', 'X', 'T', '4')
#define DDS_DXT5 BX_MAKEFOURCC('D', 'X', 'T', '5')
#define DDS_ATI1 BX_MAKEFOURCC('A', 'T', 'I', '1')
#define DDS_BC4U BX_MAKEFOURCC('B', 'C', '4', 'U')
#define DDS_ATI2 BX_MAKEFOURCC('A', 'T', 'I', '2')
#define DDS_BC5U BX_MAKEFOURCC('B', 'C', '5', 'U')
#define DDS_DX10 BX_MAKEFOURCC('D', 'X', '1', '0')

#define D3DFMT_A8R8G8B8       21
#define D3DFMT_R5G6B5         23
#define D3DFMT_A1R5G5B5       25
#define D3DFMT_A4R4G4B4       26
#define D3DFMT_A2B10G10R10    31
#define D3DFMT_G16R16         34
#define D3DFMT_A2R10G10B10    35
#define D3DFMT_A16B16G16R16   36
#define D3DFMT_A8L8           51
#define D3DFMT_R16F           111
#define D3DFMT_G16R16F        112
#define D3DFMT_A16B16G16R16F  113
#define D3DFMT_R32F           114
#define D3DFMT_G32R32F        115
#define D3DFMT_A32B32G32R32F  116

#define DXGI_FORMAT_R32G32B32A32_FLOAT 2
#define DXGI_FORMAT_R32G32B32A32_UINT  3
#define DXGI_FORMAT_R16G16B16A16_FLOAT 10
#define DXGI_FORMAT_R16G16B16A16_UNORM 11
#define DXGI_FORMAT_R16G16B16A16_UINT  12
#define DXGI_FORMAT_R32G32_FLOAT       16
#define DXGI_FORMAT_R32G32_UINT        17
#define DXGI_FORMAT_R10G10B10A2_UNORM  24
#define DXGI_FORMAT_R16G16_FLOAT       34
#define DXGI_FORMAT_R16G16_UNORM       35
#define DXGI_FORMAT_R32_FLOAT          41
#define DXGI_FORMAT_R32_UINT           42
#define DXGI_FORMAT_R8G8_UNORM         49
#define DXGI_FORMAT_R16_FLOAT          54
#define DXGI_FORMAT_R16_UNORM          56
#define DXGI_FORMAT_R8_UNORM           61
#define DXGI_FORMAT_BC1_UNORM          71
#define DXGI_FORMAT_BC2_UNORM          74
#define DXGI_FORMAT_BC3_UNORM          77
#define DXGI_FORMAT_BC4_UNORM          80
#define DXGI_FORMAT_BC5_UNORM          83
#define DXGI_FORMAT_B5G6R5_UNORM       85
#define DXGI_FORMAT_B5G5R5A1_UNORM     86
#define DXGI_FORMAT_B8G8R8A8_UNORM     87
#define DXGI_FORMAT_BC6H_SF16          96
#define DXGI_FORMAT_BC7_UNORM          98
#define DXGI_FORMAT_B4G4R4A4_UNORM     115

#define DDSD_CAPS                   0x00000001
#define DDSD_HEIGHT                 0x00000002
#define DDSD_WIDTH                  0x00000004
#define DDSD_PITCH                  0x00000008
#define DDSD_PIXELFORMAT            0x00001000
#define DDSD_MIPMAPCOUNT            0x00020000
#define DDSD_LINEARSIZE             0x00080000
#define DDSD_DEPTH                  0x00800000

#define DDPF_ALPHAPIXELS            0x00000001
#define DDPF_ALPHA                  0x00000002
#define DDPF_FOURCC                 0x00000004
#define DDPF_INDEXED                0x00000020
#define DDPF_RGB                    0x00000040
#define DDPF_YUV                    0x00000200
#define DDPF_LUMINANCE              0x00020000

#define DDSCAPS_COMPLEX             0x00000008
#define DDSCAPS_TEXTURE             0x00001000
#define DDSCAPS_MIPMAP              0x00400000

#define DDSCAPS2_CUBEMAP            0x00000200
#define DDSCAPS2_CUBEMAP_POSITIVEX  0x00000400
#define DDSCAPS2_CUBEMAP_NEGATIVEX  0x00000800
#define DDSCAPS2_CUBEMAP_POSITIVEY  0x00001000
#define DDSCAPS2_CUBEMAP_NEGATIVEY  0x00002000
#define DDSCAPS2_CUBEMAP_POSITIVEZ  0x00004000
#define DDSCAPS2_CUBEMAP_NEGATIVEZ  0x00008000

#define DDS_CUBEMAP_ALLFACES (DDSCAPS2_CUBEMAP_POSITIVEX|DDSCAPS2_CUBEMAP_NEGATIVEX \
							 |DDSCAPS2_CUBEMAP_POSITIVEY|DDSCAPS2_CUBEMAP_NEGATIVEY \
							 |DDSCAPS2_CUBEMAP_POSITIVEZ|DDSCAPS2_CUBEMAP_NEGATIVEZ)

#define DDSCAPS2_VOLUME             0x00200000

	struct TranslateDdsFormat
	{
		uint32_t m_format;
		TextureFormat::Enum m_textureFormat;

	};
	
	static TranslateDdsFormat s_translateDdsFormat[] =
	{
		{ DDS_DXT1,                  TextureFormat::BC1     },
		{ DDS_DXT2,                  TextureFormat::BC2     },
		{ DDS_DXT3,                  TextureFormat::BC2     },
		{ DDS_DXT4,                  TextureFormat::BC3     },
		{ DDS_DXT5,                  TextureFormat::BC3     },
		{ DDS_ATI1,                  TextureFormat::BC4     },
		{ DDS_BC4U,                  TextureFormat::BC4     },
		{ DDS_ATI2,                  TextureFormat::BC5     },
		{ DDS_BC5U,                  TextureFormat::BC5     },
		{ D3DFMT_A16B16G16R16,       TextureFormat::RGBA16  },
		{ D3DFMT_A16B16G16R16F,      TextureFormat::RGBA16F },
		{ DDPF_RGB|DDPF_ALPHAPIXELS, TextureFormat::BGRA8   },
		{ DDPF_INDEXED,              TextureFormat::R8      },
		{ DDPF_LUMINANCE,            TextureFormat::R8      },
		{ DDPF_ALPHA,                TextureFormat::R8      },
		{ D3DFMT_R16F,               TextureFormat::R16F    },
		{ D3DFMT_R32F,               TextureFormat::R32F    },
		{ D3DFMT_A8L8,               TextureFormat::RG8     },
		{ D3DFMT_G16R16,             TextureFormat::RG16    },
		{ D3DFMT_G16R16F,            TextureFormat::RG16F   },
		{ D3DFMT_G32R32F,            TextureFormat::RG32F   },
		{ D3DFMT_A8R8G8B8,           TextureFormat::BGRA8   },
		{ D3DFMT_A16B16G16R16,       TextureFormat::RGBA16  },
		{ D3DFMT_A16B16G16R16F,      TextureFormat::RGBA16F },
		{ D3DFMT_A32B32G32R32F,      TextureFormat::RGBA32F },
		{ D3DFMT_R5G6B5,             TextureFormat::R5G6B5  },
		{ D3DFMT_A4R4G4B4,           TextureFormat::RGBA4   },
		{ D3DFMT_A1R5G5B5,           TextureFormat::RGB5A1  },
		{ D3DFMT_A2B10G10R10,        TextureFormat::RGB10A2 },
	};

	static TranslateDdsFormat s_translateDxgiFormat[] =
	{
		{ DXGI_FORMAT_BC1_UNORM,          TextureFormat::BC1     },
		{ DXGI_FORMAT_BC2_UNORM,          TextureFormat::BC2     },
		{ DXGI_FORMAT_BC3_UNORM,          TextureFormat::BC3     }, 
		{ DXGI_FORMAT_BC4_UNORM,          TextureFormat::BC4     }, 
		{ DXGI_FORMAT_BC5_UNORM,          TextureFormat::BC5     },
		{ DXGI_FORMAT_BC6H_SF16,          TextureFormat::BC6H    },
		{ DXGI_FORMAT_BC7_UNORM,          TextureFormat::BC7     }, 

		{ DXGI_FORMAT_R8_UNORM,           TextureFormat::R8      },
		{ DXGI_FORMAT_R16_UNORM,          TextureFormat::R16     },
		{ DXGI_FORMAT_R16_FLOAT,          TextureFormat::R16F    },
		{ DXGI_FORMAT_R32_UINT,           TextureFormat::R32     },
		{ DXGI_FORMAT_R32_FLOAT,          TextureFormat::R32F    },
		{ DXGI_FORMAT_R8G8_UNORM,         TextureFormat::RG8     },
		{ DXGI_FORMAT_R16G16_UNORM,       TextureFormat::RG16    },
		{ DXGI_FORMAT_R16G16_FLOAT,       TextureFormat::RG16F   },
		{ DXGI_FORMAT_R32G32_UINT,        TextureFormat::RG32    },
		{ DXGI_FORMAT_R32G32_FLOAT,       TextureFormat::RG32F   },
		{ DXGI_FORMAT_B8G8R8A8_UNORM,     TextureFormat::BGRA8   },
		{ DXGI_FORMAT_R16G16B16A16_UNORM, TextureFormat::RGBA16  },
		{ DXGI_FORMAT_R16G16B16A16_FLOAT, TextureFormat::RGBA16F },
		{ DXGI_FORMAT_R32G32B32A32_UINT,  TextureFormat::RGBA32  },
		{ DXGI_FORMAT_R32G32B32A32_FLOAT, TextureFormat::RGBA32F },
		{ DXGI_FORMAT_B5G6R5_UNORM,       TextureFormat::R5G6B5  },
		{ DXGI_FORMAT_B4G4R4A4_UNORM,     TextureFormat::RGBA4   },
		{ DXGI_FORMAT_B5G5R5A1_UNORM,     TextureFormat::RGB5A1  },
		{ DXGI_FORMAT_R10G10B10A2_UNORM,  TextureFormat::RGB10A2 },
	};

	bool imageParseDds(ImageContainer& _imageContainer, bx::ReaderSeekerI* _reader)
	{
		uint32_t headerSize;
		bx::read(_reader, headerSize);

		if (headerSize < DDS_HEADER_SIZE)
		{
			return false;
		}

		uint32_t flags;
		bx::read(_reader, flags);

		if ( (flags & (DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH|DDSD_PIXELFORMAT) ) != (DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH|DDSD_PIXELFORMAT) )
		{
			return false;
		}

		uint32_t height;
		bx::read(_reader, height);

		uint32_t width;
		bx::read(_reader, width);

		uint32_t pitch;
		bx::read(_reader, pitch);

		uint32_t depth;
		bx::read(_reader, depth);

		uint32_t mips;
		bx::read(_reader, mips);

		bx::skip(_reader, 44); // reserved

		uint32_t pixelFormatSize;
		bx::read(_reader, pixelFormatSize);

		uint32_t pixelFlags;
		bx::read(_reader, pixelFlags);

		uint32_t fourcc;
		bx::read(_reader, fourcc);

		uint32_t rgbCount;
		bx::read(_reader, rgbCount);

		uint32_t rbitmask;
		bx::read(_reader, rbitmask);

		uint32_t gbitmask;
		bx::read(_reader, gbitmask);

		uint32_t bbitmask;
		bx::read(_reader, bbitmask);

		uint32_t abitmask;
		bx::read(_reader, abitmask);

		uint32_t caps[4];
		bx::read(_reader, caps);

		bx::skip(_reader, 4); // reserved

		uint32_t dxgiFormat = 0;
		if (DDPF_FOURCC == pixelFlags
		&&  DDS_DX10 == fourcc)
		{
			bx::read(_reader, dxgiFormat);

			uint32_t dims;
			bx::read(_reader, dims);

			uint32_t miscFlags;
			bx::read(_reader, miscFlags);

			uint32_t arraySize;
			bx::read(_reader, arraySize);

			uint32_t miscFlags2;
			bx::read(_reader, miscFlags2);
		}

		if ( (caps[0] & DDSCAPS_TEXTURE) == 0)
		{
			return false;
		}

		bool cubeMap = 0 != (caps[1] & DDSCAPS2_CUBEMAP);
		if (cubeMap)
		{
			if ( (caps[1] & DDS_CUBEMAP_ALLFACES) != DDS_CUBEMAP_ALLFACES)
			{
				// parital cube map is not supported.
				return false;
			}
		}

		TextureFormat::Enum format = TextureFormat::Unknown;
		bool hasAlpha = pixelFlags & DDPF_ALPHAPIXELS;

		if (dxgiFormat == 0)
		{
			uint32_t ddsFormat = pixelFlags & DDPF_FOURCC ? fourcc : pixelFlags;

			for (uint32_t ii = 0; ii < BX_COUNTOF(s_translateDdsFormat); ++ii)
			{
				if (s_translateDdsFormat[ii].m_format == ddsFormat)
				{
					format = s_translateDdsFormat[ii].m_textureFormat;
					break;
				}
			}
		}
		else
		{
			for (uint32_t ii = 0; ii < BX_COUNTOF(s_translateDxgiFormat); ++ii)
			{
				if (s_translateDxgiFormat[ii].m_format == dxgiFormat)
				{
					format = s_translateDxgiFormat[ii].m_textureFormat;
					break;
				}
			}
		}

		_imageContainer.m_data = NULL;
		_imageContainer.m_size = 0;
		_imageContainer.m_offset = (uint32_t)bx::seek(_reader);
		_imageContainer.m_width = width;
		_imageContainer.m_height = height;
		_imageContainer.m_depth = depth;
		_imageContainer.m_format = format;
		_imageContainer.m_numMips = (caps[0] & DDSCAPS_MIPMAP) ? mips : 1;
		_imageContainer.m_hasAlpha = hasAlpha;
		_imageContainer.m_cubeMap = cubeMap;
		_imageContainer.m_ktx = false;

		return TextureFormat::Unknown != format;
	}

// KTX
#define KTX_MAGIC       BX_MAKEFOURCC(0xAB, 'K', 'T', 'X')
#define KTX_HEADER_SIZE 64

#define KTX_ETC1_RGB8_OES                             0x8D64
#define KTX_COMPRESSED_R11_EAC                        0x9270
#define KTX_COMPRESSED_SIGNED_R11_EAC                 0x9271
#define KTX_COMPRESSED_RG11_EAC                       0x9272
#define KTX_COMPRESSED_SIGNED_RG11_EAC                0x9273
#define KTX_COMPRESSED_RGB8_ETC2                      0x9274
#define KTX_COMPRESSED_SRGB8_ETC2                     0x9275
#define KTX_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2  0x9276
#define KTX_COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2 0x9277
#define KTX_COMPRESSED_RGBA8_ETC2_EAC                 0x9278
#define KTX_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC          0x9279
#define KTX_COMPRESSED_RGB_PVRTC_4BPPV1_IMG           0x8C00
#define KTX_COMPRESSED_RGB_PVRTC_2BPPV1_IMG           0x8C01
#define KTX_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG          0x8C02
#define KTX_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG          0x8C03
#define KTX_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG          0x9137
#define KTX_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG          0x9138
#define KTX_COMPRESSED_RGBA_S3TC_DXT1_EXT             0x83F1
#define KTX_COMPRESSED_RGBA_S3TC_DXT3_EXT             0x83F2
#define KTX_COMPRESSED_RGBA_S3TC_DXT5_EXT             0x83F3
#define KTX_COMPRESSED_LUMINANCE_LATC1_EXT            0x8C70
#define KTX_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT      0x8C72
#define KTX_COMPRESSED_RGBA_BPTC_UNORM_ARB            0x8E8C
#define KTX_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB      0x8E8D
#define KTX_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB      0x8E8E
#define KTX_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB    0x8E8F
#define KTX_R8                                        0x8229
#define KTX_R16                                       0x822A
#define KTX_RG8                                       0x822B
#define KTX_RG16                                      0x822C
#define KTX_R16F                                      0x822D
#define KTX_R32F                                      0x822E
#define KTX_RG16F                                     0x822F
#define KTX_RG32F                                     0x8230
#define KTX_RGBA16                                    0x805B
#define KTX_RGBA16F                                   0x881A
#define KTX_R32UI                                     0x8236
#define KTX_RG32UI                                    0x823C
#define KTX_RGBA32UI                                  0x8D70
#define KTX_BGRA                                      0x80E1
#define KTX_RGBA32F                                   0x8814
#define KTX_RGB565                                    0x8D62
#define KTX_RGBA4                                     0x8056
#define KTX_RGB5_A1                                   0x8057
#define KTX_RGB10_A2                                  0x8059

	static struct TranslateKtxFormat
	{
		uint32_t m_format;
		TextureFormat::Enum m_textureFormat;

	} s_translateKtxFormat[] =
	{
		{ KTX_COMPRESSED_RGBA_S3TC_DXT1_EXT,             TextureFormat::BC1     },
		{ KTX_COMPRESSED_RGBA_S3TC_DXT3_EXT,             TextureFormat::BC2     },
		{ KTX_COMPRESSED_RGBA_S3TC_DXT5_EXT,             TextureFormat::BC3     },
		{ KTX_COMPRESSED_LUMINANCE_LATC1_EXT,            TextureFormat::BC4     },
		{ KTX_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT,      TextureFormat::BC5     },
		{ KTX_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB,      TextureFormat::BC6H    },
		{ KTX_COMPRESSED_RGBA_BPTC_UNORM_ARB,            TextureFormat::BC7     },
		{ KTX_ETC1_RGB8_OES,                             TextureFormat::ETC1    },
		{ KTX_COMPRESSED_RGB8_ETC2,                      TextureFormat::ETC2    },
		{ KTX_COMPRESSED_RGBA8_ETC2_EAC,                 TextureFormat::ETC2A   },
		{ KTX_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2,  TextureFormat::ETC2A1  },
		{ KTX_COMPRESSED_RGB_PVRTC_2BPPV1_IMG,           TextureFormat::PTC12   },
		{ KTX_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG,          TextureFormat::PTC12A  },
		{ KTX_COMPRESSED_RGB_PVRTC_4BPPV1_IMG,           TextureFormat::PTC14   },
		{ KTX_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG,          TextureFormat::PTC14A  },
		{ KTX_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG,          TextureFormat::PTC22   },
		{ KTX_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG,          TextureFormat::PTC24   },
		{ KTX_R8,                                        TextureFormat::R8      },
		{ KTX_RGBA16,                                    TextureFormat::RGBA16  },
		{ KTX_RGBA16F,                                   TextureFormat::RGBA16F },
		{ KTX_R32UI,                                     TextureFormat::R32     },
		{ KTX_R32F,                                      TextureFormat::R32F    },
		{ KTX_RG8,                                       TextureFormat::RG8     },
		{ KTX_RG16,                                      TextureFormat::RG16    },
		{ KTX_RG16F,                                     TextureFormat::RG16F   },
		{ KTX_RG32UI,                                    TextureFormat::RG32    },
		{ KTX_RG32F,                                     TextureFormat::RG32F   },
		{ KTX_BGRA,                                      TextureFormat::BGRA8   },
		{ KTX_RGBA16,                                    TextureFormat::RGBA16  },
		{ KTX_RGBA16F,                                   TextureFormat::RGBA16F },
		{ KTX_RGBA32UI,                                  TextureFormat::RGBA32  },
		{ KTX_RGBA32F,                                   TextureFormat::RGBA32F },
		{ KTX_RGB565,                                    TextureFormat::R5G6B5  },
		{ KTX_RGBA4,                                     TextureFormat::RGBA4   },
		{ KTX_RGB5_A1,                                   TextureFormat::RGB5A1  },
		{ KTX_RGB10_A2,                                  TextureFormat::RGB10A2 },
	};

	bool imageParseKtx(ImageContainer& _imageContainer, bx::ReaderSeekerI* _reader)
	{
		uint8_t identifier[8];
		bx::read(_reader, identifier);

		if (identifier[1] != '1'
		&&  identifier[2] != '1')
		{
			return false;
		}

		uint32_t endianness;
		bx::read(_reader, endianness);

		bool fromLittleEndian = 0x04030201 == endianness;

		uint32_t glType;
		bx::readHE(_reader, glType, fromLittleEndian);

		uint32_t glTypeSize;
		bx::readHE(_reader, glTypeSize, fromLittleEndian);

		uint32_t glFormat;
		bx::readHE(_reader, glFormat, fromLittleEndian);

		uint32_t glInternalFormat;
		bx::readHE(_reader, glInternalFormat, fromLittleEndian);

		uint32_t glBaseInternalFormat;
		bx::readHE(_reader, glBaseInternalFormat, fromLittleEndian);

		uint32_t width;
		bx::readHE(_reader, width, fromLittleEndian);

		uint32_t height;
		bx::readHE(_reader, height, fromLittleEndian);

		uint32_t depth;
		bx::readHE(_reader, depth, fromLittleEndian);

		uint32_t numberOfArrayElements;
		bx::readHE(_reader, numberOfArrayElements, fromLittleEndian);

		uint32_t numFaces;
		bx::readHE(_reader, numFaces, fromLittleEndian);

		uint32_t numMips;
		bx::readHE(_reader, numMips, fromLittleEndian);

		uint32_t metaDataSize;
		bx::readHE(_reader, metaDataSize, fromLittleEndian);

		// skip meta garbage...
		int64_t offset = bx::skip(_reader, metaDataSize);

		TextureFormat::Enum format = TextureFormat::Unknown;
		bool hasAlpha = false;

		for (uint32_t ii = 0; ii < BX_COUNTOF(s_translateKtxFormat); ++ii)
		{
			if (s_translateKtxFormat[ii].m_format == glInternalFormat)
			{
				format = s_translateKtxFormat[ii].m_textureFormat;
				break;
			}
		}

		_imageContainer.m_data = NULL;
		_imageContainer.m_size = 0;
		_imageContainer.m_offset = (uint32_t)offset;
		_imageContainer.m_width = width;
		_imageContainer.m_height = height;
		_imageContainer.m_depth = depth;
		_imageContainer.m_format = format;
		_imageContainer.m_numMips = numMips;
		_imageContainer.m_hasAlpha = hasAlpha;
		_imageContainer.m_cubeMap = numFaces > 1;
		_imageContainer.m_ktx = true;

		return TextureFormat::Unknown != format;
	}

// PVR3
#define PVR3_MAKE8CC(_a, _b, _c, _d, _e, _f, _g, _h) (uint64_t(BX_MAKEFOURCC(_a, _b, _c, _d) ) | (uint64_t(BX_MAKEFOURCC(_e, _f, _g, _h) )<<32) )

#define PVR3_MAGIC            BX_MAKEFOURCC('P', 'V', 'R', 3)
#define PVR3_HEADER_SIZE      52

#define PVR3_PVRTC1_2BPP_RGB  0
#define PVR3_PVRTC1_2BPP_RGBA 1
#define PVR3_PVRTC1_4BPP_RGB  2
#define PVR3_PVRTC1_4BPP_RGBA 3
#define PVR3_PVRTC2_2BPP_RGBA 4
#define PVR3_PVRTC2_4BPP_RGBA 5
#define PVR3_ETC1             6
#define PVR3_DXT1             7
#define PVR3_DXT2             8
#define PVR3_DXT3             9
#define PVR3_DXT4             10
#define PVR3_DXT5             11
#define PVR3_BC4              12
#define PVR3_BC5              13
#define PVR3_R8               PVR3_MAKE8CC('r',   0,   0,   0,  8,  0,  0,  0)
#define PVR3_R16              PVR3_MAKE8CC('r',   0,   0,   0, 16,  0,  0,  0)
#define PVR3_R32              PVR3_MAKE8CC('r',   0,   0,   0, 32,  0,  0,  0)
#define PVR3_RG8              PVR3_MAKE8CC('r', 'g',   0,   0,  8,  8,  0,  0)
#define PVR3_RG16             PVR3_MAKE8CC('r', 'g',   0,   0, 16, 16,  0,  0)
#define PVR3_RG32             PVR3_MAKE8CC('r', 'g',   0,   0, 32, 32,  0,  0)
#define PVR3_BGRA8            PVR3_MAKE8CC('b', 'g', 'r', 'a',  8,  8,  8,  8)
#define PVR3_RGBA16           PVR3_MAKE8CC('r', 'g', 'b', 'a', 16, 16, 16, 16)
#define PVR3_RGBA32           PVR3_MAKE8CC('r', 'g', 'b', 'a', 32, 32, 32, 32)
#define PVR3_RGB565           PVR3_MAKE8CC('r', 'g', 'b',   0,  5,  6,  5,  0)
#define PVR3_RGBA4            PVR3_MAKE8CC('r', 'g', 'b', 'a',  4,  4,  4,  4)
#define PVR3_RGBA51           PVR3_MAKE8CC('r', 'g', 'b', 'a',  5,  5,  5,  1)
#define PVR3_RGB10A2          PVR3_MAKE8CC('r', 'g', 'b', 'a', 10, 10, 10,  2)

#define PVR3_CHANNEL_TYPE_ANY   UINT32_MAX
#define PVR3_CHANNEL_TYPE_FLOAT UINT32_C(12)

	static struct TranslatePvr3Format
	{
		uint64_t m_format;
		uint32_t m_channelTypeMask;
		TextureFormat::Enum m_textureFormat;

	} s_translatePvr3Format[] =
	{
		{ PVR3_PVRTC1_2BPP_RGB,  PVR3_CHANNEL_TYPE_ANY,   TextureFormat::PTC12   },
		{ PVR3_PVRTC1_2BPP_RGBA, PVR3_CHANNEL_TYPE_ANY,   TextureFormat::PTC12A  },
		{ PVR3_PVRTC1_4BPP_RGB,  PVR3_CHANNEL_TYPE_ANY,   TextureFormat::PTC14   },
		{ PVR3_PVRTC1_4BPP_RGBA, PVR3_CHANNEL_TYPE_ANY,   TextureFormat::PTC14A  },
		{ PVR3_PVRTC2_2BPP_RGBA, PVR3_CHANNEL_TYPE_ANY,   TextureFormat::PTC22   },
		{ PVR3_PVRTC2_4BPP_RGBA, PVR3_CHANNEL_TYPE_ANY,   TextureFormat::PTC24   },
		{ PVR3_ETC1,             PVR3_CHANNEL_TYPE_ANY,   TextureFormat::ETC1    },
		{ PVR3_DXT1,             PVR3_CHANNEL_TYPE_ANY,   TextureFormat::BC1     },
		{ PVR3_DXT2,             PVR3_CHANNEL_TYPE_ANY,   TextureFormat::BC2     },
		{ PVR3_DXT3,             PVR3_CHANNEL_TYPE_ANY,   TextureFormat::BC2     },
		{ PVR3_DXT4,             PVR3_CHANNEL_TYPE_ANY,   TextureFormat::BC3     },
		{ PVR3_DXT5,             PVR3_CHANNEL_TYPE_ANY,   TextureFormat::BC3     },
		{ PVR3_BC4,              PVR3_CHANNEL_TYPE_ANY,   TextureFormat::BC4     },
		{ PVR3_BC5,              PVR3_CHANNEL_TYPE_ANY,   TextureFormat::BC5     },
		{ PVR3_R8,               PVR3_CHANNEL_TYPE_ANY,   TextureFormat::R8      },
		{ PVR3_R16,              PVR3_CHANNEL_TYPE_ANY,   TextureFormat::R16     },
		{ PVR3_R16,              PVR3_CHANNEL_TYPE_FLOAT, TextureFormat::R16F    },
		{ PVR3_R32,              PVR3_CHANNEL_TYPE_ANY,   TextureFormat::R32     },
		{ PVR3_R32,              PVR3_CHANNEL_TYPE_FLOAT, TextureFormat::R32F    },
		{ PVR3_RG8,              PVR3_CHANNEL_TYPE_ANY,   TextureFormat::RG8     },
		{ PVR3_RG16,             PVR3_CHANNEL_TYPE_ANY,   TextureFormat::RG16    },
		{ PVR3_RG16,             PVR3_CHANNEL_TYPE_FLOAT, TextureFormat::RG16F   },
		{ PVR3_RG32,             PVR3_CHANNEL_TYPE_ANY,   TextureFormat::RG16    },
		{ PVR3_RG32,             PVR3_CHANNEL_TYPE_FLOAT, TextureFormat::RG32F   },
		{ PVR3_BGRA8,            PVR3_CHANNEL_TYPE_ANY,   TextureFormat::BGRA8   },
		{ PVR3_RGBA16,           PVR3_CHANNEL_TYPE_ANY,   TextureFormat::RGBA16  },
		{ PVR3_RGBA16,           PVR3_CHANNEL_TYPE_FLOAT, TextureFormat::RGBA16F },
		{ PVR3_RGBA32,           PVR3_CHANNEL_TYPE_ANY,   TextureFormat::RGBA32  },
		{ PVR3_RGBA32,           PVR3_CHANNEL_TYPE_FLOAT, TextureFormat::RGBA32F },
		{ PVR3_RGB565,           PVR3_CHANNEL_TYPE_ANY,   TextureFormat::R5G6B5  },
		{ PVR3_RGBA4,            PVR3_CHANNEL_TYPE_ANY,   TextureFormat::RGBA4   },
		{ PVR3_RGBA51,           PVR3_CHANNEL_TYPE_ANY,   TextureFormat::RGB5A1  },
		{ PVR3_RGB10A2,          PVR3_CHANNEL_TYPE_ANY,   TextureFormat::RGB10A2 },
	};

	bool imageParsePvr3(ImageContainer& _imageContainer, bx::ReaderSeekerI* _reader)
	{
		uint32_t flags;
		bx::read(_reader, flags);

		uint64_t pixelFormat;
		bx::read(_reader, pixelFormat);

		uint32_t colorSpace;
		bx::read(_reader, colorSpace); // 0 - linearRGB, 1 - sRGB

		uint32_t channelType;
		bx::read(_reader, channelType);

		uint32_t height;
		bx::read(_reader, height);

		uint32_t width;
		bx::read(_reader, width);

		uint32_t depth;
		bx::read(_reader, depth);

		uint32_t numSurfaces;
		bx::read(_reader, numSurfaces);

		uint32_t numFaces;
		bx::read(_reader, numFaces);

		uint32_t numMips;
		bx::read(_reader, numMips);

		uint32_t metaDataSize;
		bx::read(_reader, metaDataSize);

		// skip meta garbage...
		int64_t offset = bx::skip(_reader, metaDataSize);

		TextureFormat::Enum format = TextureFormat::Unknown;
		bool hasAlpha = false;

		for (uint32_t ii = 0; ii < BX_COUNTOF(s_translatePvr3Format); ++ii)
		{
			if (s_translatePvr3Format[ii].m_format == pixelFormat
			&&  channelType == (s_translatePvr3Format[ii].m_channelTypeMask & channelType) )
			{
				format = s_translatePvr3Format[ii].m_textureFormat;
				break;
			}
		}

		_imageContainer.m_data = NULL;
		_imageContainer.m_size = 0;
		_imageContainer.m_offset = (uint32_t)offset;
		_imageContainer.m_width = width;
		_imageContainer.m_height = height;
		_imageContainer.m_depth = depth;
		_imageContainer.m_format = format;
		_imageContainer.m_numMips = numMips;
		_imageContainer.m_hasAlpha = hasAlpha;
		_imageContainer.m_cubeMap = numFaces > 1;
		_imageContainer.m_ktx = false;

		return TextureFormat::Unknown != format;
	}

	bool imageParse(ImageContainer& _imageContainer, bx::ReaderSeekerI* _reader)
	{
		uint32_t magic;
		bx::read(_reader, magic);

		if (DDS_MAGIC == magic)
		{
			return imageParseDds(_imageContainer, _reader);
		}
		else if (KTX_MAGIC == magic)
		{
			return imageParseKtx(_imageContainer, _reader);
		}
		else if (PVR3_MAGIC == magic)
		{
			return imageParsePvr3(_imageContainer, _reader);
		}
		else if (BGFX_CHUNK_MAGIC_TEX == magic)
		{
			TextureCreate tc;
			bx::read(_reader, tc);

			_imageContainer.m_format = tc.m_format;
			_imageContainer.m_offset = UINT32_MAX;
			if (NULL == tc.m_mem)
			{
				_imageContainer.m_data = NULL;
				_imageContainer.m_size = 0;
			}
			else
			{
				_imageContainer.m_data = tc.m_mem->data;
				_imageContainer.m_size = tc.m_mem->size;
			}
			_imageContainer.m_width = tc.m_width;
			_imageContainer.m_height = tc.m_height;
			_imageContainer.m_depth = tc.m_depth;
			_imageContainer.m_numMips = tc.m_numMips;
			_imageContainer.m_hasAlpha = false;
			_imageContainer.m_cubeMap = tc.m_cubeMap;
			_imageContainer.m_ktx = false;

			return true;
		}

		return false;
	}

	bool imageParse(ImageContainer& _imageContainer, const void* _data, uint32_t _size)
	{
		bx::MemoryReader reader(_data, _size);
		return imageParse(_imageContainer, &reader);
	}

	void imageDecodeToBgra8(uint8_t* _dst, const uint8_t* _src, uint32_t _width, uint32_t _height, uint32_t _pitch, uint8_t _type)
	{
		const uint8_t* src = _src;

		uint32_t width  = _width/4;
		uint32_t height = _height/4;

		uint8_t temp[16*4];

		switch (_type)
		{
		case TextureFormat::BC1:
			for (uint32_t yy = 0; yy < height; ++yy)
			{
				for (uint32_t xx = 0; xx < width; ++xx)
				{
					decodeBlockDxt1(temp, src);
					src += 8;

					uint8_t* dst = &_dst[(yy*_pitch+xx*4)*4];
					memcpy(&dst[0*_pitch], &temp[ 0], 16);
					memcpy(&dst[1*_pitch], &temp[16], 16);
					memcpy(&dst[2*_pitch], &temp[32], 16);
					memcpy(&dst[3*_pitch], &temp[48], 16);
				}
			}
			break;

		case TextureFormat::BC2:
			for (uint32_t yy = 0; yy < height; ++yy)
			{
				for (uint32_t xx = 0; xx < width; ++xx)
				{
					decodeBlockDxt23A(temp+3, src);
					src += 8;
					decodeBlockDxt(temp, src);
					src += 8;

					uint8_t* dst = &_dst[(yy*_pitch+xx*4)*4];
					memcpy(&dst[0*_pitch], &temp[ 0], 16);
					memcpy(&dst[1*_pitch], &temp[16], 16);
					memcpy(&dst[2*_pitch], &temp[32], 16);
					memcpy(&dst[3*_pitch], &temp[48], 16);
				}
			}
			break;

		case TextureFormat::BC3:
			for (uint32_t yy = 0; yy < height; ++yy)
			{
				for (uint32_t xx = 0; xx < width; ++xx)
				{
					decodeBlockDxt45A(temp+3, src);
					src += 8;
					decodeBlockDxt(temp, src);
					src += 8;

					uint8_t* dst = &_dst[(yy*_pitch+xx*4)*4];
					memcpy(&dst[0*_pitch], &temp[ 0], 16);
					memcpy(&dst[1*_pitch], &temp[16], 16);
					memcpy(&dst[2*_pitch], &temp[32], 16);
					memcpy(&dst[3*_pitch], &temp[48], 16);
				}
			}
			break;

		case TextureFormat::BC4:
			for (uint32_t yy = 0; yy < height; ++yy)
			{
				for (uint32_t xx = 0; xx < width; ++xx)
				{
					decodeBlockDxt45A(temp, src);
					src += 8;

					uint8_t* dst = &_dst[(yy*_pitch+xx*4)*4];
					memcpy(&dst[0*_pitch], &temp[ 0], 16);
					memcpy(&dst[1*_pitch], &temp[16], 16);
					memcpy(&dst[2*_pitch], &temp[32], 16);
					memcpy(&dst[3*_pitch], &temp[48], 16);
				}
			}
			break;

		case TextureFormat::BC5:
			for (uint32_t yy = 0; yy < height; ++yy)
			{
				for (uint32_t xx = 0; xx < width; ++xx)
				{
					decodeBlockDxt45A(temp+1, src);
					src += 8;
					decodeBlockDxt45A(temp+2, src);
					src += 8;

					for (uint32_t ii = 0; ii < 16; ++ii)
					{
						float nx = temp[ii*4+2]*2.0f/255.0f - 1.0f;
						float ny = temp[ii*4+1]*2.0f/255.0f - 1.0f;
						float nz = sqrtf(1.0f - nx*nx - ny*ny);
						temp[ii*4+0] = uint8_t( (nz + 1.0f)*255.0f/2.0f);
						temp[ii*4+3] = 0;
					}

					uint8_t* dst = &_dst[(yy*_pitch+xx*4)*4];
					memcpy(&dst[0*_pitch], &temp[ 0], 16);
					memcpy(&dst[1*_pitch], &temp[16], 16);
					memcpy(&dst[2*_pitch], &temp[32], 16);
					memcpy(&dst[3*_pitch], &temp[48], 16);
				}
			}
			break;

		case TextureFormat::ETC1:
		case TextureFormat::ETC2:
			for (uint32_t yy = 0; yy < height; ++yy)
			{
				for (uint32_t xx = 0; xx < width; ++xx)
				{
					decodeBlockEtc12(temp, src);
					src += 8;

					uint8_t* dst = &_dst[(yy*_pitch+xx*4)*4];
					memcpy(&dst[0*_pitch], &temp[ 0], 16);
					memcpy(&dst[1*_pitch], &temp[16], 16);
					memcpy(&dst[2*_pitch], &temp[32], 16);
					memcpy(&dst[3*_pitch], &temp[48], 16);
				}
			}
			break;

		case TextureFormat::ETC2A:
			BX_WARN(false, "ETC2A decoder is not implemented.");
			imageCheckerboard(_width, _height, 16, UINT32_C(0xff000000), UINT32_C(0xff00ff00), _dst);
			break;

		case TextureFormat::ETC2A1:
			BX_WARN(false, "ETC2A1 decoder is not implemented.");
			imageCheckerboard(_width, _height, 16, UINT32_C(0xff000000), UINT32_C(0xffff0000), _dst);
			break;

		case TextureFormat::PTC12:
			BX_WARN(false, "PTC12 decoder is not implemented.");
			imageCheckerboard(_width, _height, 16, UINT32_C(0xff000000), UINT32_C(0xffff00ff), _dst);
			break;

		case TextureFormat::PTC12A:
			BX_WARN(false, "PTC12A decoder is not implemented.");
			imageCheckerboard(_width, _height, 16, UINT32_C(0xff000000), UINT32_C(0xffffff00), _dst);
			break;

		case TextureFormat::PTC14:
			BX_WARN(false, "PTC14 decoder is not implemented.");
			imageCheckerboard(_width, _height, 16, UINT32_C(0xff000000), UINT32_C(0xff00ffff), _dst);
			break;

		case TextureFormat::PTC14A:
			BX_WARN(false, "PTC14A decoder is not implemented.");
			imageCheckerboard(_width, _height, 16, UINT32_C(0xffff0000), UINT32_C(0xff0000ff), _dst);
			break;

		case TextureFormat::PTC22:
			BX_WARN(false, "PTC22 decoder is not implemented.");
			imageCheckerboard(_width, _height, 16, UINT32_C(0xff00ff00), UINT32_C(0xff0000ff), _dst);
			break;

		case TextureFormat::PTC24:
			BX_WARN(false, "PTC24 decoder is not implemented.");
			imageCheckerboard(_width, _height, 16, UINT32_C(0xff000000), UINT32_C(0xffffffff), _dst);
			break;

		default:
			// Decompression not implemented... Make ugly red-yellow checkerboard texture.
			imageCheckerboard(_width, _height, 16, UINT32_C(0xffff0000), UINT32_C(0xffffff00), _dst);
			break;
		}
	}

	bool imageGetRawData(const ImageContainer& _imageContainer, uint8_t _side, uint8_t _lod, const void* _data, uint32_t _size, ImageMip& _mip)
	{
		uint32_t offset = _imageContainer.m_offset;
		TextureFormat::Enum type = TextureFormat::Enum(_imageContainer.m_format);
		bool hasAlpha = _imageContainer.m_hasAlpha;

		const ImageBlockInfo& blockInfo = s_imageBlockInfo[type];
		const uint8_t  bpp         = blockInfo.bitsPerPixel;
		const uint32_t blockSize   = blockInfo.blockSize;
		const uint32_t blockWidth  = blockInfo.blockWidth;
		const uint32_t blockHeight = blockInfo.blockHeight;

		if (UINT32_MAX == _imageContainer.m_offset)
		{
			if (NULL == _imageContainer.m_data)
			{
				return false;
			}

			offset = 0;
			_data = _imageContainer.m_data;
			_size = _imageContainer.m_size;
		}

		for (uint8_t side = 0, numSides = _imageContainer.m_cubeMap ? 6 : 1; side < numSides; ++side)
		{
			uint32_t width  = _imageContainer.m_width;
			uint32_t height = _imageContainer.m_height;
			uint32_t depth  = _imageContainer.m_depth;

			for (uint8_t lod = 0, num = _imageContainer.m_numMips; lod < num; ++lod)
			{
				// skip imageSize in KTX format.
				offset += _imageContainer.m_ktx ? sizeof(uint32_t) : 0;

				width  = bx::uint32_max(blockWidth,  ( (width +blockWidth -1)/blockWidth )*blockWidth);
				height = bx::uint32_max(blockHeight, ( (height+blockHeight-1)/blockHeight)*blockHeight);
				depth  = bx::uint32_max(1, depth);

				uint32_t size = width*height*depth*bpp/8;

				if (side == _side
				&&  lod == _lod)
				{
					_mip.m_width = width;
					_mip.m_height = height;
					_mip.m_blockSize = blockSize;
					_mip.m_size = size;
					_mip.m_data = (const uint8_t*)_data + offset;
					_mip.m_bpp = bpp;
					_mip.m_format = type;
					_mip.m_hasAlpha = hasAlpha;
					return true;
				}

				offset += size;

				BX_CHECK(offset <= _size, "Reading past size of data buffer! (offset %d, size %d)", offset, _size);
				BX_UNUSED(_size);

				width  >>= 1;
				height >>= 1;
				depth  >>= 1;
			}
		}

		return false;
	}

} // namespace bgfx
/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "bgfx_p.h"

#if BGFX_CONFIG_RENDERER_DIRECT3D11
#	include "renderer_d3d11.h"

#	if BGFX_CONFIG_DEBUG_PIX
#		include <psapi.h>
#		include <renderdoc/renderdoc_app.h>
#	endif // BGFX_CONFIG_DEBUG_PIX

namespace bgfx
{
	static wchar_t s_viewNameW[BGFX_CONFIG_MAX_VIEWS][256];

	struct PrimInfo
	{
		D3D11_PRIMITIVE_TOPOLOGY m_type;
		uint32_t m_min;
		uint32_t m_div;
		uint32_t m_sub;
	};

	static const PrimInfo s_primInfo[] =
	{
		{ D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  3, 3, 0 },
		{ D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, 3, 1, 2 },
		{ D3D11_PRIMITIVE_TOPOLOGY_LINELIST,      2, 2, 0 },
		{ D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,     1, 1, 0 },
		{ D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED,     0, 0, 0 },
	};

	static const char* s_primName[] =
	{
		"TriList",
		"TriStrip",
		"Line",
		"Point",
	};
	BX_STATIC_ASSERT(BX_COUNTOF(s_primInfo) == BX_COUNTOF(s_primName)+1);

	static const uint32_t s_checkMsaa[] =
	{
		0,
		2,
		4,
		8,
		16,
	};

	static DXGI_SAMPLE_DESC s_msaa[] =
	{
		{  1, 0 },
		{  2, 0 },
		{  4, 0 },
		{  8, 0 },
		{ 16, 0 },
	};

	static const D3D11_BLEND s_blendFactor[][2] =
	{
		{ (D3D11_BLEND)0,               (D3D11_BLEND)0               }, // ignored
		{ D3D11_BLEND_ZERO,             D3D11_BLEND_ZERO             }, // ZERO
		{ D3D11_BLEND_ONE,              D3D11_BLEND_ONE              },	// ONE
		{ D3D11_BLEND_SRC_COLOR,        D3D11_BLEND_SRC_ALPHA        },	// SRC_COLOR
		{ D3D11_BLEND_INV_SRC_COLOR,    D3D11_BLEND_INV_SRC_ALPHA    },	// INV_SRC_COLOR
		{ D3D11_BLEND_SRC_ALPHA,        D3D11_BLEND_SRC_ALPHA        },	// SRC_ALPHA
		{ D3D11_BLEND_INV_SRC_ALPHA,    D3D11_BLEND_INV_SRC_ALPHA    },	// INV_SRC_ALPHA
		{ D3D11_BLEND_DEST_ALPHA,       D3D11_BLEND_DEST_ALPHA       },	// DST_ALPHA
		{ D3D11_BLEND_INV_DEST_ALPHA,   D3D11_BLEND_INV_DEST_ALPHA   },	// INV_DST_ALPHA
		{ D3D11_BLEND_DEST_COLOR,       D3D11_BLEND_DEST_ALPHA       },	// DST_COLOR
		{ D3D11_BLEND_INV_DEST_COLOR,   D3D11_BLEND_INV_DEST_ALPHA   },	// INV_DST_COLOR
		{ D3D11_BLEND_SRC_ALPHA_SAT,    D3D11_BLEND_ONE              },	// SRC_ALPHA_SAT
		{ D3D11_BLEND_BLEND_FACTOR,     D3D11_BLEND_BLEND_FACTOR     },	// FACTOR
		{ D3D11_BLEND_INV_BLEND_FACTOR, D3D11_BLEND_INV_BLEND_FACTOR },	// INV_FACTOR
	};

	static const D3D11_BLEND_OP s_blendEquation[] =
	{
		D3D11_BLEND_OP_ADD,
		D3D11_BLEND_OP_SUBTRACT,
		D3D11_BLEND_OP_REV_SUBTRACT,
		D3D11_BLEND_OP_MIN,
		D3D11_BLEND_OP_MAX,
	};

	static const D3D11_COMPARISON_FUNC s_cmpFunc[] =
	{
		D3D11_COMPARISON_FUNC(0), // ignored
		D3D11_COMPARISON_LESS,
		D3D11_COMPARISON_LESS_EQUAL,
		D3D11_COMPARISON_EQUAL,
		D3D11_COMPARISON_GREATER_EQUAL,
		D3D11_COMPARISON_GREATER,
		D3D11_COMPARISON_NOT_EQUAL,
		D3D11_COMPARISON_NEVER,
		D3D11_COMPARISON_ALWAYS,
	};

	static const D3D11_STENCIL_OP s_stencilOp[] =
	{
		D3D11_STENCIL_OP_ZERO,
		D3D11_STENCIL_OP_KEEP,
		D3D11_STENCIL_OP_REPLACE,
		D3D11_STENCIL_OP_INCR,
		D3D11_STENCIL_OP_INCR_SAT,
		D3D11_STENCIL_OP_DECR,
		D3D11_STENCIL_OP_DECR_SAT,
		D3D11_STENCIL_OP_INVERT,
	};

	static const D3D11_CULL_MODE s_cullMode[] =
	{
		D3D11_CULL_NONE,
		D3D11_CULL_FRONT,
		D3D11_CULL_BACK,
	};

	static const DXGI_FORMAT s_depthFormat[] =
	{
		DXGI_FORMAT_UNKNOWN,           // ignored
		DXGI_FORMAT_D16_UNORM,         // D16
		DXGI_FORMAT_D24_UNORM_S8_UINT, // D24
		DXGI_FORMAT_D24_UNORM_S8_UINT, // D24S8
		DXGI_FORMAT_D24_UNORM_S8_UINT, // D32
		DXGI_FORMAT_D32_FLOAT,         // D16F
		DXGI_FORMAT_D32_FLOAT,         // D24F
		DXGI_FORMAT_D32_FLOAT,         // D32F
		DXGI_FORMAT_D24_UNORM_S8_UINT, // D0S8
	};

	static const D3D11_TEXTURE_ADDRESS_MODE s_textureAddress[] =
	{
		D3D11_TEXTURE_ADDRESS_WRAP,
		D3D11_TEXTURE_ADDRESS_MIRROR,
		D3D11_TEXTURE_ADDRESS_CLAMP,
	};

	/*
	 * D3D11_FILTER_MIN_MAG_MIP_POINT               = 0x00,
	 * D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR        = 0x01,
	 * D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT  = 0x04,
	 * D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR        = 0x05,
	 * D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT        = 0x10,
	 * D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x11,
	 * D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT        = 0x14,
	 * D3D11_FILTER_MIN_MAG_MIP_LINEAR              = 0x15,
	 * D3D11_FILTER_ANISOTROPIC                     = 0x55,
	 *
	 * D3D11_COMPARISON_FILTERING_BIT               = 0x80,
	 * D3D11_ANISOTROPIC_FILTERING_BIT              = 0x40,
	 *
	 * According to D3D11_FILTER enum bits for mip, mag and mip are:
	 * 0x10 // MIN_LINEAR
	 * 0x04 // MAG_LINEAR
	 * 0x01 // MIP_LINEAR
	 */

	static const uint32_t s_textureFilter[3][3] =
	{
		{
			0x10, // min linear
			0x00, // min point
			0x55, // anisotopic
		},
		{
			0x04, // mag linear
			0x00, // mag point
			0x55, // anisotopic
		},
		{
			0x01, // mip linear
			0x00, // mip point
			0x55, // anisotopic
		},
	};

	struct TextureFormatInfo
	{
		DXGI_FORMAT m_fmt;
		DXGI_FORMAT m_fmtSrv;
		DXGI_FORMAT m_fmtDsv;
	};

#ifndef DXGI_FORMAT_B4G4R4A4_UNORM
// Win8 only BS
// https://blogs.msdn.com/b/chuckw/archive/2012/11/14/directx-11-1-and-windows-7.aspx?Redirected=true
// http://msdn.microsoft.com/en-us/library/windows/desktop/bb173059%28v=vs.85%29.aspx
#	define DXGI_FORMAT_B4G4R4A4_UNORM DXGI_FORMAT(115)
#endif // DXGI_FORMAT_B4G4R4A4_UNORM

	static const TextureFormatInfo s_textureFormat[] =
	{
		{ DXGI_FORMAT_BC1_UNORM,          DXGI_FORMAT_BC1_UNORM,             DXGI_FORMAT_UNKNOWN           }, // BC1 
		{ DXGI_FORMAT_BC2_UNORM,          DXGI_FORMAT_BC2_UNORM,             DXGI_FORMAT_UNKNOWN           }, // BC2
		{ DXGI_FORMAT_BC3_UNORM,          DXGI_FORMAT_BC3_UNORM,             DXGI_FORMAT_UNKNOWN           }, // BC3
		{ DXGI_FORMAT_BC4_UNORM,          DXGI_FORMAT_BC4_UNORM,             DXGI_FORMAT_UNKNOWN           }, // BC4
		{ DXGI_FORMAT_BC5_UNORM,          DXGI_FORMAT_BC5_UNORM,             DXGI_FORMAT_UNKNOWN           }, // BC5
		{ DXGI_FORMAT_BC6H_SF16,          DXGI_FORMAT_BC6H_SF16,             DXGI_FORMAT_UNKNOWN           }, // BC6H
		{ DXGI_FORMAT_BC7_UNORM,          DXGI_FORMAT_BC7_UNORM,             DXGI_FORMAT_UNKNOWN           }, // BC7
		{ DXGI_FORMAT_UNKNOWN,            DXGI_FORMAT_UNKNOWN,               DXGI_FORMAT_UNKNOWN           }, // ETC1
		{ DXGI_FORMAT_UNKNOWN,            DXGI_FORMAT_UNKNOWN,               DXGI_FORMAT_UNKNOWN           }, // ETC2
		{ DXGI_FORMAT_UNKNOWN,            DXGI_FORMAT_UNKNOWN,               DXGI_FORMAT_UNKNOWN           }, // ETC2A
		{ DXGI_FORMAT_UNKNOWN,            DXGI_FORMAT_UNKNOWN,               DXGI_FORMAT_UNKNOWN           }, // ETC2A1
		{ DXGI_FORMAT_UNKNOWN,            DXGI_FORMAT_UNKNOWN,               DXGI_FORMAT_UNKNOWN           }, // PTC12
		{ DXGI_FORMAT_UNKNOWN,            DXGI_FORMAT_UNKNOWN,               DXGI_FORMAT_UNKNOWN           }, // PTC14
		{ DXGI_FORMAT_UNKNOWN,            DXGI_FORMAT_UNKNOWN,               DXGI_FORMAT_UNKNOWN           }, // PTC12A
		{ DXGI_FORMAT_UNKNOWN,            DXGI_FORMAT_UNKNOWN,               DXGI_FORMAT_UNKNOWN           }, // PTC14A
		{ DXGI_FORMAT_UNKNOWN,            DXGI_FORMAT_UNKNOWN,               DXGI_FORMAT_UNKNOWN           }, // PTC22
		{ DXGI_FORMAT_UNKNOWN,            DXGI_FORMAT_UNKNOWN,               DXGI_FORMAT_UNKNOWN           }, // PTC24
		{ DXGI_FORMAT_UNKNOWN,            DXGI_FORMAT_UNKNOWN,               DXGI_FORMAT_UNKNOWN           }, // Unknown
		{ DXGI_FORMAT_R1_UNORM,           DXGI_FORMAT_R1_UNORM,              DXGI_FORMAT_UNKNOWN           }, // R1
		{ DXGI_FORMAT_R8_UNORM,           DXGI_FORMAT_R8_UNORM,              DXGI_FORMAT_UNKNOWN           }, // R8
		{ DXGI_FORMAT_R16_UNORM,          DXGI_FORMAT_R16_UNORM,             DXGI_FORMAT_UNKNOWN           }, // R16
		{ DXGI_FORMAT_R16_FLOAT,          DXGI_FORMAT_R16_FLOAT,             DXGI_FORMAT_UNKNOWN           }, // R16F
		{ DXGI_FORMAT_R32_UINT,           DXGI_FORMAT_R32_UINT,              DXGI_FORMAT_UNKNOWN           }, // R32
		{ DXGI_FORMAT_R32_FLOAT,          DXGI_FORMAT_R32_FLOAT,             DXGI_FORMAT_UNKNOWN           }, // R32F
		{ DXGI_FORMAT_R8G8_UNORM,         DXGI_FORMAT_R8G8_UNORM,            DXGI_FORMAT_UNKNOWN           }, // RG8
		{ DXGI_FORMAT_R16G16_UNORM,       DXGI_FORMAT_R16G16_UNORM,          DXGI_FORMAT_UNKNOWN           }, // RG16
		{ DXGI_FORMAT_R16G16_FLOAT,       DXGI_FORMAT_R16G16_FLOAT,          DXGI_FORMAT_UNKNOWN           }, // RG16F
		{ DXGI_FORMAT_R32G32_UINT,        DXGI_FORMAT_R32G32_UINT,           DXGI_FORMAT_UNKNOWN           }, // RG32
		{ DXGI_FORMAT_R32G32_FLOAT,       DXGI_FORMAT_R32G32_FLOAT,          DXGI_FORMAT_UNKNOWN           }, // RG32F
		{ DXGI_FORMAT_B8G8R8A8_UNORM,     DXGI_FORMAT_B8G8R8A8_UNORM,        DXGI_FORMAT_UNKNOWN           }, // BGRA8
		{ DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_UNORM,    DXGI_FORMAT_UNKNOWN           }, // RGBA16
		{ DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,    DXGI_FORMAT_UNKNOWN           }, // RGBA16F
		{ DXGI_FORMAT_R32G32B32A32_UINT,  DXGI_FORMAT_R32G32B32A32_UINT,     DXGI_FORMAT_UNKNOWN           }, // RGBA32
		{ DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,    DXGI_FORMAT_UNKNOWN           }, // RGBA32F
		{ DXGI_FORMAT_B5G6R5_UNORM,       DXGI_FORMAT_B5G6R5_UNORM,          DXGI_FORMAT_UNKNOWN           }, // R5G6B5
		{ DXGI_FORMAT_B4G4R4A4_UNORM,     DXGI_FORMAT_B4G4R4A4_UNORM,        DXGI_FORMAT_UNKNOWN           }, // RGBA4
		{ DXGI_FORMAT_B5G5R5A1_UNORM,     DXGI_FORMAT_B5G5R5A1_UNORM,        DXGI_FORMAT_UNKNOWN           }, // RGB5A1
		{ DXGI_FORMAT_R10G10B10A2_UNORM,  DXGI_FORMAT_R10G10B10A2_UNORM,     DXGI_FORMAT_UNKNOWN           }, // RGB10A2
		{ DXGI_FORMAT_R11G11B10_FLOAT,    DXGI_FORMAT_R11G11B10_FLOAT,       DXGI_FORMAT_UNKNOWN           }, // R11G11B10F
		{ DXGI_FORMAT_UNKNOWN,            DXGI_FORMAT_UNKNOWN,               DXGI_FORMAT_UNKNOWN           }, // UnknownDepth
		{ DXGI_FORMAT_R16_TYPELESS,       DXGI_FORMAT_R16_UNORM,             DXGI_FORMAT_D16_UNORM         }, // D16
		{ DXGI_FORMAT_R24G8_TYPELESS,     DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT }, // D24
		{ DXGI_FORMAT_R24G8_TYPELESS,     DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT }, // D24S8
		{ DXGI_FORMAT_R24G8_TYPELESS,     DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT }, // D32
		{ DXGI_FORMAT_R32_TYPELESS,       DXGI_FORMAT_R32_FLOAT,             DXGI_FORMAT_D32_FLOAT         }, // D16F
		{ DXGI_FORMAT_R32_TYPELESS,       DXGI_FORMAT_R32_FLOAT,             DXGI_FORMAT_D32_FLOAT         }, // D24F
		{ DXGI_FORMAT_R32_TYPELESS,       DXGI_FORMAT_R32_FLOAT,             DXGI_FORMAT_D32_FLOAT         }, // D32F
		{ DXGI_FORMAT_R24G8_TYPELESS,     DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT }, // D0S8
	};
	BX_STATIC_ASSERT(TextureFormat::Count == BX_COUNTOF(s_textureFormat) );

	static const D3D11_INPUT_ELEMENT_DESC s_attrib[] =
	{
		{ "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BITANGENT",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",        0, DXGI_FORMAT_R8G8B8A8_UINT,   0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",        1, DXGI_FORMAT_R8G8B8A8_UINT,   0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,   0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     2, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     3, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     4, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     5, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     6, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     7, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	BX_STATIC_ASSERT(Attrib::Count == BX_COUNTOF(s_attrib) );

	static const DXGI_FORMAT s_attribType[][4][2] =
	{
		{
			{ DXGI_FORMAT_R8_UINT,            DXGI_FORMAT_R8_UNORM           },
			{ DXGI_FORMAT_R8G8_UINT,          DXGI_FORMAT_R8G8_UNORM         },
			{ DXGI_FORMAT_R8G8B8A8_UINT,      DXGI_FORMAT_R8G8B8A8_UNORM     },
			{ DXGI_FORMAT_R8G8B8A8_UINT,      DXGI_FORMAT_R8G8B8A8_UNORM     },
		},
		{
			{ DXGI_FORMAT_R16_SINT,           DXGI_FORMAT_R16_SNORM          },
			{ DXGI_FORMAT_R16G16_SINT,        DXGI_FORMAT_R16G16_SNORM       },
			{ DXGI_FORMAT_R16G16B16A16_SINT,  DXGI_FORMAT_R16G16B16A16_SNORM },
			{ DXGI_FORMAT_R16G16B16A16_SINT,  DXGI_FORMAT_R16G16B16A16_SNORM },
		},
		{
			{ DXGI_FORMAT_R16_FLOAT,          DXGI_FORMAT_R16_FLOAT          },
			{ DXGI_FORMAT_R16G16_FLOAT,       DXGI_FORMAT_R16G16_FLOAT       },
			{ DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT },
			{ DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT },
		},
		{
			{ DXGI_FORMAT_R32_FLOAT,          DXGI_FORMAT_R32_FLOAT          },
			{ DXGI_FORMAT_R32G32_FLOAT,       DXGI_FORMAT_R32G32_FLOAT       },
			{ DXGI_FORMAT_R32G32B32_FLOAT,    DXGI_FORMAT_R32G32B32_FLOAT    },
			{ DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT },
		},
	};
	BX_STATIC_ASSERT(AttribType::Count == BX_COUNTOF(s_attribType) );

	static D3D11_INPUT_ELEMENT_DESC* fillVertexDecl(D3D11_INPUT_ELEMENT_DESC* _out, const VertexDecl& _decl)
	{
		D3D11_INPUT_ELEMENT_DESC* elem = _out;

		for (uint32_t attr = 0; attr < Attrib::Count; ++attr)
		{
			if (0xff != _decl.m_attributes[attr])
			{
				memcpy(elem, &s_attrib[attr], sizeof(D3D11_INPUT_ELEMENT_DESC) );

				if (0 == _decl.m_attributes[attr])
				{
					elem->AlignedByteOffset = 0;
				}
				else
				{
					uint8_t num;
					AttribType::Enum type;
					bool normalized;
					bool asInt;
					_decl.decode(Attrib::Enum(attr), num, type, normalized, asInt);
					elem->Format = s_attribType[type][num-1][normalized];
					elem->AlignedByteOffset = _decl.m_offset[attr];
				}

				++elem;
			}
		}

		return elem;
	}

	struct TextureStage
	{
		TextureStage()
		{
			clear();
		}

		void clear()
		{
			memset(m_srv, 0, sizeof(m_srv) );
			memset(m_sampler, 0, sizeof(m_sampler) );
		}

		ID3D11ShaderResourceView* m_srv[BGFX_CONFIG_MAX_TEXTURE_SAMPLERS];
		ID3D11SamplerState* m_sampler[BGFX_CONFIG_MAX_TEXTURE_SAMPLERS];
	};

	static const GUID WKPDID_D3DDebugObjectName = { 0x429b8c22, 0x9188, 0x4b0c, { 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 } };

	template <typename Ty>
	static BX_NO_INLINE void setDebugObjectName(Ty* _interface, const char* _format, ...)
	{
		if (BX_ENABLED(BGFX_CONFIG_DEBUG_OBJECT_NAME) )
		{
			char temp[2048];
			va_list argList;
			va_start(argList, _format);
			int size = bx::uint32_min(sizeof(temp)-1, vsnprintf(temp, sizeof(temp), _format, argList) );
			va_end(argList);
			temp[size] = '\0';

			_interface->SetPrivateData(WKPDID_D3DDebugObjectName, size, temp);
		}
	}

	static BX_NO_INLINE bool getIntelExtensions(ID3D11Device* _device)
	{
		uint8_t temp[28];

		D3D11_BUFFER_DESC desc;
		desc.ByteWidth = sizeof(temp);
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;

		D3D11_SUBRESOURCE_DATA initData;
		initData.pSysMem = &temp;
		initData.SysMemPitch = sizeof(temp);
		initData.SysMemSlicePitch = 0;

		bx::StaticMemoryBlockWriter writer(&temp, sizeof(temp) );
		bx::write(&writer, "INTCEXTNCAPSFUNC", 16);
		bx::write(&writer, UINT32_C(0x00010000) );
		bx::write(&writer, UINT32_C(0) );
		bx::write(&writer, UINT32_C(0) );

		ID3D11Buffer* buffer;
		HRESULT hr = _device->CreateBuffer(&desc, &initData, &buffer);

		if (SUCCEEDED(hr) )
		{
			buffer->Release();

			bx::MemoryReader reader(&temp, sizeof(temp) );
			bx::skip(&reader, 16);

			uint32_t version;
			bx::read(&reader, version);

			uint32_t driverVersion;
			bx::read(&reader, driverVersion);

			return version <= driverVersion;
		}

		return false;
	};

#if BGFX_CONFIG_DEBUG_PIX && BX_PLATFORM_WINDOWS
	bool findModule(const char* _name)
	{
		HANDLE process = GetCurrentProcess();
		DWORD size;
		BOOL result = EnumProcessModules(process
						, NULL
						, 0
						, &size
						);
		if (0 != result)
		{
			HMODULE* modules = (HMODULE*)alloca(size);
			result = EnumProcessModules(process
				, modules
				, size
				, &size
				);

			if (0 != result)
			{
				char moduleName[MAX_PATH];
				for (uint32_t ii = 0, num = uint32_t(size/sizeof(HMODULE) ); ii < num; ++ii)
				{
					result = GetModuleBaseNameA(process
								, modules[ii]
								, moduleName
								, BX_COUNTOF(moduleName)
								);
					if (0 != result
					&&  0 == bx::stricmp(_name, moduleName) )
					{
						return true;
					}
				}
			}
		}

		return false;
	}

#define RENDERDOC_IMPORT \
			RENDERDOC_IMPORT_FUNC(RENDERDOC_SetLogFile); \
			RENDERDOC_IMPORT_FUNC(RENDERDOC_GetCapture); \
			RENDERDOC_IMPORT_FUNC(RENDERDOC_SetCaptureOptions); \
			RENDERDOC_IMPORT_FUNC(RENDERDOC_SetActiveWindow); \
			RENDERDOC_IMPORT_FUNC(RENDERDOC_TriggerCapture); \
			RENDERDOC_IMPORT_FUNC(RENDERDOC_StartFrameCapture); \
			RENDERDOC_IMPORT_FUNC(RENDERDOC_EndFrameCapture); \
			RENDERDOC_IMPORT_FUNC(RENDERDOC_GetOverlayBits); \
			RENDERDOC_IMPORT_FUNC(RENDERDOC_MaskOverlayBits); \
			RENDERDOC_IMPORT_FUNC(RENDERDOC_SetFocusToggleKeys); \
			RENDERDOC_IMPORT_FUNC(RENDERDOC_SetCaptureKeys); \
			RENDERDOC_IMPORT_FUNC(RENDERDOC_InitRemoteAccess);

#define RENDERDOC_IMPORT_FUNC(_func) p##_func _func
	RENDERDOC_IMPORT
#undef RENDERDOC_IMPORT_FUNC

	pRENDERDOC_GetAPIVersion RENDERDOC_GetAPIVersion;

	void* loadRenderDoc()
	{
		// Skip loading RenderDoc when IntelGPA is present to avoid RenderDoc crash.
		if (findModule(BX_ARCH_32BIT ? "shimloader32.dll" : "shimloader64.dll") )
		{
			return NULL;
		}

		void* renderdocdll = bx::dlopen("renderdoc.dll");

		if (NULL != renderdocdll)
		{
			RENDERDOC_GetAPIVersion = (pRENDERDOC_GetAPIVersion)bx::dlsym(renderdocdll, "RENDERDOC_GetAPIVersion");
			if (NULL != RENDERDOC_GetAPIVersion
			&&  RENDERDOC_API_VERSION == RENDERDOC_GetAPIVersion() )
			{
#define RENDERDOC_IMPORT_FUNC(_func) \
			_func = (p##_func)bx::dlsym(renderdocdll, #_func); \
			BX_TRACE("%p " #_func, _func);
RENDERDOC_IMPORT
#undef RENDERDOC_IMPORT_FUNC

				RENDERDOC_SetLogFile(L"temp/bgfx");

				RENDERDOC_SetFocusToggleKeys(NULL, 0);

				KeyButton captureKey = eKey_F11;
				RENDERDOC_SetCaptureKeys(&captureKey, 1);

				CaptureOptions opt;
				memset(&opt, 0, sizeof(opt) );
				opt.AllowVSync      = 1;
				opt.SaveAllInitials = 1;
				RENDERDOC_SetCaptureOptions(&opt);

				uint32_t ident = 0;
				RENDERDOC_InitRemoteAccess(&ident);

				RENDERDOC_MaskOverlayBits(eOverlay_None, eOverlay_None);
			}
			else
			{
				bx::dlclose(renderdocdll);
				renderdocdll = NULL;
			}
		}

		return renderdocdll;
	}

	void unloadRenderDoc(void* _renderdocdll)
	{
		if (NULL != _renderdocdll)
		{
			bx::dlclose(_renderdocdll);
		}
	}
#else
	void* loadRenderDoc()
	{
		return NULL;
	}

	void unloadRenderDoc(void*)
	{
	}
#endif // BGFX_CONFIG_DEBUG_PIX

#if USE_D3D11_DYNAMIC_LIB
	static PFN_D3D11_CREATE_DEVICE D3D11CreateDevice;
	static PFN_CREATE_DXGI_FACTORY CreateDXGIFactory;
	static PFN_D3DPERF_SET_MARKER  D3DPERF_SetMarker;
	static PFN_D3DPERF_BEGIN_EVENT D3DPERF_BeginEvent;
	static PFN_D3DPERF_END_EVENT   D3DPERF_EndEvent;
#endif // USE_D3D11_DYNAMIC_LIB

	struct RendererContextD3D11 : public RendererContextI
	{
		RendererContextD3D11()
			: m_lost(0)
			, m_captureTexture(NULL)
			, m_captureResolve(NULL)
			, m_wireframe(false)
			, m_flags(BGFX_RESET_NONE)
			, m_vsChanges(0)
			, m_fsChanges(0)
			, m_rtMsaa(false)
		{
			m_renderdocdll = loadRenderDoc();

			m_fbh.idx = invalidHandle;
			memset(m_uniforms, 0, sizeof(m_uniforms) );
			memset(&m_resolution, 0, sizeof(m_resolution) );

#if USE_D3D11_DYNAMIC_LIB
			m_d3d11dll = bx::dlopen("d3d11.dll");
			BGFX_FATAL(NULL != m_d3d11dll, Fatal::UnableToInitialize, "Failed to load d3d11.dll.");

			m_d3d9dll = NULL;

			if (BX_ENABLED(BGFX_CONFIG_DEBUG_PIX) )
			{
				// D3D11_1.h has ID3DUserDefinedAnnotation
				// http://msdn.microsoft.com/en-us/library/windows/desktop/hh446881%28v=vs.85%29.aspx
				m_d3d9dll = bx::dlopen("d3d9.dll");
				BGFX_FATAL(NULL != m_d3d9dll, Fatal::UnableToInitialize, "Failed to load d3d9.dll.");

				D3DPERF_SetMarker  = (PFN_D3DPERF_SET_MARKER )bx::dlsym(m_d3d9dll, "D3DPERF_SetMarker" );
				D3DPERF_BeginEvent = (PFN_D3DPERF_BEGIN_EVENT)bx::dlsym(m_d3d9dll, "D3DPERF_BeginEvent");
				D3DPERF_EndEvent   = (PFN_D3DPERF_END_EVENT  )bx::dlsym(m_d3d9dll, "D3DPERF_EndEvent"  );
				BX_CHECK(NULL != D3DPERF_SetMarker
					  && NULL != D3DPERF_BeginEvent
					  && NULL != D3DPERF_EndEvent
					  , "Failed to initialize PIX events."
					  );
			}

			D3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE)bx::dlsym(m_d3d11dll, "D3D11CreateDevice");
			BGFX_FATAL(NULL != D3D11CreateDevice, Fatal::UnableToInitialize, "Function D3D11CreateDevice not found.");

			m_dxgidll = bx::dlopen("dxgi.dll");
			BGFX_FATAL(NULL != m_dxgidll, Fatal::UnableToInitialize, "Failed to load dxgi.dll.");

			CreateDXGIFactory = (PFN_CREATE_DXGI_FACTORY)bx::dlsym(m_dxgidll, "CreateDXGIFactory");
			BGFX_FATAL(NULL != CreateDXGIFactory, Fatal::UnableToInitialize, "Function CreateDXGIFactory not found.");
#endif // USE_D3D11_DYNAMIC_LIB

			HRESULT hr;

			IDXGIFactory* factory;
			hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory);
			BGFX_FATAL(SUCCEEDED(hr), Fatal::UnableToInitialize, "Unable to create DXGI factory.");

			m_adapter = NULL;
			m_driverType = D3D_DRIVER_TYPE_HARDWARE;

			IDXGIAdapter* adapter;
			for (uint32_t ii = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters(ii, &adapter); ++ii)
			{
				DXGI_ADAPTER_DESC desc;
				hr = adapter->GetDesc(&desc);
				if (SUCCEEDED(hr) )
				{
					BX_TRACE("Adapter #%d", ii);

					char description[BX_COUNTOF(desc.Description)];
					wcstombs(description, desc.Description, BX_COUNTOF(desc.Description) );
					BX_TRACE("\tDescription: %s", description);
					BX_TRACE("\tVendorId: 0x%08x, DeviceId: 0x%08x, SubSysId: 0x%08x, Revision: 0x%08x"
						, desc.VendorId
						, desc.DeviceId
						, desc.SubSysId
						, desc.Revision
						);
					BX_TRACE("\tMemory: %" PRIi64 " (video), %" PRIi64 " (system), %" PRIi64 " (shared)"
						, desc.DedicatedVideoMemory
						, desc.DedicatedSystemMemory
						, desc.SharedSystemMemory
						);

					if (BX_ENABLED(BGFX_CONFIG_DEBUG_PERFHUD)
					&&  0 != strstr(description, "PerfHUD") )
					{
						m_adapter = adapter;
						m_driverType = D3D_DRIVER_TYPE_REFERENCE;
					}
				}

				DX_RELEASE(adapter, adapter == m_adapter ? 1 : 0);
			}
			DX_RELEASE(factory, NULL != m_adapter ? 1 : 0);

			D3D_FEATURE_LEVEL features[] =
			{
				D3D_FEATURE_LEVEL_11_0,
				D3D_FEATURE_LEVEL_10_1,
				D3D_FEATURE_LEVEL_10_0,
			};

			uint32_t flags = D3D11_CREATE_DEVICE_SINGLETHREADED
				| BX_ENABLED(BGFX_CONFIG_DEBUG) ? D3D11_CREATE_DEVICE_DEBUG : 0
				;

			D3D_FEATURE_LEVEL featureLevel;

			hr = D3D11CreateDevice(m_adapter
				, m_driverType
				, NULL
				, flags
				, features
				, 1
				, D3D11_SDK_VERSION
				, &m_device
				, &featureLevel
				, &m_deviceCtx
				);
			BGFX_FATAL(SUCCEEDED(hr), Fatal::UnableToInitialize, "Unable to create Direct3D11 device.");

			IDXGIDevice* device;
			hr = m_device->QueryInterface(__uuidof(IDXGIDevice), (void**)&device);
			BGFX_FATAL(SUCCEEDED(hr), Fatal::UnableToInitialize, "Unable to create Direct3D11 device.");

			hr = device->GetParent(__uuidof(IDXGIAdapter), (void**)&adapter);
			BGFX_FATAL(SUCCEEDED(hr), Fatal::UnableToInitialize, "Unable to create Direct3D11 device.");

			// GPA increases device ref count.
			// RenderDoc makes device ref count 0 here.
			//
			// This causes assert in debug. When debugger is present refcount
			// checks are off.
			setGraphicsDebuggerPresent(2 != getRefCount(device) );
			DX_RELEASE(device, 2);

			hr = adapter->GetDesc(&m_adapterDesc);
			BGFX_FATAL(SUCCEEDED(hr), Fatal::UnableToInitialize, "Unable to create Direct3D11 device.");

			hr = adapter->GetParent(__uuidof(IDXGIFactory), (void**)&m_factory);
			BGFX_FATAL(SUCCEEDED(hr), Fatal::UnableToInitialize, "Unable to create Direct3D11 device.");
			DX_RELEASE(adapter, 2);

			memset(&m_scd, 0, sizeof(m_scd) );
			m_scd.BufferDesc.Width  = BGFX_DEFAULT_WIDTH;
			m_scd.BufferDesc.Height = BGFX_DEFAULT_HEIGHT;
			m_scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			m_scd.BufferDesc.RefreshRate.Numerator = 60;
			m_scd.BufferDesc.RefreshRate.Denominator = 1;
			m_scd.SampleDesc.Count = 1;
			m_scd.SampleDesc.Quality = 0;
			m_scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			m_scd.BufferCount = 1;
			m_scd.OutputWindow = g_bgfxHwnd;
			m_scd.Windowed = true;

			hr = m_factory->CreateSwapChain(m_device
										, &m_scd
										, &m_swapChain
										);
			BGFX_FATAL(SUCCEEDED(hr), Fatal::UnableToInitialize, "Failed to create swap chain.");

			m_numWindows = 1;

			if (BX_ENABLED(BGFX_CONFIG_DEBUG) )
			{
				ID3D11InfoQueue* infoQueue;
				hr = m_device->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&infoQueue);

				if (SUCCEEDED(hr) )
				{
					infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
					infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR,      true);
					infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING,    false);

					D3D11_INFO_QUEUE_FILTER filter;
					memset(&filter, 0, sizeof(filter) );

					D3D11_MESSAGE_CATEGORY categies[] =
					{
						D3D11_MESSAGE_CATEGORY_STATE_SETTING,
						D3D11_MESSAGE_CATEGORY_EXECUTION,
					};
					filter.DenyList.NumCategories = BX_COUNTOF(categies);
					filter.DenyList.pCategoryList = categies;
					infoQueue->PushStorageFilter(&filter);

					DX_RELEASE(infoQueue, 3);
				}
				else
				{
					// InfoQueue QueryInterface will fail when AMD GPU Perfstudio 2 is present.
					setGraphicsDebuggerPresent(true);
				}
			}

			UniformHandle handle = BGFX_INVALID_HANDLE;
			for (uint32_t ii = 0; ii < PredefinedUniform::Count; ++ii)
			{
				m_uniformReg.add(handle, getPredefinedUniformName(PredefinedUniform::Enum(ii) ), &m_predefinedUniforms[ii]);
			}

			g_caps.supported |= ( 0
								| BGFX_CAPS_TEXTURE_3D
								| BGFX_CAPS_TEXTURE_COMPARE_ALL
								| BGFX_CAPS_INSTANCING
								| BGFX_CAPS_VERTEX_ATTRIB_HALF
								| BGFX_CAPS_FRAGMENT_DEPTH
								| BGFX_CAPS_BLEND_INDEPENDENT
								| BGFX_CAPS_COMPUTE
								| (getIntelExtensions(m_device) ? BGFX_CAPS_FRAGMENT_ORDERING : 0)
								| BGFX_CAPS_SWAP_CHAIN
								);
			g_caps.maxTextureSize   = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
			g_caps.maxFBAttachments = bx::uint32_min(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS);

			for (uint32_t ii = 0; ii < TextureFormat::Count; ++ii)
			{
				g_caps.formats[ii] = DXGI_FORMAT_UNKNOWN == s_textureFormat[ii].m_fmt ? 0 : 1;
			}

			updateMsaa();
			postReset();
		}

		~RendererContextD3D11()
		{
			preReset();

			m_deviceCtx->ClearState();

			invalidateCache();

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_indexBuffers); ++ii)
			{
				m_indexBuffers[ii].destroy();
			}

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_vertexBuffers); ++ii)
			{
				m_vertexBuffers[ii].destroy();
			}

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_shaders); ++ii)
			{
				m_shaders[ii].destroy();
			}

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_textures); ++ii)
			{
				m_textures[ii].destroy();
			}

			DX_RELEASE(m_swapChain, 0);
			DX_RELEASE(m_deviceCtx, 0);
			DX_RELEASE(m_device, 0);
			DX_RELEASE(m_factory, 0);

			unloadRenderDoc(m_renderdocdll);

#if USE_D3D11_DYNAMIC_LIB
			bx::dlclose(m_dxgidll);
			bx::dlclose(m_d3d9dll);
			bx::dlclose(m_d3d11dll);
#endif // USE_D3D11_DYNAMIC_LIB
		}

		RendererType::Enum getRendererType() const BX_OVERRIDE
		{
			return RendererType::Direct3D11;
		}

		const char* getRendererName() const BX_OVERRIDE
		{
			return BGFX_RENDERER_DIRECT3D11_NAME;
		}

		void createIndexBuffer(IndexBufferHandle _handle, Memory* _mem) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].create(_mem->size, _mem->data);
		}

		void destroyIndexBuffer(IndexBufferHandle _handle) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].destroy();
		}

		void createVertexDecl(VertexDeclHandle _handle, const VertexDecl& _decl) BX_OVERRIDE
		{
			VertexDecl& decl = m_vertexDecls[_handle.idx];
			memcpy(&decl, &_decl, sizeof(VertexDecl) );
			dump(decl);
		}

		void destroyVertexDecl(VertexDeclHandle /*_handle*/) BX_OVERRIDE
		{
		}

		void createVertexBuffer(VertexBufferHandle _handle, Memory* _mem, VertexDeclHandle _declHandle) BX_OVERRIDE
		{
			m_vertexBuffers[_handle.idx].create(_mem->size, _mem->data, _declHandle);
		}

		void destroyVertexBuffer(VertexBufferHandle _handle) BX_OVERRIDE
		{
			m_vertexBuffers[_handle.idx].destroy();
		}

		void createDynamicIndexBuffer(IndexBufferHandle _handle, uint32_t _size) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].create(_size, NULL);
		}

		void updateDynamicIndexBuffer(IndexBufferHandle _handle, uint32_t _offset, uint32_t _size, Memory* _mem) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].update(_offset, bx::uint32_min(_size, _mem->size), _mem->data);
		}

		void destroyDynamicIndexBuffer(IndexBufferHandle _handle) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].destroy();
		}

		void createDynamicVertexBuffer(VertexBufferHandle _handle, uint32_t _size) BX_OVERRIDE
		{
			VertexDeclHandle decl = BGFX_INVALID_HANDLE;
			m_vertexBuffers[_handle.idx].create(_size, NULL, decl);
		}

		void updateDynamicVertexBuffer(VertexBufferHandle _handle, uint32_t _offset, uint32_t _size, Memory* _mem) BX_OVERRIDE
		{
			m_vertexBuffers[_handle.idx].update(_offset, bx::uint32_min(_size, _mem->size), _mem->data);
		}

		void destroyDynamicVertexBuffer(VertexBufferHandle _handle) BX_OVERRIDE
		{
			m_vertexBuffers[_handle.idx].destroy();
		}

		void createShader(ShaderHandle _handle, Memory* _mem) BX_OVERRIDE
		{
			m_shaders[_handle.idx].create(_mem);
		}

		void destroyShader(ShaderHandle _handle) BX_OVERRIDE
		{
			m_shaders[_handle.idx].destroy();
		}

		void createProgram(ProgramHandle _handle, ShaderHandle _vsh, ShaderHandle _fsh) BX_OVERRIDE
		{
			m_program[_handle.idx].create(&m_shaders[_vsh.idx], isValid(_fsh) ? &m_shaders[_fsh.idx] : NULL);
		}

		void destroyProgram(ProgramHandle _handle) BX_OVERRIDE
		{
			m_program[_handle.idx].destroy();
		}

		void createTexture(TextureHandle _handle, Memory* _mem, uint32_t _flags, uint8_t _skip) BX_OVERRIDE
		{
			m_textures[_handle.idx].create(_mem, _flags, _skip);
		}

		void updateTextureBegin(TextureHandle /*_handle*/, uint8_t /*_side*/, uint8_t /*_mip*/) BX_OVERRIDE
		{
		}

		void updateTexture(TextureHandle _handle, uint8_t _side, uint8_t _mip, const Rect& _rect, uint16_t _z, uint16_t _depth, uint16_t _pitch, const Memory* _mem) BX_OVERRIDE
		{
			m_textures[_handle.idx].update(_side, _mip, _rect, _z, _depth, _pitch, _mem);
		}

		void updateTextureEnd() BX_OVERRIDE
		{
		}

		void destroyTexture(TextureHandle _handle) BX_OVERRIDE
		{
			m_textures[_handle.idx].destroy();
		}

		void createFrameBuffer(FrameBufferHandle _handle, uint8_t _num, const TextureHandle* _textureHandles) BX_OVERRIDE
		{
			m_frameBuffers[_handle.idx].create(_num, _textureHandles);
		}

		void createFrameBuffer(FrameBufferHandle _handle, void* _nwh, uint32_t _width, uint32_t _height, TextureFormat::Enum _depthFormat) BX_OVERRIDE
		{
			uint16_t denseIdx = m_numWindows++;
			m_windows[denseIdx] = _handle;
			m_frameBuffers[_handle.idx].create(denseIdx, _nwh, _width, _height, _depthFormat);
		}

		void destroyFrameBuffer(FrameBufferHandle _handle) BX_OVERRIDE
		{
			uint16_t denseIdx = m_frameBuffers[_handle.idx].destroy();
			if (UINT16_MAX != denseIdx)
			{
				--m_numWindows;
				if (m_numWindows > 1)
				{
					FrameBufferHandle handle = m_windows[m_numWindows];
					m_windows[denseIdx] = handle;
					m_frameBuffers[handle.idx].m_denseIdx = denseIdx;
				}
			}
		}

		void createUniform(UniformHandle _handle, UniformType::Enum _type, uint16_t _num, const char* _name) BX_OVERRIDE
		{
			if (NULL != m_uniforms[_handle.idx])
			{
				BX_FREE(g_allocator, m_uniforms[_handle.idx]);
			}

			uint32_t size = BX_ALIGN_16(g_uniformTypeSize[_type]*_num);
			void* data = BX_ALLOC(g_allocator, size);
			memset(data, 0, size);
			m_uniforms[_handle.idx] = data;
			m_uniformReg.add(_handle, _name, data);
		}

		void destroyUniform(UniformHandle _handle) BX_OVERRIDE
		{
			BX_FREE(g_allocator, m_uniforms[_handle.idx]);
			m_uniforms[_handle.idx] = NULL;
		}

		void saveScreenShot(const char* _filePath) BX_OVERRIDE
		{
			ID3D11Texture2D* backBuffer;
			DX_CHECK(m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer) );

			D3D11_TEXTURE2D_DESC backBufferDesc;
			backBuffer->GetDesc(&backBufferDesc);

			D3D11_TEXTURE2D_DESC desc;
			memcpy(&desc, &backBufferDesc, sizeof(desc) );
			desc.SampleDesc.Count = 1;
			desc.SampleDesc.Quality = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.BindFlags = 0;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

			ID3D11Texture2D* texture;
			HRESULT hr = m_device->CreateTexture2D(&desc, NULL, &texture);
			if (SUCCEEDED(hr) )
			{
				if (backBufferDesc.SampleDesc.Count == 1)
				{
					m_deviceCtx->CopyResource(texture, backBuffer);
				}
				else
				{
					desc.Usage = D3D11_USAGE_DEFAULT;
					desc.CPUAccessFlags = 0;
					ID3D11Texture2D* resolve;
					HRESULT hr = m_device->CreateTexture2D(&desc, NULL, &resolve);
					if (SUCCEEDED(hr) )
					{
						m_deviceCtx->ResolveSubresource(resolve, 0, backBuffer, 0, desc.Format);
						m_deviceCtx->CopyResource(texture, resolve);
						DX_RELEASE(resolve, 0);
					}
				}

				D3D11_MAPPED_SUBRESOURCE mapped;
				DX_CHECK(m_deviceCtx->Map(texture, 0, D3D11_MAP_READ, 0, &mapped) );
				g_callback->screenShot(_filePath
					, backBufferDesc.Width
					, backBufferDesc.Height
					, mapped.RowPitch
					, mapped.pData
					, backBufferDesc.Height*mapped.RowPitch
					, false
					);
				m_deviceCtx->Unmap(texture, 0);

				DX_RELEASE(texture, 0);
			}

			DX_RELEASE(backBuffer, 0);
		}

		void updateViewName(uint8_t _id, const char* _name) BX_OVERRIDE
		{
			mbstowcs(&s_viewNameW[_id][0], _name, BX_COUNTOF(s_viewNameW[0]) );
		}

		void updateUniform(uint16_t _loc, const void* _data, uint32_t _size) BX_OVERRIDE
		{
			memcpy(m_uniforms[_loc], _data, _size);
		}

		void setMarker(const char* _marker, uint32_t _size) BX_OVERRIDE
		{
			if (BX_ENABLED(BGFX_CONFIG_DEBUG_PIX) )
			{
				uint32_t size = _size*sizeof(wchar_t);
				wchar_t* name = (wchar_t*)alloca(size);
				mbstowcs(name, _marker, size-2);
				PIX_SETMARKER(D3DCOLOR_RGBA(0xff, 0xff, 0xff, 0xff), name);
			}
		}

		void submit(Frame* _render, ClearQuad& _clearQuad, TextVideoMemBlitter& _textVideoMemBlitter) BX_OVERRIDE;

		void blitSetup(TextVideoMemBlitter& _blitter) BX_OVERRIDE
		{
			ID3D11DeviceContext* deviceCtx = m_deviceCtx;

			uint32_t width  = m_scd.BufferDesc.Width;
			uint32_t height = m_scd.BufferDesc.Height;

			FrameBufferHandle fbh = BGFX_INVALID_HANDLE;
			setFrameBuffer(fbh, false);

			D3D11_VIEWPORT vp;
			vp.TopLeftX = 0;
			vp.TopLeftY = 0;
			vp.Width    = (float)width;
			vp.Height   = (float)height;
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			deviceCtx->RSSetViewports(1, &vp);

			uint64_t state = BGFX_STATE_RGB_WRITE
				| BGFX_STATE_ALPHA_WRITE
				| BGFX_STATE_DEPTH_TEST_ALWAYS
				;

			setBlendState(state);
			setDepthStencilState(state);
			setRasterizerState(state);

			ProgramD3D11& program = m_program[_blitter.m_program.idx];
			m_currentProgram = &program;
			deviceCtx->VSSetShader(program.m_vsh->m_vertexShader, NULL, 0);
			deviceCtx->VSSetConstantBuffers(0, 1, &program.m_vsh->m_buffer);
			deviceCtx->PSSetShader(program.m_fsh->m_pixelShader, NULL, 0);
			deviceCtx->PSSetConstantBuffers(0, 1, &program.m_fsh->m_buffer);

			VertexBufferD3D11& vb = m_vertexBuffers[_blitter.m_vb->handle.idx];
			VertexDecl& vertexDecl = m_vertexDecls[_blitter.m_vb->decl.idx];
			uint32_t stride = vertexDecl.m_stride;
			uint32_t offset = 0;
			deviceCtx->IASetVertexBuffers(0, 1, &vb.m_ptr, &stride, &offset);
			setInputLayout(vertexDecl, program, 0);

			IndexBufferD3D11& ib = m_indexBuffers[_blitter.m_ib->handle.idx];
			deviceCtx->IASetIndexBuffer(ib.m_ptr, DXGI_FORMAT_R16_UINT, 0);

			float proj[16];
			mtxOrtho(proj, 0.0f, (float)width, (float)height, 0.0f, 0.0f, 1000.0f);

			PredefinedUniform& predefined = program.m_predefined[0];
			uint8_t flags = predefined.m_type;
			setShaderConstant(flags, predefined.m_loc, proj, 4);

			commitShaderConstants();
			m_textures[_blitter.m_texture.idx].commit(0);
			commitTextureStage();
		}

		void blitRender(TextVideoMemBlitter& _blitter, uint32_t _numIndices) BX_OVERRIDE
		{
			ID3D11DeviceContext* deviceCtx = m_deviceCtx;

			uint32_t numVertices = _numIndices*4/6;
			m_indexBuffers [_blitter.m_ib->handle.idx].update(0, _numIndices*2, _blitter.m_ib->data);
			m_vertexBuffers[_blitter.m_vb->handle.idx].update(0, numVertices*_blitter.m_decl.m_stride, _blitter.m_vb->data, true);

			deviceCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			deviceCtx->DrawIndexed(_numIndices, 0, 0);
		}

		void preReset()
		{
			DX_RELEASE(m_backBufferDepthStencil, 0);
			DX_RELEASE(m_backBufferColor, 0);

//			invalidateCache();

			capturePreReset();
		}

		void postReset()
		{
			ID3D11Texture2D* color;
			DX_CHECK(m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&color) );

			DX_CHECK(m_device->CreateRenderTargetView(color, NULL, &m_backBufferColor) );
			DX_RELEASE(color, 0);

			D3D11_TEXTURE2D_DESC dsd;
			dsd.Width = m_scd.BufferDesc.Width;
			dsd.Height = m_scd.BufferDesc.Height;
			dsd.MipLevels = 1;
			dsd.ArraySize = 1;
			dsd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			dsd.SampleDesc = m_scd.SampleDesc;
			dsd.Usage = D3D11_USAGE_DEFAULT;
			dsd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			dsd.CPUAccessFlags = 0;
			dsd.MiscFlags = 0;

			ID3D11Texture2D* depthStencil;
			DX_CHECK(m_device->CreateTexture2D(&dsd, NULL, &depthStencil) );
			DX_CHECK(m_device->CreateDepthStencilView(depthStencil, NULL, &m_backBufferDepthStencil) );
			DX_RELEASE(depthStencil, 0);

			m_deviceCtx->OMSetRenderTargets(1, &m_backBufferColor, m_backBufferDepthStencil);

			m_currentColor = m_backBufferColor;
			m_currentDepthStencil = m_backBufferDepthStencil;

			capturePostReset();
		}

		static bool isLost(HRESULT _hr)
		{
			return DXGI_ERROR_DEVICE_REMOVED == _hr
				|| DXGI_ERROR_DEVICE_HUNG == _hr
				|| DXGI_ERROR_DEVICE_RESET == _hr
				|| DXGI_ERROR_DRIVER_INTERNAL_ERROR == _hr
				|| DXGI_ERROR_NOT_CURRENTLY_AVAILABLE == _hr
				;
		}

		void flip() BX_OVERRIDE
		{
			if (NULL != m_swapChain)
			{
				HRESULT hr = 0;
				uint32_t syncInterval = !!(m_flags & BGFX_RESET_VSYNC);
				for (uint32_t ii = 1, num = m_numWindows; ii < num && SUCCEEDED(hr); ++ii)
				{
					hr = m_frameBuffers[m_windows[ii].idx].m_swapChain->Present(syncInterval, 0);
				}

				if (SUCCEEDED(hr) )
				{
					hr = m_swapChain->Present(syncInterval, 0);
				}

				if (FAILED(hr)
				&&  isLost(hr) )
				{
					++m_lost;
					BGFX_FATAL(10 > m_lost, bgfx::Fatal::DeviceLost, "Device is lost. FAILED 0x%08x", hr);
				}
				else
				{
					m_lost = 0;
				}
			}
		}

		void invalidateCache()
		{
			m_inputLayoutCache.invalidate();
			m_blendStateCache.invalidate();
			m_depthStencilStateCache.invalidate();
			m_rasterizerStateCache.invalidate();
			m_samplerStateCache.invalidate();
		}

		void updateMsaa()
		{
			for (uint32_t ii = 1, last = 0; ii < BX_COUNTOF(s_msaa); ++ii)
			{
				uint32_t msaa = s_checkMsaa[ii];
				uint32_t quality = 0;
				HRESULT hr = m_device->CheckMultisampleQualityLevels(m_scd.BufferDesc.Format, msaa, &quality);

				if (SUCCEEDED(hr)
				&&  0 < quality)
				{
					s_msaa[ii].Count = msaa;
					s_msaa[ii].Quality = quality - 1;
					last = ii;
				}
				else
				{
					s_msaa[ii] = s_msaa[last];
				}
			}
		}

		void updateResolution(const Resolution& _resolution)
		{
			if ( (uint32_t)m_scd.BufferDesc.Width != _resolution.m_width
			||   (uint32_t)m_scd.BufferDesc.Height != _resolution.m_height
			||   m_flags != _resolution.m_flags)
			{
				bool resize = (m_flags&BGFX_RESET_MSAA_MASK) == (_resolution.m_flags&BGFX_RESET_MSAA_MASK);
				m_flags = _resolution.m_flags;

				m_textVideoMem.resize(false, _resolution.m_width, _resolution.m_height);
				m_textVideoMem.clear();

				m_resolution = _resolution;

				m_scd.BufferDesc.Width = _resolution.m_width;
				m_scd.BufferDesc.Height = _resolution.m_height;

				preReset();

				if (resize)
				{
					DX_CHECK(m_swapChain->ResizeBuffers(2
						, m_scd.BufferDesc.Width
						, m_scd.BufferDesc.Height
						, m_scd.BufferDesc.Format
						, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
						) );
				}
				else
				{
					updateMsaa();
					m_scd.SampleDesc = s_msaa[(m_flags&BGFX_RESET_MSAA_MASK)>>BGFX_RESET_MSAA_SHIFT];

					DX_RELEASE(m_swapChain, 0);

					HRESULT hr;
					hr = m_factory->CreateSwapChain(m_device
						, &m_scd
						, &m_swapChain
						);
					BGFX_FATAL(SUCCEEDED(hr), bgfx::Fatal::UnableToInitialize, "Failed to create swap chain.");
				}

				postReset();
			}
		}

		void setShaderConstant(uint8_t _flags, uint16_t _regIndex, const void* _val, uint16_t _numRegs)
		{
			if (_flags&BGFX_UNIFORM_FRAGMENTBIT)
			{
				memcpy(&m_fsScratch[_regIndex], _val, _numRegs*16);
				m_fsChanges += _numRegs;
			}
			else
			{
				memcpy(&m_vsScratch[_regIndex], _val, _numRegs*16);
				m_vsChanges += _numRegs;
			}
		}

		void commitShaderConstants()
		{
			if (0 < m_vsChanges)
			{
				if (NULL != m_currentProgram->m_vsh->m_buffer)
				{
					m_deviceCtx->UpdateSubresource(m_currentProgram->m_vsh->m_buffer, 0, 0, m_vsScratch, 0, 0);
				}

				m_vsChanges = 0;
			}

			if (0 < m_fsChanges)
			{
				if (NULL != m_currentProgram->m_fsh->m_buffer)
				{
					m_deviceCtx->UpdateSubresource(m_currentProgram->m_fsh->m_buffer, 0, 0, m_fsScratch, 0, 0);
				}

				m_fsChanges = 0;
			}
		}

		void setFrameBuffer(FrameBufferHandle _fbh, bool _msaa = true)
		{
			BX_UNUSED(_msaa);
			if (!isValid(_fbh) )
			{
				m_deviceCtx->OMSetRenderTargets(1, &m_backBufferColor, m_backBufferDepthStencil);

				m_currentColor = m_backBufferColor;
				m_currentDepthStencil = m_backBufferDepthStencil;
			}
			else
			{
				invalidateTextureStage();

				FrameBufferD3D11& frameBuffer = m_frameBuffers[_fbh.idx];
				m_deviceCtx->OMSetRenderTargets(frameBuffer.m_num, frameBuffer.m_rtv, frameBuffer.m_dsv);

				m_currentColor = frameBuffer.m_rtv[0];
				m_currentDepthStencil = frameBuffer.m_dsv;
			}

			if (isValid(m_fbh)
			&&  m_fbh.idx != _fbh.idx
			&&  m_rtMsaa)
			{
				FrameBufferD3D11& frameBuffer = m_frameBuffers[m_fbh.idx];
				frameBuffer.resolve();
			}

			m_fbh = _fbh;
			m_rtMsaa = _msaa;
		}

		void clear(const Clear& _clear, const float _palette[][4])
		{
			if (isValid(m_fbh) )
			{
				FrameBufferD3D11& frameBuffer = m_frameBuffers[m_fbh.idx];
				frameBuffer.clear(_clear, _palette);
			}
			else
			{
				if (NULL != m_currentColor
				&&  BGFX_CLEAR_COLOR_BIT & _clear.m_flags)
				{
					if (BGFX_CLEAR_COLOR_USE_PALETTE_BIT & _clear.m_flags)
					{
						uint8_t index = _clear.m_index[0];
						if (UINT8_MAX != index)
						{
							m_deviceCtx->ClearRenderTargetView(m_currentColor, _palette[index]);
						}
					}
					else
					{
						float frgba[4] =
						{ 
							_clear.m_index[0]*1.0f/255.0f,
							_clear.m_index[1]*1.0f/255.0f,
							_clear.m_index[2]*1.0f/255.0f,
							_clear.m_index[3]*1.0f/255.0f,
						};
						m_deviceCtx->ClearRenderTargetView(m_currentColor, frgba);
					}
				}

				if (NULL != m_currentDepthStencil
				&& (BGFX_CLEAR_DEPTH_BIT|BGFX_CLEAR_STENCIL_BIT) & _clear.m_flags)
				{
					DWORD flags = 0;
					flags |= (_clear.m_flags & BGFX_CLEAR_DEPTH_BIT) ? D3D11_CLEAR_DEPTH : 0;
					flags |= (_clear.m_flags & BGFX_CLEAR_STENCIL_BIT) ? D3D11_CLEAR_STENCIL : 0;
					m_deviceCtx->ClearDepthStencilView(m_currentDepthStencil, flags, _clear.m_depth, _clear.m_stencil);
				}
			}
		}

		void setInputLayout(const VertexDecl& _vertexDecl, const ProgramD3D11& _program, uint8_t _numInstanceData)
		{
			uint64_t layoutHash = (uint64_t(_vertexDecl.m_hash)<<32) | _program.m_vsh->m_hash;
			layoutHash ^= _numInstanceData;
			ID3D11InputLayout* layout = m_inputLayoutCache.find(layoutHash);
			if (NULL == layout)
			{
				D3D11_INPUT_ELEMENT_DESC vertexElements[Attrib::Count+1+BGFX_CONFIG_MAX_INSTANCE_DATA_COUNT];

				VertexDecl decl;
				memcpy(&decl, &_vertexDecl, sizeof(VertexDecl) );
				const uint8_t* attrMask = _program.m_vsh->m_attrMask;

				for (uint32_t ii = 0; ii < Attrib::Count; ++ii)
				{
					uint8_t mask = attrMask[ii];
					uint8_t attr = (decl.m_attributes[ii] & mask);
					decl.m_attributes[ii] = attr == 0 ? 0xff : attr == 0xff ? 0 : attr;
				}

				D3D11_INPUT_ELEMENT_DESC* elem = fillVertexDecl(vertexElements, decl);
				uint32_t num = uint32_t(elem-vertexElements);

				const D3D11_INPUT_ELEMENT_DESC inst = { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_INSTANCE_DATA, 1 };

				for (uint32_t ii = 0; ii < _numInstanceData; ++ii)
				{
					uint32_t index = 8-_numInstanceData+ii;

					uint32_t jj;
					D3D11_INPUT_ELEMENT_DESC* curr = vertexElements;
					for (jj = 0; jj < num; ++jj)
					{
						curr = &vertexElements[jj];
						if (0 == strcmp(curr->SemanticName, "TEXCOORD")
						&&  curr->SemanticIndex == index)
						{
							break;
						}
					}

					if (jj == num)
					{
						curr = elem;
						++elem;
					}

					memcpy(curr, &inst, sizeof(D3D11_INPUT_ELEMENT_DESC) );
					curr->InputSlot = 1;
					curr->SemanticIndex = index;
					curr->AlignedByteOffset = ii*16;
				}

				num = uint32_t(elem-vertexElements);
				DX_CHECK(m_device->CreateInputLayout(vertexElements
					, num
					, _program.m_vsh->m_code->data
					, _program.m_vsh->m_code->size
					, &layout
					) );
				m_inputLayoutCache.add(layoutHash, layout);
			}

			m_deviceCtx->IASetInputLayout(layout);
		}

		void setBlendState(uint64_t _state, uint32_t _rgba = 0)
		{
			_state &= 0
				| BGFX_STATE_BLEND_MASK
				| BGFX_STATE_BLEND_EQUATION_MASK
				| BGFX_STATE_BLEND_INDEPENDENT
				| BGFX_STATE_ALPHA_WRITE
				| BGFX_STATE_RGB_WRITE
				;

			bx::HashMurmur2A murmur;
			murmur.begin();
			murmur.add(_state);

			const uint64_t f0 = BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_FACTOR, BGFX_STATE_BLEND_FACTOR);
			const uint64_t f1 = BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_INV_FACTOR, BGFX_STATE_BLEND_INV_FACTOR);
			bool hasFactor = f0 == (_state & f0) 
				|| f1 == (_state & f1)
				;

			float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			if (hasFactor)
			{
				blendFactor[0] = ( (_rgba>>24)     )/255.0f;
				blendFactor[1] = ( (_rgba>>16)&0xff)/255.0f;
				blendFactor[2] = ( (_rgba>> 8)&0xff)/255.0f;
				blendFactor[3] = ( (_rgba    )&0xff)/255.0f;
			}
			else
			{
				murmur.add(_rgba);
			}

			uint32_t hash = murmur.end();

			ID3D11BlendState* bs = m_blendStateCache.find(hash);
			if (NULL == bs)
			{
				D3D11_BLEND_DESC desc;
				memset(&desc, 0, sizeof(desc) );
				desc.IndependentBlendEnable = !!(BGFX_STATE_BLEND_INDEPENDENT & _state);

				D3D11_RENDER_TARGET_BLEND_DESC* drt = &desc.RenderTarget[0];
				drt->BlendEnable = !!(BGFX_STATE_BLEND_MASK & _state);

				const uint32_t blend    = uint32_t( (_state&BGFX_STATE_BLEND_MASK)>>BGFX_STATE_BLEND_SHIFT);
				const uint32_t equation = uint32_t( (_state&BGFX_STATE_BLEND_EQUATION_MASK)>>BGFX_STATE_BLEND_EQUATION_SHIFT);

				const uint32_t srcRGB = (blend    )&0xf;
				const uint32_t dstRGB = (blend>> 4)&0xf;
				const uint32_t srcA   = (blend>> 8)&0xf;
				const uint32_t dstA   = (blend>>12)&0xf;

				const uint32_t equRGB = (equation   )&0x7;
				const uint32_t equA   = (equation>>3)&0x7;

				drt->SrcBlend       = s_blendFactor[srcRGB][0];
				drt->DestBlend      = s_blendFactor[dstRGB][0];
				drt->BlendOp        = s_blendEquation[equRGB];

				drt->SrcBlendAlpha  = s_blendFactor[srcA][1];
				drt->DestBlendAlpha = s_blendFactor[dstA][1];
				drt->BlendOpAlpha   = s_blendEquation[equA];

				uint32_t writeMask = (_state&BGFX_STATE_ALPHA_WRITE) ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0;
				writeMask |= (_state&BGFX_STATE_RGB_WRITE) ? D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE : 0;

				drt->RenderTargetWriteMask = writeMask;

				if (desc.IndependentBlendEnable)
				{
					for (uint32_t ii = 1, rgba = _rgba; ii < BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS; ++ii, rgba >>= 11)
					{
						drt = &desc.RenderTarget[ii];
						drt->BlendEnable = 0 != (rgba&0x7ff);

						const uint32_t src      = (rgba   )&0xf;
						const uint32_t dst      = (rgba>>4)&0xf;
						const uint32_t equation = (rgba>>8)&0x7;

						drt->SrcBlend       = s_blendFactor[src][0];
						drt->DestBlend      = s_blendFactor[dst][0];
						drt->BlendOp        = s_blendEquation[equation];

						drt->SrcBlendAlpha  = s_blendFactor[src][1];
						drt->DestBlendAlpha = s_blendFactor[dst][1];
						drt->BlendOpAlpha   = s_blendEquation[equation];

						drt->RenderTargetWriteMask = writeMask;
					}
				}
				else
				{
					for (uint32_t ii = 1; ii < BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS; ++ii)
					{
						memcpy(&desc.RenderTarget[ii], drt, sizeof(D3D11_RENDER_TARGET_BLEND_DESC) );
					}
				}

				DX_CHECK(m_device->CreateBlendState(&desc, &bs) );

				m_blendStateCache.add(hash, bs);
			}

			m_deviceCtx->OMSetBlendState(bs, blendFactor, 0xffffffff);
		}

		void setDepthStencilState(uint64_t _state, uint64_t _stencil = 0)
		{
			_state &= BGFX_STATE_DEPTH_WRITE|BGFX_STATE_DEPTH_TEST_MASK;

			uint32_t fstencil = unpackStencil(0, _stencil);
			uint32_t ref = (fstencil&BGFX_STENCIL_FUNC_REF_MASK)>>BGFX_STENCIL_FUNC_REF_SHIFT;
			_stencil &= packStencil(~BGFX_STENCIL_FUNC_REF_MASK, BGFX_STENCIL_MASK);

			bx::HashMurmur2A murmur;
			murmur.begin();
			murmur.add(_state);
			murmur.add(_stencil);
			uint32_t hash = murmur.end();

			ID3D11DepthStencilState* dss = m_depthStencilStateCache.find(hash);
			if (NULL == dss)
			{
				D3D11_DEPTH_STENCIL_DESC desc;
				memset(&desc, 0, sizeof(desc) );
				uint32_t func = (_state&BGFX_STATE_DEPTH_TEST_MASK)>>BGFX_STATE_DEPTH_TEST_SHIFT;
				desc.DepthEnable = 0 != func;
				desc.DepthWriteMask = !!(BGFX_STATE_DEPTH_WRITE & _state) ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
				desc.DepthFunc = s_cmpFunc[func];

				uint32_t bstencil = unpackStencil(1, _stencil);
				uint32_t frontAndBack = bstencil != BGFX_STENCIL_NONE && bstencil != fstencil;
				bstencil = frontAndBack ? bstencil : fstencil;

				desc.StencilEnable = 0 != _stencil;
				desc.StencilReadMask = (fstencil&BGFX_STENCIL_FUNC_RMASK_MASK)>>BGFX_STENCIL_FUNC_RMASK_SHIFT;
				desc.StencilWriteMask = 0xff;
				desc.FrontFace.StencilFailOp      = s_stencilOp[(fstencil&BGFX_STENCIL_OP_FAIL_S_MASK)>>BGFX_STENCIL_OP_FAIL_S_SHIFT];
				desc.FrontFace.StencilDepthFailOp = s_stencilOp[(fstencil&BGFX_STENCIL_OP_FAIL_Z_MASK)>>BGFX_STENCIL_OP_FAIL_Z_SHIFT];
				desc.FrontFace.StencilPassOp      = s_stencilOp[(fstencil&BGFX_STENCIL_OP_PASS_Z_MASK)>>BGFX_STENCIL_OP_PASS_Z_SHIFT];
				desc.FrontFace.StencilFunc        = s_cmpFunc[(fstencil&BGFX_STENCIL_TEST_MASK)>>BGFX_STENCIL_TEST_SHIFT];
				desc.BackFace.StencilFailOp       = s_stencilOp[(bstencil&BGFX_STENCIL_OP_FAIL_S_MASK)>>BGFX_STENCIL_OP_FAIL_S_SHIFT];
				desc.BackFace.StencilDepthFailOp  = s_stencilOp[(bstencil&BGFX_STENCIL_OP_FAIL_Z_MASK)>>BGFX_STENCIL_OP_FAIL_Z_SHIFT];
				desc.BackFace.StencilPassOp       = s_stencilOp[(bstencil&BGFX_STENCIL_OP_PASS_Z_MASK)>>BGFX_STENCIL_OP_PASS_Z_SHIFT];
				desc.BackFace.StencilFunc         = s_cmpFunc[(bstencil&BGFX_STENCIL_TEST_MASK)>>BGFX_STENCIL_TEST_SHIFT];

				DX_CHECK(m_device->CreateDepthStencilState(&desc, &dss) );

				m_depthStencilStateCache.add(hash, dss);
			}

			m_deviceCtx->OMSetDepthStencilState(dss, ref);
		}

		void setDebugWireframe(bool _wireframe)
		{
			if (m_wireframe != _wireframe)
			{
				m_wireframe = _wireframe;
				m_rasterizerStateCache.invalidate();
			}
		}

		void setRasterizerState(uint64_t _state, bool _wireframe = false, bool _scissor = false)
		{
			_state &= BGFX_STATE_CULL_MASK|BGFX_STATE_MSAA;
			_state |= _wireframe ? BGFX_STATE_PT_LINES : BGFX_STATE_NONE;
			_state |= _scissor ? BGFX_STATE_RESERVED_MASK : 0;

			ID3D11RasterizerState* rs = m_rasterizerStateCache.find(_state);
			if (NULL == rs)
			{
				uint32_t cull = (_state&BGFX_STATE_CULL_MASK)>>BGFX_STATE_CULL_SHIFT;

				D3D11_RASTERIZER_DESC desc;
				desc.FillMode = _wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
				desc.CullMode = s_cullMode[cull];
				desc.FrontCounterClockwise = false;
				desc.DepthBias = 0;
				desc.DepthBiasClamp = 0.0f;
				desc.SlopeScaledDepthBias = 0.0f;
				desc.DepthClipEnable = false;
				desc.ScissorEnable = _scissor;
				desc.MultisampleEnable = !!(_state&BGFX_STATE_MSAA);
				desc.AntialiasedLineEnable = false;

				DX_CHECK(m_device->CreateRasterizerState(&desc, &rs) );

				m_rasterizerStateCache.add(_state, rs);
			}

			m_deviceCtx->RSSetState(rs);
		}

		ID3D11SamplerState* getSamplerState(uint32_t _flags)
		{
			_flags &= BGFX_TEXTURE_SAMPLER_BITS_MASK;
			ID3D11SamplerState* sampler = m_samplerStateCache.find(_flags);
			if (NULL == sampler)
			{
				const uint32_t cmpFunc = (_flags&BGFX_TEXTURE_COMPARE_MASK)>>BGFX_TEXTURE_COMPARE_SHIFT;
				const uint8_t minFilter = s_textureFilter[0][(_flags&BGFX_TEXTURE_MIN_MASK)>>BGFX_TEXTURE_MIN_SHIFT];
				const uint8_t magFilter = s_textureFilter[1][(_flags&BGFX_TEXTURE_MAG_MASK)>>BGFX_TEXTURE_MAG_SHIFT];
				const uint8_t mipFilter = s_textureFilter[2][(_flags&BGFX_TEXTURE_MIP_MASK)>>BGFX_TEXTURE_MIP_SHIFT];
				const uint8_t filter = 0 == cmpFunc ? 0 : D3D11_COMPARISON_FILTERING_BIT;

				D3D11_SAMPLER_DESC sd;
				sd.Filter = (D3D11_FILTER)(filter|minFilter|magFilter|mipFilter);
				sd.AddressU = s_textureAddress[(_flags&BGFX_TEXTURE_U_MASK)>>BGFX_TEXTURE_U_SHIFT];
				sd.AddressV = s_textureAddress[(_flags&BGFX_TEXTURE_V_MASK)>>BGFX_TEXTURE_V_SHIFT];
				sd.AddressW = s_textureAddress[(_flags&BGFX_TEXTURE_W_MASK)>>BGFX_TEXTURE_W_SHIFT];
				sd.MipLODBias = 0.0f;
				sd.MaxAnisotropy = 1;
				sd.ComparisonFunc = 0 == cmpFunc ? D3D11_COMPARISON_NEVER : s_cmpFunc[cmpFunc];
				sd.BorderColor[0] = 0.0f;
				sd.BorderColor[1] = 0.0f;
				sd.BorderColor[2] = 0.0f;
				sd.BorderColor[3] = 0.0f;
				sd.MinLOD = 0;
				sd.MaxLOD = D3D11_FLOAT32_MAX;

				m_device->CreateSamplerState(&sd, &sampler);
				DX_CHECK_REFCOUNT(sampler, 1);

				m_samplerStateCache.add(_flags, sampler);
			}

			return sampler;
		}

		void commitTextureStage()
		{
			m_deviceCtx->PSSetShaderResources(0, BGFX_CONFIG_MAX_TEXTURE_SAMPLERS, m_textureStage.m_srv);
			m_deviceCtx->PSSetSamplers(0, BGFX_CONFIG_MAX_TEXTURE_SAMPLERS, m_textureStage.m_sampler);
		}

		void invalidateTextureStage()
		{
			m_textureStage.clear();
			commitTextureStage();
		}

		void capturePostReset()
		{
			if (m_flags&BGFX_RESET_CAPTURE)
			{
				ID3D11Texture2D* backBuffer;
				DX_CHECK(m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer) );

				D3D11_TEXTURE2D_DESC backBufferDesc;
				backBuffer->GetDesc(&backBufferDesc);

				D3D11_TEXTURE2D_DESC desc;
				memcpy(&desc, &backBufferDesc, sizeof(desc) );
				desc.SampleDesc.Count = 1;
				desc.SampleDesc.Quality = 0;
				desc.Usage = D3D11_USAGE_STAGING;
				desc.BindFlags = 0;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

				HRESULT hr = m_device->CreateTexture2D(&desc, NULL, &m_captureTexture);
				if (SUCCEEDED(hr) )
				{
					if (backBufferDesc.SampleDesc.Count != 1)
					{
						desc.Usage = D3D11_USAGE_DEFAULT;
						desc.CPUAccessFlags = 0;
						m_device->CreateTexture2D(&desc, NULL, &m_captureResolve);
					}

					g_callback->captureBegin(backBufferDesc.Width, backBufferDesc.Height, backBufferDesc.Width*4, TextureFormat::BGRA8, false);
				}

				DX_RELEASE(backBuffer, 0);
			}
		}

		void capturePreReset()
		{
			if (NULL != m_captureTexture)
			{
				g_callback->captureEnd();
			}

			DX_RELEASE(m_captureResolve, 0);
			DX_RELEASE(m_captureTexture, 0);
		}

		void capture()
		{
			if (NULL != m_captureTexture)
			{
				ID3D11Texture2D* backBuffer;
				DX_CHECK(m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer) );

				DXGI_MODE_DESC& desc = m_scd.BufferDesc;

				if (NULL == m_captureResolve)
				{
					m_deviceCtx->CopyResource(m_captureTexture, backBuffer);
				}
				else
				{
					m_deviceCtx->ResolveSubresource(m_captureResolve, 0, backBuffer, 0, desc.Format);
					m_deviceCtx->CopyResource(m_captureTexture, m_captureResolve);
				}

				D3D11_MAPPED_SUBRESOURCE mapped;
				DX_CHECK(m_deviceCtx->Map(m_captureTexture, 0, D3D11_MAP_READ, 0, &mapped) );

				g_callback->captureFrame(mapped.pData, desc.Height*mapped.RowPitch);

				m_deviceCtx->Unmap(m_captureTexture, 0);

				DX_RELEASE(backBuffer, 0);
			}
		}

		void commit(ConstantBuffer& _constantBuffer)
		{
			_constantBuffer.reset();

			for (;;)
			{
				uint32_t opcode = _constantBuffer.read();

				if (UniformType::End == opcode)
				{
					break;
				}

				UniformType::Enum type;
				uint16_t loc;
				uint16_t num;
				uint16_t copy;
				ConstantBuffer::decodeOpcode(opcode, type, loc, num, copy);

				const char* data;
				if (copy)
				{
					data = _constantBuffer.read(g_uniformTypeSize[type]*num);
				}
				else
				{
					UniformHandle handle;
					memcpy(&handle, _constantBuffer.read(sizeof(UniformHandle) ), sizeof(UniformHandle) );
					data = (const char*)m_uniforms[handle.idx];
				}

#define CASE_IMPLEMENT_UNIFORM(_uniform, _dxsuffix, _type) \
		case UniformType::_uniform: \
		case UniformType::_uniform|BGFX_UNIFORM_FRAGMENTBIT: \
				{ \
					setShaderConstant(type, loc, data, num); \
				} \
				break;

				switch ( (int32_t)type)
				{
				case UniformType::Uniform3x3fv:
				case UniformType::Uniform3x3fv|BGFX_UNIFORM_FRAGMENTBIT: \
					 {
						 float* value = (float*)data;
						 for (uint32_t ii = 0, count = num/3; ii < count; ++ii,  loc += 3*16, value += 9)
						 {
							 Matrix4 mtx;
							 mtx.un.val[ 0] = value[0];
							 mtx.un.val[ 1] = value[1];
							 mtx.un.val[ 2] = value[2];
							 mtx.un.val[ 3] = 0.0f;
							 mtx.un.val[ 4] = value[3];
							 mtx.un.val[ 5] = value[4];
							 mtx.un.val[ 6] = value[5];
							 mtx.un.val[ 7] = 0.0f;
							 mtx.un.val[ 8] = value[6];
							 mtx.un.val[ 9] = value[7];
							 mtx.un.val[10] = value[8];
							 mtx.un.val[11] = 0.0f;
							 setShaderConstant(type, loc, &mtx.un.val[0], 3);
						 }
					}
					break;

				CASE_IMPLEMENT_UNIFORM(Uniform1i,    I, int);
				CASE_IMPLEMENT_UNIFORM(Uniform1f,    F, float);
				CASE_IMPLEMENT_UNIFORM(Uniform1iv,   I, int);
				CASE_IMPLEMENT_UNIFORM(Uniform1fv,   F, float);
				CASE_IMPLEMENT_UNIFORM(Uniform2fv,   F, float);
				CASE_IMPLEMENT_UNIFORM(Uniform3fv,   F, float);
				CASE_IMPLEMENT_UNIFORM(Uniform4fv,   F, float);
				CASE_IMPLEMENT_UNIFORM(Uniform4x4fv, F, float);

				case UniformType::End:
					break;

				default:
					BX_TRACE("%4d: INVALID 0x%08x, t %d, l %d, n %d, c %d", _constantBuffer.getPos(), opcode, type, loc, num, copy);
					break;
				}

#undef CASE_IMPLEMENT_UNIFORM

			}
		}

		void clearQuad(ClearQuad& _clearQuad, const Rect& _rect, const Clear& _clear, const float _palette[][4])
		{
			uint32_t width  = m_scd.BufferDesc.Width;
			uint32_t height = m_scd.BufferDesc.Height;

			if (0      == _rect.m_x
			&&  0      == _rect.m_y
			&&  width  == _rect.m_width
			&&  height == _rect.m_height)
			{
				clear(_clear, _palette);
			}
			else
			{
				ID3D11DeviceContext* deviceCtx = m_deviceCtx;

				uint64_t state = 0;
				state |= _clear.m_flags & BGFX_CLEAR_COLOR_BIT ? BGFX_STATE_RGB_WRITE|BGFX_STATE_ALPHA_WRITE : 0;
				state |= _clear.m_flags & BGFX_CLEAR_DEPTH_BIT ? BGFX_STATE_DEPTH_TEST_ALWAYS|BGFX_STATE_DEPTH_WRITE : 0;

				uint64_t stencil = 0;
				stencil |= _clear.m_flags & BGFX_CLEAR_STENCIL_BIT ? 0
					| BGFX_STENCIL_TEST_ALWAYS
					| BGFX_STENCIL_FUNC_REF(_clear.m_stencil)
					| BGFX_STENCIL_FUNC_RMASK(0xff)
					| BGFX_STENCIL_OP_FAIL_S_REPLACE
					| BGFX_STENCIL_OP_FAIL_Z_REPLACE
					| BGFX_STENCIL_OP_PASS_Z_REPLACE
					: 0
					;

				setBlendState(state);
				setDepthStencilState(state, stencil);
				setRasterizerState(state);

				uint32_t numMrt = 1;
				FrameBufferHandle fbh = m_fbh;
				if (isValid(fbh) )
				{
					const FrameBufferD3D11& fb = m_frameBuffers[fbh.idx];
					numMrt = bx::uint32_max(1, fb.m_num);
				}

				ProgramD3D11& program = m_program[_clearQuad.m_program[numMrt-1].idx];
				m_currentProgram = &program;
				deviceCtx->VSSetShader(program.m_vsh->m_vertexShader, NULL, 0);
				deviceCtx->VSSetConstantBuffers(0, 0, NULL);
				if (NULL != m_currentColor)
				{
					const ShaderD3D11* fsh = program.m_fsh;
					deviceCtx->PSSetShader(fsh->m_pixelShader, NULL, 0);
					deviceCtx->PSSetConstantBuffers(0, 1, &fsh->m_buffer);

					if (BGFX_CLEAR_COLOR_USE_PALETTE_BIT & _clear.m_flags)
					{
						float mrtClear[BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS][4];
						for (uint32_t ii = 0; ii < numMrt; ++ii)
						{
							uint8_t index = (uint8_t)bx::uint32_min(BGFX_CONFIG_MAX_CLEAR_COLOR_PALETTE-1, _clear.m_index[ii]);
							memcpy(mrtClear[ii], _palette[index], 16);
						}

						deviceCtx->UpdateSubresource(fsh->m_buffer, 0, 0, mrtClear, 0, 0);
					}
					else
					{
						float rgba[4] =
						{
							_clear.m_index[0]*1.0f/255.0f,
							_clear.m_index[1]*1.0f/255.0f,
							_clear.m_index[2]*1.0f/255.0f,
							_clear.m_index[3]*1.0f/255.0f,
						};
						deviceCtx->UpdateSubresource(fsh->m_buffer, 0, 0, rgba, 0, 0);
					}
				}
				else
				{
					deviceCtx->PSSetShader(NULL, NULL, 0);
				}

				VertexBufferD3D11& vb = m_vertexBuffers[_clearQuad.m_vb->handle.idx];
				const VertexDecl& vertexDecl = m_vertexDecls[_clearQuad.m_vb->decl.idx];
				const uint32_t stride = vertexDecl.m_stride;
				const uint32_t offset = 0;

				{
					struct Vertex
					{
						float m_x;
						float m_y;
						float m_z;
					};
					
					Vertex* vertex = (Vertex*)_clearQuad.m_vb->data;
					BX_CHECK(stride == sizeof(Vertex), "Stride/Vertex mismatch (stride %d, sizeof(Vertex) %d)", stride, sizeof(Vertex) );

					const float depth = _clear.m_depth;

					vertex->m_x = -1.0f;
					vertex->m_y = -1.0f;
					vertex->m_z = depth;
					vertex++;
					vertex->m_x =  1.0f;
					vertex->m_y = -1.0f;
					vertex->m_z = depth;
					vertex++;
					vertex->m_x = -1.0f;
					vertex->m_y =  1.0f;
					vertex->m_z = depth;
					vertex++;
					vertex->m_x =  1.0f;
					vertex->m_y =  1.0f;
					vertex->m_z = depth;
				}

				m_vertexBuffers[_clearQuad.m_vb->handle.idx].update(0, 4*_clearQuad.m_decl.m_stride, _clearQuad.m_vb->data);
				deviceCtx->IASetVertexBuffers(0, 1, &vb.m_ptr, &stride, &offset);
				setInputLayout(vertexDecl, program, 0);

				deviceCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
				deviceCtx->Draw(4, 0);
			}
		}

#if USE_D3D11_DYNAMIC_LIB
		void* m_d3d9dll;
		void* m_d3d11dll;
		void* m_dxgidll;
#endif // USE_D3D11_DYNAMIC_LIB

		void* m_renderdocdll;

		D3D_DRIVER_TYPE m_driverType;
		IDXGIAdapter* m_adapter;
		DXGI_ADAPTER_DESC m_adapterDesc;
		IDXGIFactory* m_factory;

		IDXGISwapChain* m_swapChain;
		uint16_t m_lost;
		uint16_t m_numWindows;
		FrameBufferHandle m_windows[BGFX_CONFIG_MAX_FRAME_BUFFERS];

		ID3D11Device* m_device;
		ID3D11DeviceContext* m_deviceCtx;
		ID3D11RenderTargetView* m_backBufferColor;
		ID3D11DepthStencilView* m_backBufferDepthStencil;
		ID3D11RenderTargetView* m_currentColor;
		ID3D11DepthStencilView* m_currentDepthStencil;

		ID3D11Texture2D* m_captureTexture;
		ID3D11Texture2D* m_captureResolve;

		Resolution m_resolution;
		bool m_wireframe;

		DXGI_SWAP_CHAIN_DESC m_scd;
		uint32_t m_flags;

		IndexBufferD3D11 m_indexBuffers[BGFX_CONFIG_MAX_INDEX_BUFFERS];
		VertexBufferD3D11 m_vertexBuffers[BGFX_CONFIG_MAX_VERTEX_BUFFERS];
		ShaderD3D11 m_shaders[BGFX_CONFIG_MAX_SHADERS];
		ProgramD3D11 m_program[BGFX_CONFIG_MAX_PROGRAMS];
		TextureD3D11 m_textures[BGFX_CONFIG_MAX_TEXTURES];
		VertexDecl m_vertexDecls[BGFX_CONFIG_MAX_VERTEX_DECLS];
		FrameBufferD3D11 m_frameBuffers[BGFX_CONFIG_MAX_FRAME_BUFFERS];
		void* m_uniforms[BGFX_CONFIG_MAX_UNIFORMS];
		Matrix4 m_predefinedUniforms[PredefinedUniform::Count];
		UniformRegistry m_uniformReg;
		
		StateCacheT<ID3D11BlendState> m_blendStateCache;
		StateCacheT<ID3D11DepthStencilState> m_depthStencilStateCache;
		StateCacheT<ID3D11InputLayout> m_inputLayoutCache;
		StateCacheT<ID3D11RasterizerState> m_rasterizerStateCache;
		StateCacheT<ID3D11SamplerState> m_samplerStateCache;

		TextVideoMem m_textVideoMem;

		TextureStage m_textureStage;

		ProgramD3D11* m_currentProgram;

		uint8_t m_vsScratch[64<<10];
		uint8_t m_fsScratch[64<<10];

		uint32_t m_vsChanges;
		uint32_t m_fsChanges;

		FrameBufferHandle m_fbh;
		bool m_rtMsaa;
	};

	static RendererContextD3D11* s_renderD3D11;

	RendererContextI* rendererCreateD3D11()
	{
		s_renderD3D11 = BX_NEW(g_allocator, RendererContextD3D11);
		return s_renderD3D11;
	}

	void rendererDestroyD3D11()
	{
		BX_DELETE(g_allocator, s_renderD3D11);
		s_renderD3D11 = NULL;
	}

	void IndexBufferD3D11::create(uint32_t _size, void* _data)
	{
		m_size = _size;
		m_dynamic = NULL == _data;

		D3D11_BUFFER_DESC desc;
		desc.ByteWidth = _size;
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		desc.MiscFlags = 0;
		desc.StructureByteStride = 0;

		if (m_dynamic)
		{
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			DX_CHECK(s_renderD3D11->m_device->CreateBuffer(&desc
				, NULL
				, &m_ptr
				) );
		}
		else
		{
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.CPUAccessFlags = 0;

			D3D11_SUBRESOURCE_DATA srd;
			srd.pSysMem = _data;
			srd.SysMemPitch = 0;
			srd.SysMemSlicePitch = 0;

			DX_CHECK(s_renderD3D11->m_device->CreateBuffer(&desc
				, &srd
				, &m_ptr
				) );
		}
	}

	void IndexBufferD3D11::update(uint32_t _offset, uint32_t _size, void* _data)
	{
		ID3D11DeviceContext* deviceCtx = s_renderD3D11->m_deviceCtx;
		BX_CHECK(m_dynamic, "Must be dynamic!");

		D3D11_MAPPED_SUBRESOURCE mapped;
		D3D11_MAP type = m_dynamic && 0 == _offset && m_size == _size ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
		DX_CHECK(deviceCtx->Map(m_ptr, 0, type, 0, &mapped) );
		memcpy( (uint8_t*)mapped.pData + _offset, _data, _size);
		deviceCtx->Unmap(m_ptr, 0);
	}

	void VertexBufferD3D11::create(uint32_t _size, void* _data, VertexDeclHandle _declHandle)
	{
		m_size = _size;
		m_decl = _declHandle;
		m_dynamic = NULL == _data;

		D3D11_BUFFER_DESC desc;
		desc.ByteWidth = _size;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.MiscFlags = 0;

		if (m_dynamic)
		{
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.StructureByteStride = 0;

			DX_CHECK(s_renderD3D11->m_device->CreateBuffer(&desc
				, NULL
				, &m_ptr
				) );
		}
		else
		{
			desc.Usage = D3D11_USAGE_IMMUTABLE;
			desc.CPUAccessFlags = 0;
			desc.StructureByteStride = 0;

			D3D11_SUBRESOURCE_DATA srd;
			srd.pSysMem = _data;
			srd.SysMemPitch = 0;
			srd.SysMemSlicePitch = 0;

			DX_CHECK(s_renderD3D11->m_device->CreateBuffer(&desc
				, &srd
				, &m_ptr
				) );
		}
	}

	void VertexBufferD3D11::update(uint32_t _offset, uint32_t _size, void* _data, bool _discard)
	{
		ID3D11DeviceContext* deviceCtx = s_renderD3D11->m_deviceCtx;
		BX_CHECK(m_dynamic, "Must be dynamic!");

		D3D11_MAPPED_SUBRESOURCE mapped;
		D3D11_MAP type = m_dynamic && ( (0 == _offset && m_size == _size) || _discard) ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
		DX_CHECK(deviceCtx->Map(m_ptr, 0, type, 0, &mapped) );
		memcpy( (uint8_t*)mapped.pData + _offset, _data, _size);
		deviceCtx->Unmap(m_ptr, 0);
	}

	void ShaderD3D11::create(const Memory* _mem)
	{
		bx::MemoryReader reader(_mem->data, _mem->size);

		uint32_t magic;
		bx::read(&reader, magic);

		switch (magic)
		{
		case BGFX_CHUNK_MAGIC_CSH:
		case BGFX_CHUNK_MAGIC_FSH:
		case BGFX_CHUNK_MAGIC_VSH:
			break;

		default:
			BGFX_FATAL(false, Fatal::InvalidShader, "Unknown shader format %x.", magic);
			break;
		}

		bool fragment = BGFX_CHUNK_MAGIC_FSH == magic;

		uint32_t iohash;
		bx::read(&reader, iohash);

		uint16_t count;
		bx::read(&reader, count);

		m_numPredefined = 0;
		m_numUniforms = count;

		BX_TRACE("%s Shader consts %d"
			, BGFX_CHUNK_MAGIC_FSH == magic ? "Fragment" : BGFX_CHUNK_MAGIC_VSH == magic ? "Vertex" : "Compute"
			, count
			);

		uint8_t fragmentBit = fragment ? BGFX_UNIFORM_FRAGMENTBIT : 0;

		if (0 < count)
		{
			for (uint32_t ii = 0; ii < count; ++ii)
			{
				uint8_t nameSize;
				bx::read(&reader, nameSize);

				char name[256];
				bx::read(&reader, &name, nameSize);
				name[nameSize] = '\0';

				uint8_t type;
				bx::read(&reader, type);

				uint8_t num;
				bx::read(&reader, num);

				uint16_t regIndex;
				bx::read(&reader, regIndex);

				uint16_t regCount;
				bx::read(&reader, regCount);

				const char* kind = "invalid";

				PredefinedUniform::Enum predefined = nameToPredefinedUniformEnum(name);
				if (PredefinedUniform::Count != predefined)
				{
					kind = "predefined";
					m_predefined[m_numPredefined].m_loc   = regIndex;
					m_predefined[m_numPredefined].m_count = regCount;
					m_predefined[m_numPredefined].m_type  = predefined|fragmentBit;
					m_numPredefined++;
				}
				else
				{
					const UniformInfo* info = s_renderD3D11->m_uniformReg.find(name);

					if (NULL != info)
					{
						if (NULL == m_constantBuffer)
						{
							m_constantBuffer = ConstantBuffer::create(1024);
						}

						kind = "user";
						m_constantBuffer->writeUniformHandle( (UniformType::Enum)(type|fragmentBit), regIndex, info->m_handle, regCount);
					}
				}

				BX_TRACE("\t%s: %s (%s), num %2d, r.index %3d, r.count %2d"
					, kind
					, name
					, getUniformTypeName(UniformType::Enum(type&~BGFX_UNIFORM_FRAGMENTBIT) )
					, num
					, regIndex
					, regCount
					);
				BX_UNUSED(kind);
			}

			if (NULL != m_constantBuffer)
			{
				m_constantBuffer->finish();
			}
		}

		uint16_t shaderSize;
		bx::read(&reader, shaderSize);

		const DWORD* code = (const DWORD*)reader.getDataPtr();
		bx::skip(&reader, shaderSize+1);

		if (BGFX_CHUNK_MAGIC_FSH == magic)
		{
			DX_CHECK(s_renderD3D11->m_device->CreatePixelShader(code, shaderSize, NULL, &m_pixelShader) );
			BGFX_FATAL(NULL != m_ptr, bgfx::Fatal::InvalidShader, "Failed to create fragment shader.");
		}
		else if (BGFX_CHUNK_MAGIC_VSH == magic)
		{
			m_hash = bx::hashMurmur2A(code, shaderSize);
			m_code = copy(code, shaderSize);

			DX_CHECK(s_renderD3D11->m_device->CreateVertexShader(code, shaderSize, NULL, &m_vertexShader) );
			BGFX_FATAL(NULL != m_ptr, bgfx::Fatal::InvalidShader, "Failed to create vertex shader.");
		}
		else
		{
			DX_CHECK(s_renderD3D11->m_device->CreateComputeShader(code, shaderSize, NULL, &m_computeShader) );
			BGFX_FATAL(NULL != m_ptr, bgfx::Fatal::InvalidShader, "Failed to create compute shader.");
		}

		uint8_t numAttrs;
		bx::read(&reader, numAttrs);

		memset(m_attrMask, 0, sizeof(m_attrMask));

		for (uint32_t ii = 0; ii < numAttrs; ++ii)
		{
			uint16_t id;
			bx::read(&reader, id);

			Attrib::Enum attr = idToAttrib(id);

			if (Attrib::Count != attr)
			{
				m_attrMask[attr] = 0xff;
			}
		}

		uint16_t size;
		bx::read(&reader, size);

		if (0 < size)
		{
			D3D11_BUFFER_DESC desc;
			desc.ByteWidth = size;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = 0;
			desc.MiscFlags = 0;
			desc.StructureByteStride = 0;
			DX_CHECK(s_renderD3D11->m_device->CreateBuffer(&desc, NULL, &m_buffer) );
		}
	}

	void TextureD3D11::create(const Memory* _mem, uint32_t _flags, uint8_t _skip)
	{
		m_sampler = s_renderD3D11->getSamplerState(_flags);

		ImageContainer imageContainer;

		if (imageParse(imageContainer, _mem->data, _mem->size) )
		{
			uint8_t numMips = imageContainer.m_numMips;
			const uint32_t startLod = bx::uint32_min(_skip, numMips-1);
			numMips -= startLod;
			const ImageBlockInfo& blockInfo = getBlockInfo(TextureFormat::Enum(imageContainer.m_format) );
			const uint32_t textureWidth  = bx::uint32_max(blockInfo.blockWidth,  imageContainer.m_width >>startLod);
			const uint32_t textureHeight = bx::uint32_max(blockInfo.blockHeight, imageContainer.m_height>>startLod);

			m_flags = _flags;
			m_requestedFormat = (uint8_t)imageContainer.m_format;
			m_textureFormat   = (uint8_t)imageContainer.m_format;

			const TextureFormatInfo& tfi = s_textureFormat[m_requestedFormat];
			const bool convert = DXGI_FORMAT_UNKNOWN == tfi.m_fmt;

			uint8_t bpp = getBitsPerPixel(TextureFormat::Enum(m_textureFormat) );
			if (convert)
			{
				m_textureFormat = (uint8_t)TextureFormat::BGRA8;
				bpp = 32;
			}

			if (imageContainer.m_cubeMap)
			{
				m_type = TextureCube;
			}
			else if (imageContainer.m_depth > 1)
			{
				m_type = Texture3D;
			}
			else
			{
				m_type = Texture2D;
			}

			m_numMips = numMips;

			uint32_t numSrd = numMips*(imageContainer.m_cubeMap ? 6 : 1);
			D3D11_SUBRESOURCE_DATA* srd = (D3D11_SUBRESOURCE_DATA*)alloca(numSrd*sizeof(D3D11_SUBRESOURCE_DATA) );

			uint32_t kk = 0;

			const bool compressed = isCompressed(TextureFormat::Enum(m_textureFormat) );
			const bool swizzle    = TextureFormat::BGRA8 == m_textureFormat && 0 != (m_flags&BGFX_TEXTURE_COMPUTE_WRITE);

			BX_TRACE("Texture %3d: %s (requested: %s), %dx%d%s%s%s."
				, this - s_renderD3D11->m_textures
				, getName( (TextureFormat::Enum)m_textureFormat)
				, getName( (TextureFormat::Enum)m_requestedFormat)
				, textureWidth
				, textureHeight
				, imageContainer.m_cubeMap ? "x6" : ""
				, 0 != (m_flags&BGFX_TEXTURE_RT_MASK) ? " (render target)" : ""
				, swizzle ? " (swizzle BGRA8 -> RGBA8)" : ""
				);

			for (uint8_t side = 0, numSides = imageContainer.m_cubeMap ? 6 : 1; side < numSides; ++side)
			{
				uint32_t width  = textureWidth;
				uint32_t height = textureHeight;
				uint32_t depth  = imageContainer.m_depth;

				for (uint32_t lod = 0, num = numMips; lod < num; ++lod)
				{
					width  = bx::uint32_max(1, width);
					height = bx::uint32_max(1, height);
					depth  = bx::uint32_max(1, depth);

					ImageMip mip;
					if (imageGetRawData(imageContainer, side, lod+startLod, _mem->data, _mem->size, mip) )
					{
						srd[kk].pSysMem = mip.m_data;

						if (convert)
						{
							uint32_t srcpitch = mip.m_width*bpp/8;
							uint8_t* temp = (uint8_t*)BX_ALLOC(g_allocator, mip.m_width*mip.m_height*bpp/8);
							imageDecodeToBgra8(temp, mip.m_data, mip.m_width, mip.m_height, srcpitch, mip.m_format);

							srd[kk].pSysMem = temp;
							srd[kk].SysMemPitch = srcpitch;
						}
						else if (compressed)
						{
							srd[kk].SysMemPitch      = (mip.m_width /blockInfo.blockWidth )*mip.m_blockSize;
							srd[kk].SysMemSlicePitch = (mip.m_height/blockInfo.blockHeight)*srd[kk].SysMemPitch;
						}
						else
						{
							srd[kk].SysMemPitch = mip.m_width*mip.m_bpp/8;
						}

 						if (swizzle)
 						{
// 							imageSwizzleBgra8(width, height, mip.m_width*4, data, temp);
 						}

						srd[kk].SysMemSlicePitch = mip.m_height*srd[kk].SysMemPitch;
						++kk;
					}

					width  >>= 1;
					height >>= 1;
					depth  >>= 1;
				}
			}

			const bool bufferOnly   = 0 != (m_flags&BGFX_TEXTURE_RT_BUFFER_ONLY);
			const bool computeWrite = 0 != (m_flags&BGFX_TEXTURE_COMPUTE_WRITE);
			const bool renderTarget = 0 != (m_flags&BGFX_TEXTURE_RT_MASK);
			const uint32_t msaaQuality = bx::uint32_satsub( (m_flags&BGFX_TEXTURE_RT_MSAA_MASK)>>BGFX_TEXTURE_RT_MSAA_SHIFT, 1);
			const DXGI_SAMPLE_DESC& msaa = s_msaa[msaaQuality];

			D3D11_SHADER_RESOURCE_VIEW_DESC srvd;
			memset(&srvd, 0, sizeof(srvd) );
			srvd.Format = s_textureFormat[m_textureFormat].m_fmtSrv;
			DXGI_FORMAT format = s_textureFormat[m_textureFormat].m_fmt;

			if (swizzle)
			{
				format      = DXGI_FORMAT_R8G8B8A8_UNORM;
				srvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			}

			switch (m_type)
			{
			case Texture2D:
			case TextureCube:
				{
					D3D11_TEXTURE2D_DESC desc;
					desc.Width = textureWidth;
					desc.Height = textureHeight;
					desc.MipLevels = numMips;
					desc.Format = format;
					desc.SampleDesc = msaa;
					desc.Usage = kk == 0 ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
					desc.BindFlags = bufferOnly ? 0 : D3D11_BIND_SHADER_RESOURCE;
					desc.CPUAccessFlags = 0;

					if (isDepth( (TextureFormat::Enum)m_textureFormat) )
					{
						desc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
						desc.Usage = D3D11_USAGE_DEFAULT;
					}
					else if (renderTarget)
					{
						desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
						desc.Usage = D3D11_USAGE_DEFAULT;
					}

					if (computeWrite)
					{
						desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
						desc.Usage = D3D11_USAGE_DEFAULT;
					}

					if (imageContainer.m_cubeMap)
					{
						desc.ArraySize = 6;
						desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
						srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
						srvd.TextureCube.MipLevels = numMips;
					}
					else
					{
						desc.ArraySize = 1;
						desc.MiscFlags = 0;
						srvd.ViewDimension = 1 < msaa.Count ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
						srvd.Texture2D.MipLevels = numMips;
					}

					DX_CHECK(s_renderD3D11->m_device->CreateTexture2D(&desc, kk == 0 ? NULL : srd, &m_texture2d) );
				}
				break;

			case Texture3D:
				{
					D3D11_TEXTURE3D_DESC desc;
					desc.Width = textureWidth;
					desc.Height = textureHeight;
					desc.Depth = imageContainer.m_depth;
					desc.MipLevels = imageContainer.m_numMips;
					desc.Format = format;
					desc.Usage = kk == 0 ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
					desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
					desc.CPUAccessFlags = 0;
					desc.MiscFlags = 0;

					if (computeWrite)
					{
						desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
						desc.Usage = D3D11_USAGE_DEFAULT;
					}

					srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
					srvd.Texture3D.MipLevels = numMips;

					DX_CHECK(s_renderD3D11->m_device->CreateTexture3D(&desc, kk == 0 ? NULL : srd, &m_texture3d) );
				}
				break;
			}

			if (!bufferOnly)
			{
				DX_CHECK(s_renderD3D11->m_device->CreateShaderResourceView(m_ptr, &srvd, &m_srv) );
			}

			if (computeWrite)
			{
				DX_CHECK(s_renderD3D11->m_device->CreateUnorderedAccessView(m_ptr, NULL, &m_uav) );
			}

			if (convert
			&&  0 != kk)
			{
				kk = 0;
				for (uint8_t side = 0, numSides = imageContainer.m_cubeMap ? 6 : 1; side < numSides; ++side)
				{
					for (uint32_t lod = 0, num = numMips; lod < num; ++lod)
					{
						BX_FREE(g_allocator, const_cast<void*>(srd[kk].pSysMem) );
						++kk;
					}
				}
			}
		}
	}

	void TextureD3D11::destroy()
	{
		DX_RELEASE(m_srv, 0);
		DX_RELEASE(m_uav, 0);
		DX_RELEASE(m_ptr, 0);
	}

	void TextureD3D11::update(uint8_t _side, uint8_t _mip, const Rect& _rect, uint16_t _z, uint16_t _depth, uint16_t _pitch, const Memory* _mem)
	{
		ID3D11DeviceContext* deviceCtx = s_renderD3D11->m_deviceCtx;

		D3D11_BOX box;
		box.left = _rect.m_x;
		box.top = _rect.m_y;
		box.right = box.left + _rect.m_width;
		box.bottom = box.top + _rect.m_height;
		box.front = _z;
		box.back = box.front + _depth;

		const uint32_t subres = _mip + (_side * m_numMips);
		const uint32_t bpp = getBitsPerPixel(TextureFormat::Enum(m_textureFormat) );
		const uint32_t rectpitch = _rect.m_width*bpp/8;
		const uint32_t srcpitch  = UINT16_MAX == _pitch ? rectpitch : _pitch;

		const bool convert = m_textureFormat != m_requestedFormat;

		uint8_t* data = _mem->data;
		uint8_t* temp = NULL;

		if (convert)
		{
			uint8_t* temp = (uint8_t*)BX_ALLOC(g_allocator, rectpitch*_rect.m_height);
			imageDecodeToBgra8(temp, data, _rect.m_width, _rect.m_height, srcpitch, m_requestedFormat);
			data = temp;
		}

		deviceCtx->UpdateSubresource(m_ptr, subres, &box, data, srcpitch, 0);

		if (NULL != temp)
		{
			BX_FREE(g_allocator, temp);
		}
	}

	void TextureD3D11::commit(uint8_t _stage, uint32_t _flags)
	{
		TextureStage& ts = s_renderD3D11->m_textureStage;
		ts.m_srv[_stage] = m_srv;
		ts.m_sampler[_stage] = 0 == (BGFX_SAMPLER_DEFAULT_FLAGS & _flags) 
			? s_renderD3D11->getSamplerState(_flags)
			: m_sampler
			;
	}

	void TextureD3D11::resolve()
	{
	}

	void FrameBufferD3D11::create(uint8_t _num, const TextureHandle* _handles)
	{
		for (uint32_t ii = 0; ii < BX_COUNTOF(m_rtv); ++ii)
		{
			m_rtv[ii] = NULL;
		}
		m_dsv       = NULL;
		m_swapChain = NULL;

		m_num = 0;
		for (uint32_t ii = 0; ii < _num; ++ii)
		{
			TextureHandle handle = _handles[ii];
			if (isValid(handle) )
			{
				const TextureD3D11& texture = s_renderD3D11->m_textures[handle.idx];
				if (isDepth( (TextureFormat::Enum)texture.m_textureFormat) )
				{
					BX_CHECK(NULL == m_dsv, "Frame buffer already has depth-stencil attached.");

					const uint32_t msaaQuality = bx::uint32_satsub( (texture.m_flags&BGFX_TEXTURE_RT_MSAA_MASK)>>BGFX_TEXTURE_RT_MSAA_SHIFT, 1);
					const DXGI_SAMPLE_DESC& msaa = s_msaa[msaaQuality];

					D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
					dsvDesc.Format = s_textureFormat[texture.m_textureFormat].m_fmtDsv;
					dsvDesc.ViewDimension = 1 < msaa.Count ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
					dsvDesc.Flags = 0;
					dsvDesc.Texture2D.MipSlice = 0;
					DX_CHECK(s_renderD3D11->m_device->CreateDepthStencilView(texture.m_ptr, &dsvDesc, &m_dsv) );
				}
				else
				{
					DX_CHECK(s_renderD3D11->m_device->CreateRenderTargetView(texture.m_ptr, NULL, &m_rtv[m_num]) );
					DX_CHECK(s_renderD3D11->m_device->CreateShaderResourceView(texture.m_ptr, NULL, &m_srv[m_num]) );
					m_num++;
				}
			}
		}
	}

	void FrameBufferD3D11::create(uint16_t _denseIdx, void* _nwh, uint32_t _width, uint32_t _height, TextureFormat::Enum _depthFormat)
	{
		BX_UNUSED(_depthFormat);

		DXGI_SWAP_CHAIN_DESC scd;
		memcpy(&scd, &s_renderD3D11->m_scd, sizeof(DXGI_SWAP_CHAIN_DESC) );
		scd.BufferDesc.Width  = _width;
		scd.BufferDesc.Height = _height;
		scd.OutputWindow = (HWND)_nwh;

		HRESULT hr;
		hr = s_renderD3D11->m_factory->CreateSwapChain(s_renderD3D11->m_device
			, &scd
			, &m_swapChain
			);
		BGFX_FATAL(SUCCEEDED(hr), Fatal::UnableToInitialize, "Failed to create swap chain.");

		ID3D11Resource* ptr;
		DX_CHECK(m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&ptr) );
		DX_CHECK(s_renderD3D11->m_device->CreateRenderTargetView(ptr, NULL, &m_rtv[0]) );
		DX_RELEASE(ptr, 0);
		m_srv[0]   = NULL;
		m_dsv      = NULL;
		m_denseIdx = _denseIdx;
		m_num      = 1;
	}

	uint16_t FrameBufferD3D11::destroy()
	{
		for (uint32_t ii = 0, num = m_num; ii < num; ++ii)
		{
			DX_RELEASE(m_srv[ii], 0);
			DX_RELEASE(m_rtv[ii], 0);
		}

		DX_RELEASE(m_dsv, 0);
		DX_RELEASE(m_swapChain, 0);

		m_num = 0;

		uint16_t denseIdx = m_denseIdx;
		m_denseIdx = UINT16_MAX;
		
		return denseIdx;
	}

	void FrameBufferD3D11::resolve()
	{
	}

	void FrameBufferD3D11::clear(const Clear& _clear, const float _palette[][4])
	{
		ID3D11DeviceContext* deviceCtx = s_renderD3D11->m_deviceCtx;

		if (BGFX_CLEAR_COLOR_BIT & _clear.m_flags)
		{
			if (BGFX_CLEAR_COLOR_USE_PALETTE_BIT & _clear.m_flags)
			{
				for (uint32_t ii = 0, num = m_num; ii < num; ++ii)
				{
					uint8_t index = _clear.m_index[ii];
					if (NULL != m_rtv[ii]
					&&  UINT8_MAX != index)
					{
						deviceCtx->ClearRenderTargetView(m_rtv[ii], _palette[index]);
					}
				}
			}
			else
			{
				float frgba[4] =
				{ 
					_clear.m_index[0]*1.0f/255.0f,
					_clear.m_index[1]*1.0f/255.0f,
					_clear.m_index[2]*1.0f/255.0f,
					_clear.m_index[3]*1.0f/255.0f,
				};
				for (uint32_t ii = 0, num = m_num; ii < num; ++ii)
				{
					if (NULL != m_rtv[ii])
					{
						deviceCtx->ClearRenderTargetView(m_rtv[ii], frgba);
					}
				}
			}
		}

		if (NULL != m_dsv
		&& (BGFX_CLEAR_DEPTH_BIT|BGFX_CLEAR_STENCIL_BIT) & _clear.m_flags)
		{
			DWORD flags = 0;
			flags |= (_clear.m_flags & BGFX_CLEAR_DEPTH_BIT) ? D3D11_CLEAR_DEPTH : 0;
			flags |= (_clear.m_flags & BGFX_CLEAR_STENCIL_BIT) ? D3D11_CLEAR_STENCIL : 0;
			deviceCtx->ClearDepthStencilView(m_dsv, flags, _clear.m_depth, _clear.m_stencil);
		}
	}

	void RendererContextD3D11::submit(Frame* _render, ClearQuad& _clearQuad, TextVideoMemBlitter& _textVideoMemBlitter)
	{
		PIX_BEGINEVENT(D3DCOLOR_RGBA(0xff, 0x00, 0x00, 0xff), L"rendererSubmit");

		ID3D11DeviceContext* deviceCtx = m_deviceCtx;

		updateResolution(_render->m_resolution);

		int64_t elapsed = -bx::getHPCounter();
		int64_t captureElapsed = 0;

		if (0 < _render->m_iboffset)
		{
			TransientIndexBuffer* ib = _render->m_transientIb;
			m_indexBuffers[ib->handle.idx].update(0, _render->m_iboffset, ib->data);
		}

		if (0 < _render->m_vboffset)
		{
			TransientVertexBuffer* vb = _render->m_transientVb;
			m_vertexBuffers[vb->handle.idx].update(0, _render->m_vboffset, vb->data);
		}

		_render->sort();

		RenderDraw currentState;
		currentState.clear();
		currentState.m_flags = BGFX_STATE_NONE;
		currentState.m_stencil = packStencil(BGFX_STENCIL_NONE, BGFX_STENCIL_NONE);

		Matrix4 viewProj[BGFX_CONFIG_MAX_VIEWS];
		for (uint32_t ii = 0; ii < BGFX_CONFIG_MAX_VIEWS; ++ii)
		{
			bx::float4x4_mul(&viewProj[ii].un.f4x4, &_render->m_view[ii].un.f4x4, &_render->m_proj[ii].un.f4x4);
		}

		Matrix4 invView;
		Matrix4 invProj;
		Matrix4 invViewProj;
		uint8_t invViewCached = 0xff;
		uint8_t invProjCached = 0xff;
		uint8_t invViewProjCached = 0xff;

		bool wireframe = !!(_render->m_debug&BGFX_DEBUG_WIREFRAME);
		bool scissorEnabled = false;
		setDebugWireframe(wireframe);

		uint16_t programIdx = invalidHandle;
		SortKey key;
		uint8_t view = 0xff;
		FrameBufferHandle fbh = BGFX_INVALID_HANDLE;
		float alphaRef = 0.0f;

		const uint64_t pt = _render->m_debug&BGFX_DEBUG_WIREFRAME ? BGFX_STATE_PT_LINES : 0;
		uint8_t primIndex = uint8_t(pt>>BGFX_STATE_PT_SHIFT);
		PrimInfo prim = s_primInfo[primIndex];
		deviceCtx->IASetPrimitiveTopology(prim.m_type);

		bool wasCompute = false;
		bool viewHasScissor = false;
		Rect viewScissorRect;
		viewScissorRect.clear();

		uint32_t statsNumPrimsSubmitted[BX_COUNTOF(s_primInfo)] = {};
		uint32_t statsNumPrimsRendered[BX_COUNTOF(s_primInfo)] = {};
		uint32_t statsNumInstances[BX_COUNTOF(s_primInfo)] = {};
		uint32_t statsNumIndices = 0;

		if (0 == (_render->m_debug&BGFX_DEBUG_IFH) )
		{
			for (uint32_t item = 0, numItems = _render->m_num; item < numItems; ++item)
			{
				const bool isCompute = key.decode(_render->m_sortKeys[item]);
				const bool viewChanged = key.m_view != view;

				const RenderItem& renderItem = _render->m_renderItem[_render->m_sortValues[item] ];

				if (viewChanged)
				{
					PIX_ENDEVENT();
					PIX_BEGINEVENT(D3DCOLOR_RGBA(0xff, 0x00, 0x00, 0xff), s_viewNameW[key.m_view]);

					view = key.m_view;
					programIdx = invalidHandle;

					if (_render->m_fb[view].idx != fbh.idx)
					{
						fbh = _render->m_fb[view];
						setFrameBuffer(fbh);
					}

					const Rect& rect = _render->m_rect[view];
					const Rect& scissorRect = _render->m_scissor[view];
					viewHasScissor = !scissorRect.isZero();
					viewScissorRect = viewHasScissor ? scissorRect : rect;

					D3D11_VIEWPORT vp;
					vp.TopLeftX = rect.m_x;
					vp.TopLeftY = rect.m_y;
					vp.Width    = rect.m_width;
					vp.Height   = rect.m_height;
					vp.MinDepth = 0.0f;
					vp.MaxDepth = 1.0f;
					deviceCtx->RSSetViewports(1, &vp);
					Clear& clear = _render->m_clear[view];

					if (BGFX_CLEAR_NONE != clear.m_flags)
					{
						clearQuad(_clearQuad, rect, clear, _render->m_clearColor);
						prim = s_primInfo[BX_COUNTOF(s_primName)]; // Force primitive type update after clear quad.
					}
				}

				if (isCompute)
				{
					if (!wasCompute)
					{
						wasCompute = true;

						ID3D11ShaderResourceView* srv[BGFX_CONFIG_MAX_TEXTURE_SAMPLERS] = {};
						deviceCtx->VSSetShaderResources(0, BGFX_CONFIG_MAX_TEXTURE_SAMPLERS, srv);
						deviceCtx->PSSetShaderResources(0, BGFX_CONFIG_MAX_TEXTURE_SAMPLERS, srv);

						ID3D11SamplerState* sampler[BGFX_CONFIG_MAX_TEXTURE_SAMPLERS] = {};
						deviceCtx->VSSetSamplers(0, BGFX_CONFIG_MAX_TEXTURE_SAMPLERS, sampler);
						deviceCtx->PSSetSamplers(0, BGFX_CONFIG_MAX_TEXTURE_SAMPLERS, sampler);
					}

					const RenderCompute& compute = renderItem.compute;

					bool programChanged = false;
					bool constantsChanged = compute.m_constBegin < compute.m_constEnd;
					rendererUpdateUniforms(this, _render->m_constantBuffer, compute.m_constBegin, compute.m_constEnd);

					if (key.m_program != programIdx)
					{
						programIdx = key.m_program;

						ProgramD3D11& program = m_program[key.m_program];
						m_currentProgram = &program;

						deviceCtx->CSSetShader(program.m_vsh->m_computeShader, NULL, 0);
						deviceCtx->CSSetConstantBuffers(0, 1, &program.m_vsh->m_buffer);

						programChanged = 
							constantsChanged = true;
					}

					if (invalidHandle != programIdx)
					{
						ProgramD3D11& program = m_program[programIdx];

						if (constantsChanged)
						{
							ConstantBuffer* vcb = program.m_vsh->m_constantBuffer;
							if (NULL != vcb)
							{
								commit(*vcb);
							}
						}

						if (constantsChanged
						||  program.m_numPredefined > 0)
						{
							commitShaderConstants();
						}
					}
					BX_UNUSED(programChanged);
					ID3D11UnorderedAccessView* uav[BGFX_MAX_COMPUTE_BINDINGS] = {};
					ID3D11ShaderResourceView*  srv[BGFX_MAX_COMPUTE_BINDINGS] = {};
					ID3D11SamplerState*    sampler[BGFX_CONFIG_MAX_TEXTURE_SAMPLERS] = {};

					for (uint32_t ii = 0; ii < BGFX_MAX_COMPUTE_BINDINGS; ++ii)
					{
						const ComputeBinding& bind = compute.m_bind[ii];
						if (invalidHandle != bind.m_idx)
						{
							switch (bind.m_type)
							{
							case ComputeBinding::Image:
								{
									const TextureD3D11& texture = m_textures[bind.m_idx];
									if (Access::Read != bind.m_access)
									{
										uav[ii] = texture.m_uav;
									}
									else
									{
										srv[ii] = texture.m_srv;
										sampler[ii] = texture.m_sampler;
									}
								}
								break;

							case ComputeBinding::Buffer:
								{
									const VertexBufferD3D11& vertexBuffer = m_vertexBuffers[bind.m_idx];
									BX_UNUSED(vertexBuffer);
								}
								break;
							}
						}
					}

					deviceCtx->CSSetUnorderedAccessViews(0, BGFX_MAX_COMPUTE_BINDINGS, uav, NULL);
					deviceCtx->CSSetShaderResources(0, BGFX_MAX_COMPUTE_BINDINGS, srv);
					deviceCtx->CSSetSamplers(0, BGFX_MAX_COMPUTE_BINDINGS, sampler);

					deviceCtx->Dispatch(compute.m_numX, compute.m_numY, compute.m_numZ);

					continue;
				}

				if (wasCompute)
				{
					wasCompute = false;

					programIdx = invalidHandle;
					m_currentProgram = NULL;

					deviceCtx->CSSetShader(NULL, NULL, 0);

					ID3D11UnorderedAccessView* uav[BGFX_CONFIG_MAX_TEXTURE_SAMPLERS] = {};
					deviceCtx->CSSetUnorderedAccessViews(0, BGFX_CONFIG_MAX_TEXTURE_SAMPLERS, uav, NULL);

					ID3D11ShaderResourceView* srv[BGFX_CONFIG_MAX_TEXTURE_SAMPLERS] = {};
					deviceCtx->CSSetShaderResources(0, BGFX_CONFIG_MAX_TEXTURE_SAMPLERS, srv);

					ID3D11SamplerState* samplers[BGFX_CONFIG_MAX_TEXTURE_SAMPLERS] = {};
					m_deviceCtx->CSSetSamplers(0, BGFX_CONFIG_MAX_TEXTURE_SAMPLERS, samplers);
				}

				const RenderDraw& draw = renderItem.draw;

				const uint64_t newFlags = draw.m_flags;
				uint64_t changedFlags = currentState.m_flags ^ draw.m_flags;
				currentState.m_flags = newFlags;

				const uint64_t newStencil = draw.m_stencil;
				uint64_t changedStencil = currentState.m_stencil ^ draw.m_stencil;
				currentState.m_stencil = newStencil;

				if (viewChanged)
				{
					currentState.clear();
					currentState.m_scissor = !draw.m_scissor;
					changedFlags = BGFX_STATE_MASK;
					changedStencil = packStencil(BGFX_STENCIL_MASK, BGFX_STENCIL_MASK);
					currentState.m_flags = newFlags;
					currentState.m_stencil = newStencil;

					uint64_t newFlags = renderItem.draw.m_flags;
					setBlendState(newFlags);
					setDepthStencilState(newFlags, packStencil(BGFX_STENCIL_DEFAULT, BGFX_STENCIL_DEFAULT) );

					const uint64_t pt = newFlags&BGFX_STATE_PT_MASK;
					primIndex = uint8_t(pt>>BGFX_STATE_PT_SHIFT);
					if (prim.m_type != s_primInfo[primIndex].m_type)
					{
						prim = s_primInfo[primIndex];
						deviceCtx->IASetPrimitiveTopology(prim.m_type);
					}
				}

				uint16_t scissor = draw.m_scissor;
				if (currentState.m_scissor != scissor)
				{
					currentState.m_scissor = scissor;

					if (UINT16_MAX == scissor)
					{
						scissorEnabled = viewHasScissor;
						if (viewHasScissor)
						{
							D3D11_RECT rc;
							rc.left   = viewScissorRect.m_x;
							rc.top    = viewScissorRect.m_y;
							rc.right  = viewScissorRect.m_x + viewScissorRect.m_width;
							rc.bottom = viewScissorRect.m_y + viewScissorRect.m_height;
							deviceCtx->RSSetScissorRects(1, &rc);
						}
					}
					else
					{
						Rect scissorRect;
						scissorRect.intersect(viewScissorRect, _render->m_rectCache.m_cache[scissor]);
						scissorEnabled = true;
						D3D11_RECT rc;
						rc.left   = scissorRect.m_x;
						rc.top    = scissorRect.m_y;
						rc.right  = scissorRect.m_x + scissorRect.m_width;
						rc.bottom = scissorRect.m_y + scissorRect.m_height;
						deviceCtx->RSSetScissorRects(1, &rc);
					}

					setRasterizerState(newFlags, wireframe, scissorEnabled);
				}

				if ( (BGFX_STATE_DEPTH_WRITE|BGFX_STATE_DEPTH_TEST_MASK) & changedFlags
				|| 0 != changedStencil)
				{
					setDepthStencilState(newFlags, newStencil);
				}

				if ( (0
					 | BGFX_STATE_CULL_MASK
					 | BGFX_STATE_RGB_WRITE
					 | BGFX_STATE_ALPHA_WRITE
					 | BGFX_STATE_BLEND_MASK
					 | BGFX_STATE_BLEND_EQUATION_MASK
					 | BGFX_STATE_ALPHA_REF_MASK
					 | BGFX_STATE_PT_MASK
					 | BGFX_STATE_POINT_SIZE_MASK
					 | BGFX_STATE_MSAA
					 ) & changedFlags)
				{
					if ( (BGFX_STATE_BLEND_MASK|BGFX_STATE_BLEND_EQUATION_MASK|BGFX_STATE_ALPHA_WRITE|BGFX_STATE_RGB_WRITE) & changedFlags)
					{
						setBlendState(newFlags, draw.m_rgba);
					}

					if ( (BGFX_STATE_CULL_MASK|BGFX_STATE_MSAA) & changedFlags)
					{
						setRasterizerState(newFlags, wireframe, scissorEnabled);
					}

					if (BGFX_STATE_ALPHA_REF_MASK & changedFlags)
					{
						uint32_t ref = (newFlags&BGFX_STATE_ALPHA_REF_MASK)>>BGFX_STATE_ALPHA_REF_SHIFT;
						alphaRef = ref/255.0f;
					}

					const uint64_t pt = newFlags&BGFX_STATE_PT_MASK;
					primIndex = uint8_t(pt>>BGFX_STATE_PT_SHIFT);
					if (prim.m_type != s_primInfo[primIndex].m_type)
					{
						prim = s_primInfo[primIndex];
						deviceCtx->IASetPrimitiveTopology(prim.m_type);
					}
				}

				bool programChanged = false;
				bool constantsChanged = draw.m_constBegin < draw.m_constEnd;
				rendererUpdateUniforms(this, _render->m_constantBuffer, draw.m_constBegin, draw.m_constEnd);

				if (key.m_program != programIdx)
				{
					programIdx = key.m_program;

					if (invalidHandle == programIdx)
					{
						m_currentProgram = NULL;

						deviceCtx->VSSetShader(NULL, NULL, 0);
						deviceCtx->PSSetShader(NULL, NULL, 0);
					}
					else
					{
						ProgramD3D11& program = m_program[programIdx];
						m_currentProgram = &program;

						const ShaderD3D11* vsh = program.m_vsh;
						deviceCtx->VSSetShader(vsh->m_vertexShader, NULL, 0);
						deviceCtx->VSSetConstantBuffers(0, 1, &vsh->m_buffer);

						if (NULL != m_currentColor)
						{
							const ShaderD3D11* fsh = program.m_fsh;
							deviceCtx->PSSetShader(fsh->m_pixelShader, NULL, 0);
							deviceCtx->PSSetConstantBuffers(0, 1, &fsh->m_buffer);
						}
						else
						{
							deviceCtx->PSSetShader(NULL, NULL, 0);
						}
					}

					programChanged = 
						constantsChanged = true;
				}

				if (invalidHandle != programIdx)
				{
					ProgramD3D11& program = m_program[programIdx];

					if (constantsChanged)
					{
						ConstantBuffer* vcb = program.m_vsh->m_constantBuffer;
						if (NULL != vcb)
						{
							commit(*vcb);
						}

						ConstantBuffer* fcb = program.m_fsh->m_constantBuffer;
						if (NULL != fcb)
						{
							commit(*fcb);
						}
					}

					for (uint32_t ii = 0, num = program.m_numPredefined; ii < num; ++ii)
					{
						PredefinedUniform& predefined = program.m_predefined[ii];
						uint8_t flags = predefined.m_type&BGFX_UNIFORM_FRAGMENTBIT;
						switch (predefined.m_type&(~BGFX_UNIFORM_FRAGMENTBIT) )
						{
						case PredefinedUniform::ViewRect:
							{
								float rect[4];
								rect[0] = _render->m_rect[view].m_x;
								rect[1] = _render->m_rect[view].m_y;
								rect[2] = _render->m_rect[view].m_width;
								rect[3] = _render->m_rect[view].m_height;

								setShaderConstant(flags, predefined.m_loc, &rect[0], 1);
							}
							break;

						case PredefinedUniform::ViewTexel:
							{
								float rect[4];
								rect[0] = 1.0f/float(_render->m_rect[view].m_width);
								rect[1] = 1.0f/float(_render->m_rect[view].m_height);

								setShaderConstant(flags, predefined.m_loc, &rect[0], 1);
							}
							break;

						case PredefinedUniform::View:
							{
								setShaderConstant(flags, predefined.m_loc, _render->m_view[view].un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::InvView:
							{
								if (view != invViewCached)
								{
									invViewCached = view;
									bx::float4x4_inverse(&invView.un.f4x4, &_render->m_view[view].un.f4x4);
								}

								setShaderConstant(flags, predefined.m_loc, invView.un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::Proj:
							{
								setShaderConstant(flags, predefined.m_loc, _render->m_proj[view].un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::InvProj:
							{
								if (view != invProjCached)
								{
									invProjCached = view;
									bx::float4x4_inverse(&invProj.un.f4x4, &_render->m_proj[view].un.f4x4);
								}

								setShaderConstant(flags, predefined.m_loc, invProj.un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::ViewProj:
							{
								setShaderConstant(flags, predefined.m_loc, viewProj[view].un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::InvViewProj:
							{
								if (view != invViewProjCached)
								{
									invViewProjCached = view;
									bx::float4x4_inverse(&invViewProj.un.f4x4, &viewProj[view].un.f4x4);
								}

								setShaderConstant(flags, predefined.m_loc, invViewProj.un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::Model:
							{
								const Matrix4& model = _render->m_matrixCache.m_cache[draw.m_matrix];
								setShaderConstant(flags, predefined.m_loc, model.un.val, bx::uint32_min(draw.m_num*4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::ModelView:
							{
								Matrix4 modelView;
								const Matrix4& model = _render->m_matrixCache.m_cache[draw.m_matrix];
								bx::float4x4_mul(&modelView.un.f4x4, &model.un.f4x4, &_render->m_view[view].un.f4x4);
								setShaderConstant(flags, predefined.m_loc, modelView.un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::ModelViewProj:
							{
								Matrix4 modelViewProj;
								const Matrix4& model = _render->m_matrixCache.m_cache[draw.m_matrix];
								bx::float4x4_mul(&modelViewProj.un.f4x4, &model.un.f4x4, &viewProj[view].un.f4x4);
								setShaderConstant(flags, predefined.m_loc, modelViewProj.un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::AlphaRef:
							{
								setShaderConstant(flags, predefined.m_loc, &alphaRef, 1);
							}
							break;

						default:
							BX_CHECK(false, "predefined %d not handled", predefined.m_type);
							break;
						}
					}

					if (constantsChanged
					||  program.m_numPredefined > 0)
					{
						commitShaderConstants();
					}
				}

				{
					uint32_t changes = 0;
					for (uint32_t stage = 0; stage < BGFX_CONFIG_MAX_TEXTURE_SAMPLERS; ++stage)
					{
						const Sampler& sampler = draw.m_sampler[stage];
						Sampler& current = currentState.m_sampler[stage];
						if (current.m_idx != sampler.m_idx
						||  current.m_flags != sampler.m_flags
						||  programChanged)
						{
							if (invalidHandle != sampler.m_idx)
							{
								TextureD3D11& texture = m_textures[sampler.m_idx];
								texture.commit(stage, sampler.m_flags);
							}
							else
							{
								m_textureStage.m_srv[stage] = NULL;
								m_textureStage.m_sampler[stage] = NULL;
							}

							++changes;
						}

						current = sampler;
					}

					if (0 < changes)
					{
						commitTextureStage();
					}
				}

				if (programChanged
				||  currentState.m_vertexBuffer.idx != draw.m_vertexBuffer.idx
				||  currentState.m_instanceDataBuffer.idx != draw.m_instanceDataBuffer.idx
				||  currentState.m_instanceDataOffset != draw.m_instanceDataOffset
				||  currentState.m_instanceDataStride != draw.m_instanceDataStride)
				{
					currentState.m_vertexBuffer = draw.m_vertexBuffer;
					currentState.m_instanceDataBuffer.idx = draw.m_instanceDataBuffer.idx;
					currentState.m_instanceDataOffset = draw.m_instanceDataOffset;
					currentState.m_instanceDataStride = draw.m_instanceDataStride;

					uint16_t handle = draw.m_vertexBuffer.idx;
					if (invalidHandle != handle)
					{
						const VertexBufferD3D11& vb = m_vertexBuffers[handle];

						uint16_t decl = !isValid(vb.m_decl) ? draw.m_vertexDecl.idx : vb.m_decl.idx;
						const VertexDecl& vertexDecl = m_vertexDecls[decl];
						uint32_t stride = vertexDecl.m_stride;
						uint32_t offset = 0;
						deviceCtx->IASetVertexBuffers(0, 1, &vb.m_ptr, &stride, &offset);

						if (isValid(draw.m_instanceDataBuffer) )
						{
 							const VertexBufferD3D11& inst = m_vertexBuffers[draw.m_instanceDataBuffer.idx];
							uint32_t instStride = draw.m_instanceDataStride;
							deviceCtx->IASetVertexBuffers(1, 1, &inst.m_ptr, &instStride, &draw.m_instanceDataOffset);
							setInputLayout(vertexDecl, m_program[programIdx], draw.m_instanceDataStride/16);
						}
						else
						{
							deviceCtx->IASetVertexBuffers(1, 0, NULL, NULL, NULL);
							setInputLayout(vertexDecl, m_program[programIdx], 0);
						}
					}
					else
					{
						deviceCtx->IASetVertexBuffers(0, 0, NULL, NULL, NULL);
					}
				}

				if (currentState.m_indexBuffer.idx != draw.m_indexBuffer.idx)
				{
					currentState.m_indexBuffer = draw.m_indexBuffer;

					uint16_t handle = draw.m_indexBuffer.idx;
					if (invalidHandle != handle)
					{
						const IndexBufferD3D11& ib = m_indexBuffers[handle];
						deviceCtx->IASetIndexBuffer(ib.m_ptr, DXGI_FORMAT_R16_UINT, 0);
					}
					else
					{
						deviceCtx->IASetIndexBuffer(NULL, DXGI_FORMAT_R16_UINT, 0);
					}
				}

				if (isValid(currentState.m_vertexBuffer) )
				{
					uint32_t numVertices = draw.m_numVertices;
					if (UINT32_MAX == numVertices)
					{
						const VertexBufferD3D11& vb = m_vertexBuffers[currentState.m_vertexBuffer.idx];
						uint16_t decl = !isValid(vb.m_decl) ? draw.m_vertexDecl.idx : vb.m_decl.idx;
						const VertexDecl& vertexDecl = m_vertexDecls[decl];
						numVertices = vb.m_size/vertexDecl.m_stride;
					}

					uint32_t numIndices = 0;
					uint32_t numPrimsSubmitted = 0;
					uint32_t numInstances = 0;
					uint32_t numPrimsRendered = 0;

					if (isValid(draw.m_indexBuffer) )
					{
						if (UINT32_MAX == draw.m_numIndices)
						{
							numIndices = m_indexBuffers[draw.m_indexBuffer.idx].m_size/2;
							numPrimsSubmitted = numIndices/prim.m_div - prim.m_sub;
							numInstances = draw.m_numInstances;
							numPrimsRendered = numPrimsSubmitted*draw.m_numInstances;

							deviceCtx->DrawIndexedInstanced(numIndices
								, draw.m_numInstances
								, 0
								, draw.m_startVertex
								, 0
								);
						}
						else if (prim.m_min <= draw.m_numIndices)
						{
							numIndices = draw.m_numIndices;
							numPrimsSubmitted = numIndices/prim.m_div - prim.m_sub;
							numInstances = draw.m_numInstances;
							numPrimsRendered = numPrimsSubmitted*draw.m_numInstances;

							deviceCtx->DrawIndexedInstanced(numIndices
								, draw.m_numInstances
								, draw.m_startIndex
								, draw.m_startVertex
								, 0
								);
						}
					}
					else
					{
						numPrimsSubmitted = numVertices/prim.m_div - prim.m_sub;
						numInstances = draw.m_numInstances;
						numPrimsRendered = numPrimsSubmitted*draw.m_numInstances;

						deviceCtx->DrawInstanced(numVertices
							, draw.m_numInstances
							, draw.m_startVertex
							, 0
							);
					}

					statsNumPrimsSubmitted[primIndex] += numPrimsSubmitted;
					statsNumPrimsRendered[primIndex]  += numPrimsRendered;
					statsNumInstances[primIndex]      += numInstances;
					statsNumIndices += numIndices;
				}
			}

			if (0 < _render->m_num)
			{
				captureElapsed = -bx::getHPCounter();
				capture();
				captureElapsed += bx::getHPCounter();
			}
		}

		int64_t now = bx::getHPCounter();
		elapsed += now;

		static int64_t last = now;
		int64_t frameTime = now - last;
		last = now;

		static int64_t min = frameTime;
		static int64_t max = frameTime;
		min = min > frameTime ? frameTime : min;
		max = max < frameTime ? frameTime : max;

		if (_render->m_debug & (BGFX_DEBUG_IFH|BGFX_DEBUG_STATS) )
		{
			PIX_BEGINEVENT(D3DCOLOR_RGBA(0x40, 0x40, 0x40, 0xff), L"debugstats");

			TextVideoMem& tvm = m_textVideoMem;

			static int64_t next = now;

			if (now >= next)
			{
				next = now + bx::getHPFrequency();
				double freq = double(bx::getHPFrequency() );
				double toMs = 1000.0/freq;

				tvm.clear();
				uint16_t pos = 0;
				tvm.printf(0, pos++, BGFX_CONFIG_DEBUG ? 0x89 : 0x8f
					, " %s / " BX_COMPILER_NAME " / " BX_CPU_NAME " / " BX_ARCH_NAME " / " BX_PLATFORM_NAME " "
					, getRendererName()
					);

				const DXGI_ADAPTER_DESC& desc = m_adapterDesc;
				char description[BX_COUNTOF(desc.Description)];
				wcstombs(description, desc.Description, BX_COUNTOF(desc.Description) );
				tvm.printf(0, pos++, 0x0f, " Device: %s", description);

				char dedicatedVideo[16];
				bx::prettify(dedicatedVideo, BX_COUNTOF(dedicatedVideo), desc.DedicatedVideoMemory);

				char dedicatedSystem[16];
				bx::prettify(dedicatedSystem, BX_COUNTOF(dedicatedSystem), desc.DedicatedSystemMemory);

				char sharedSystem[16];
				bx::prettify(sharedSystem, BX_COUNTOF(sharedSystem), desc.SharedSystemMemory);
				
				tvm.printf(0, pos++, 0x0f, " Memory: %s (video), %s (system), %s (shared)"
					, dedicatedVideo
					, dedicatedSystem
					, sharedSystem
					);

				pos = 10;
				tvm.printf(10, pos++, 0x8e, "       Frame: %7.3f, % 7.3f \x1f, % 7.3f \x1e [ms] / % 6.2f FPS "
					, double(frameTime)*toMs
					, double(min)*toMs
					, double(max)*toMs
					, freq/frameTime
					);

				const uint32_t msaa = (m_resolution.m_flags&BGFX_RESET_MSAA_MASK)>>BGFX_RESET_MSAA_SHIFT;
				tvm.printf(10, pos++, 0x8e, " Reset flags: [%c] vsync, [%c] MSAAx%d "
					, !!(m_resolution.m_flags&BGFX_RESET_VSYNC) ? '\xfe' : ' '
					, 0 != msaa ? '\xfe' : ' '
					, 1<<msaa
					);

				double elapsedCpuMs = double(elapsed)*toMs;
				tvm.printf(10, pos++, 0x8e, "  Draw calls: %4d / CPU %3.4f [ms]"
					, _render->m_num
					, elapsedCpuMs
					);
				for (uint32_t ii = 0; ii < BX_COUNTOF(s_primName); ++ii)
				{
					tvm.printf(10, pos++, 0x8e, "    %8s: %7d (#inst: %5d), submitted: %7d"
						, s_primName[ii]
						, statsNumPrimsRendered[ii]
						, statsNumInstances[ii]
						, statsNumPrimsSubmitted[ii]
						);
				}

				if (NULL != m_renderdocdll)
				{
					tvm.printf(tvm.m_width-27, 0, 0x1f, " [F11 - RenderDoc capture] ");
				}

				tvm.printf(10, pos++, 0x8e, "     Indices: %7d", statsNumIndices);
				tvm.printf(10, pos++, 0x8e, "    DVB size: %7d", _render->m_vboffset);
				tvm.printf(10, pos++, 0x8e, "    DIB size: %7d", _render->m_iboffset);

				pos++;
				tvm.printf(10, pos++, 0x8e, " State cache:                                ");
				tvm.printf(10, pos++, 0x8e, " Blend  | DepthS | Input  | Raster | Sampler ");
				tvm.printf(10, pos++, 0x8e, " %6d | %6d | %6d | %6d | %6d  "
					, m_blendStateCache.getCount()
					, m_depthStencilStateCache.getCount()
					, m_inputLayoutCache.getCount()
					, m_rasterizerStateCache.getCount()
					, m_samplerStateCache.getCount()
					);
				pos++;

				double captureMs = double(captureElapsed)*toMs;
				tvm.printf(10, pos++, 0x8e, "     Capture: %3.4f [ms]", captureMs);

				uint8_t attr[2] = { 0x89, 0x8a };
				uint8_t attrIndex = _render->m_waitSubmit < _render->m_waitRender;

				tvm.printf(10, pos++, attr[attrIndex&1], " Submit wait: %3.4f [ms]", _render->m_waitSubmit*toMs);
				tvm.printf(10, pos++, attr[(attrIndex+1)&1], " Render wait: %3.4f [ms]", _render->m_waitRender*toMs);

				min = frameTime;
				max = frameTime;
			}

			blit(this, _textVideoMemBlitter, tvm);

			PIX_ENDEVENT();
		}
		else if (_render->m_debug & BGFX_DEBUG_TEXT)
		{
			PIX_BEGINEVENT(D3DCOLOR_RGBA(0x40, 0x40, 0x40, 0xff), L"debugtext");

			blit(this, _textVideoMemBlitter, _render->m_textVideoMem);

			PIX_ENDEVENT();
		}
	}
} // namespace bgfx

#else

namespace bgfx
{
	RendererContextI* rendererCreateD3D11()
	{
		return NULL;
	}

	void rendererDestroyD3D11()
	{
	}
} // namespace bgfx

#endif // BGFX_CONFIG_RENDERER_DIRECT3D11
/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "bgfx_p.h"

#if BGFX_CONFIG_RENDERER_DIRECT3D12
#else

namespace bgfx
{
	RendererContextI* rendererCreateD3D12()
	{
		return NULL;
	}

	void rendererDestroyD3D12()
	{
	}
} // namespace bgfx

#endif // BGFX_CONFIG_RENDERER_DIRECT3D12
/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "bgfx_p.h"

#if BGFX_CONFIG_RENDERER_DIRECT3D9
#	include "renderer_d3d9.h"

namespace bgfx
{
	static wchar_t s_viewNameW[BGFX_CONFIG_MAX_VIEWS][256];

	struct PrimInfo
	{
		D3DPRIMITIVETYPE m_type;
		uint32_t m_min;
		uint32_t m_div;
		uint32_t m_sub;
	};

	static const PrimInfo s_primInfo[] =
	{
		{ D3DPT_TRIANGLELIST,  3, 3, 0 },
		{ D3DPT_TRIANGLESTRIP, 3, 1, 2 },
		{ D3DPT_LINELIST,      2, 2, 0 },
		{ D3DPT_POINTLIST,     1, 1, 0 },
		{ D3DPRIMITIVETYPE(0), 0, 0, 0 },
	};

	static const char* s_primName[] =
	{
		"TriList",
		"TriStrip",
		"Line",
		"Point",
	};
	BX_STATIC_ASSERT(BX_COUNTOF(s_primInfo) == BX_COUNTOF(s_primName)+1);

	static const D3DMULTISAMPLE_TYPE s_checkMsaa[] =
	{
		D3DMULTISAMPLE_NONE,
		D3DMULTISAMPLE_2_SAMPLES,
		D3DMULTISAMPLE_4_SAMPLES,
		D3DMULTISAMPLE_8_SAMPLES,
		D3DMULTISAMPLE_16_SAMPLES,
	};

	static Msaa s_msaa[] =
	{
		{ D3DMULTISAMPLE_NONE,       0 },
		{ D3DMULTISAMPLE_2_SAMPLES,  0 },
		{ D3DMULTISAMPLE_4_SAMPLES,  0 },
		{ D3DMULTISAMPLE_8_SAMPLES,  0 },
		{ D3DMULTISAMPLE_16_SAMPLES, 0 },
	};

	struct Blend
	{
		D3DBLEND m_src;
		D3DBLEND m_dst;
		bool m_factor;
	};

	static const Blend s_blendFactor[] =
	{
		{ (D3DBLEND)0,             (D3DBLEND)0,             false }, // ignored
		{ D3DBLEND_ZERO,           D3DBLEND_ZERO,           false }, // ZERO
		{ D3DBLEND_ONE,            D3DBLEND_ONE,            false }, // ONE
		{ D3DBLEND_SRCCOLOR,       D3DBLEND_SRCCOLOR,       false }, // SRC_COLOR
		{ D3DBLEND_INVSRCCOLOR,    D3DBLEND_INVSRCCOLOR,    false }, // INV_SRC_COLOR
		{ D3DBLEND_SRCALPHA,       D3DBLEND_SRCALPHA,       false }, // SRC_ALPHA
		{ D3DBLEND_INVSRCALPHA,    D3DBLEND_INVSRCALPHA,    false }, // INV_SRC_ALPHA
		{ D3DBLEND_DESTALPHA,      D3DBLEND_DESTALPHA,      false }, // DST_ALPHA
		{ D3DBLEND_INVDESTALPHA,   D3DBLEND_INVDESTALPHA,   false }, // INV_DST_ALPHA
		{ D3DBLEND_DESTCOLOR,      D3DBLEND_DESTCOLOR,      false }, // DST_COLOR
		{ D3DBLEND_INVDESTCOLOR,   D3DBLEND_INVDESTCOLOR,   false }, // INV_DST_COLOR
		{ D3DBLEND_SRCALPHASAT,    D3DBLEND_ONE,            false }, // SRC_ALPHA_SAT
		{ D3DBLEND_BLENDFACTOR,    D3DBLEND_BLENDFACTOR,    true  }, // FACTOR
		{ D3DBLEND_INVBLENDFACTOR, D3DBLEND_INVBLENDFACTOR, true  }, // INV_FACTOR
	};

	static const D3DBLENDOP s_blendEquation[] =
	{
		D3DBLENDOP_ADD,
		D3DBLENDOP_SUBTRACT,
		D3DBLENDOP_REVSUBTRACT,
		D3DBLENDOP_MIN,
		D3DBLENDOP_MAX,
	};

	static const D3DCMPFUNC s_cmpFunc[] =
	{
		(D3DCMPFUNC)0, // ignored
		D3DCMP_LESS,
		D3DCMP_LESSEQUAL,
		D3DCMP_EQUAL,
		D3DCMP_GREATEREQUAL,
		D3DCMP_GREATER,
		D3DCMP_NOTEQUAL,
		D3DCMP_NEVER,
		D3DCMP_ALWAYS,
	};

	static const D3DSTENCILOP s_stencilOp[] =
	{
		D3DSTENCILOP_ZERO,
		D3DSTENCILOP_KEEP,
		D3DSTENCILOP_REPLACE,
		D3DSTENCILOP_INCR,
		D3DSTENCILOP_INCRSAT,
		D3DSTENCILOP_DECR,
		D3DSTENCILOP_DECRSAT,
		D3DSTENCILOP_INVERT,
	};

	static const D3DRENDERSTATETYPE s_stencilFuncRs[] =
	{
		D3DRS_STENCILFUNC,
		D3DRS_CCW_STENCILFUNC,
	};

	static const D3DRENDERSTATETYPE s_stencilFailRs[] =
	{
		D3DRS_STENCILFAIL,
		D3DRS_CCW_STENCILFAIL,
	};

	static const D3DRENDERSTATETYPE s_stencilZFailRs[] =
	{
		D3DRS_STENCILZFAIL,
		D3DRS_CCW_STENCILZFAIL,
	};

	static const D3DRENDERSTATETYPE s_stencilZPassRs[] =
	{
		D3DRS_STENCILPASS,
		D3DRS_CCW_STENCILPASS,
	};

	static const D3DCULL s_cullMode[] =
	{
		D3DCULL_NONE,
		D3DCULL_CW,
		D3DCULL_CCW,
	};

	static const D3DFORMAT s_checkColorFormats[] =
	{
		D3DFMT_UNKNOWN,
		D3DFMT_A8R8G8B8, D3DFMT_UNKNOWN,
		D3DFMT_R32F, D3DFMT_R16F, D3DFMT_G16R16, D3DFMT_A8R8G8B8, D3DFMT_UNKNOWN,

		D3DFMT_UNKNOWN, // terminator
	};

	static D3DFORMAT s_colorFormat[] =
	{
		D3DFMT_UNKNOWN, // ignored
		D3DFMT_A8R8G8B8,
		D3DFMT_A2B10G10R10,
		D3DFMT_A16B16G16R16,
		D3DFMT_A16B16G16R16F,
		D3DFMT_R16F,
		D3DFMT_R32F,
	};

	static const D3DTEXTUREADDRESS s_textureAddress[] =
	{
		D3DTADDRESS_WRAP,
		D3DTADDRESS_MIRROR,
		D3DTADDRESS_CLAMP,
	};

	static const D3DTEXTUREFILTERTYPE s_textureFilter[] =
	{
		D3DTEXF_LINEAR,
		D3DTEXF_POINT,
		D3DTEXF_ANISOTROPIC,
	};

	struct TextureFormatInfo
	{
		D3DFORMAT m_fmt;
	};

	static TextureFormatInfo s_textureFormat[] =
	{
		{ D3DFMT_DXT1          }, // BC1 
		{ D3DFMT_DXT3          }, // BC2
		{ D3DFMT_DXT5          }, // BC3
		{ D3DFMT_UNKNOWN       }, // BC4
		{ D3DFMT_UNKNOWN       }, // BC5
		{ D3DFMT_UNKNOWN       }, // BC6H
		{ D3DFMT_UNKNOWN       }, // BC7
		{ D3DFMT_UNKNOWN       }, // ETC1
		{ D3DFMT_UNKNOWN       }, // ETC2
		{ D3DFMT_UNKNOWN       }, // ETC2A
		{ D3DFMT_UNKNOWN       }, // ETC2A1
		{ D3DFMT_UNKNOWN       }, // PTC12
		{ D3DFMT_UNKNOWN       }, // PTC14
		{ D3DFMT_UNKNOWN       }, // PTC12A
		{ D3DFMT_UNKNOWN       }, // PTC14A
		{ D3DFMT_UNKNOWN       }, // PTC22
		{ D3DFMT_UNKNOWN       }, // PTC24
		{ D3DFMT_UNKNOWN       }, // Unknown
		{ D3DFMT_A1            }, // R1
		{ D3DFMT_L8            }, // R8
		{ D3DFMT_G16R16        }, // R16
		{ D3DFMT_R16F          }, // R16F
		{ D3DFMT_UNKNOWN       }, // R32
		{ D3DFMT_R32F          }, // R32F
		{ D3DFMT_A8L8          }, // RG8
		{ D3DFMT_G16R16        }, // RG16
		{ D3DFMT_G16R16F       }, // RG16F
		{ D3DFMT_UNKNOWN       }, // RG32
		{ D3DFMT_G32R32F       }, // RG32F
		{ D3DFMT_A8R8G8B8      }, // BGRA8
		{ D3DFMT_A16B16G16R16  }, // RGBA16
		{ D3DFMT_A16B16G16R16F }, // RGBA16F
		{ D3DFMT_UNKNOWN       }, // RGBA32
		{ D3DFMT_A32B32G32R32F }, // RGBA32F
		{ D3DFMT_R5G6B5        }, // R5G6B5
		{ D3DFMT_A4R4G4B4      }, // RGBA4
		{ D3DFMT_A1R5G5B5      }, // RGB5A1
		{ D3DFMT_A2B10G10R10   }, // RGB10A2
		{ D3DFMT_UNKNOWN       }, // R11G11B10F
		{ D3DFMT_UNKNOWN       }, // UnknownDepth
		{ D3DFMT_D16           }, // D16  
		{ D3DFMT_D24X8         }, // D24  
		{ D3DFMT_D24S8         }, // D24S8
		{ D3DFMT_D32           }, // D32  
		{ D3DFMT_DF16          }, // D16F 
		{ D3DFMT_DF24          }, // D24F
		{ D3DFMT_D32F_LOCKABLE }, // D32F
		{ D3DFMT_S8_LOCKABLE   }, // D0S8
	};
	BX_STATIC_ASSERT(TextureFormat::Count == BX_COUNTOF(s_textureFormat) );

	static ExtendedFormat s_extendedFormats[ExtendedFormat::Count] =
	{
		{ D3DFMT_ATI1, 0,                     D3DRTYPE_TEXTURE, false },
		{ D3DFMT_ATI2, 0,                     D3DRTYPE_TEXTURE, false },
		{ D3DFMT_DF16, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, false },
		{ D3DFMT_DF24, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, false },
		{ D3DFMT_INST, 0,                     D3DRTYPE_SURFACE, false },
		{ D3DFMT_INTZ, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, false },
		{ D3DFMT_NULL, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, false },
		{ D3DFMT_RESZ, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, false },
		{ D3DFMT_RAWZ, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, false },
	};

	static const GUID IID_IDirect3D9         = { 0x81bdcbca, 0x64d4, 0x426d, { 0xae, 0x8d, 0xad, 0x1, 0x47, 0xf4, 0x27, 0x5c } };
	static const GUID IID_IDirect3DDevice9Ex = { 0xb18b10ce, 0x2649, 0x405a, { 0x87, 0xf, 0x95, 0xf7, 0x77, 0xd4, 0x31, 0x3a } };

	static PFN_D3DPERF_SET_MARKER  D3DPERF_SetMarker;
	static PFN_D3DPERF_BEGIN_EVENT D3DPERF_BeginEvent;
	static PFN_D3DPERF_END_EVENT   D3DPERF_EndEvent;

	struct RendererContextD3D9 : public RendererContextI
	{
		RendererContextD3D9()
			: m_d3d9(NULL)
			, m_device(NULL)
			, m_captureTexture(NULL)
			, m_captureSurface(NULL)
			, m_captureResolve(NULL)
			, m_flags(BGFX_RESET_NONE)
			, m_initialized(false)
			, m_amd(false)
			, m_nvidia(false)
			, m_instancing(false)
			, m_rtMsaa(false)
		{
			m_fbh.idx = invalidHandle;
			memset(m_uniforms, 0, sizeof(m_uniforms) );
			memset(&m_resolution, 0, sizeof(m_resolution) );

			D3DFORMAT adapterFormat = D3DFMT_X8R8G8B8;

			// http://msdn.microsoft.com/en-us/library/windows/desktop/bb172588%28v=vs.85%29.aspx
			memset(&m_params, 0, sizeof(m_params) );
			m_params.BackBufferWidth = BGFX_DEFAULT_WIDTH;
			m_params.BackBufferHeight = BGFX_DEFAULT_HEIGHT;
			m_params.BackBufferFormat = adapterFormat;
			m_params.BackBufferCount = 1;
			m_params.MultiSampleType = D3DMULTISAMPLE_NONE;
			m_params.MultiSampleQuality = 0;
			m_params.EnableAutoDepthStencil = TRUE;
			m_params.AutoDepthStencilFormat = D3DFMT_D24S8;
			m_params.Flags = D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL;
#if BX_PLATFORM_WINDOWS
			m_params.FullScreen_RefreshRateInHz = 0;
			m_params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
			m_params.SwapEffect = D3DSWAPEFFECT_DISCARD;
			m_params.hDeviceWindow = NULL;
			m_params.Windowed = true;

			RECT rect;
			GetWindowRect(g_bgfxHwnd, &rect);
			m_params.BackBufferWidth = rect.right-rect.left;
			m_params.BackBufferHeight = rect.bottom-rect.top;

			m_d3d9dll = bx::dlopen("d3d9.dll");
			BGFX_FATAL(NULL != m_d3d9dll, Fatal::UnableToInitialize, "Failed to load d3d9.dll.");

			if (BX_ENABLED(BGFX_CONFIG_DEBUG_PIX) )
			{
				D3DPERF_SetMarker  = (PFN_D3DPERF_SET_MARKER )bx::dlsym(m_d3d9dll, "D3DPERF_SetMarker");
				D3DPERF_BeginEvent = (PFN_D3DPERF_BEGIN_EVENT)bx::dlsym(m_d3d9dll, "D3DPERF_BeginEvent");
				D3DPERF_EndEvent   = (PFN_D3DPERF_END_EVENT  )bx::dlsym(m_d3d9dll, "D3DPERF_EndEvent");

				BX_CHECK(NULL != D3DPERF_SetMarker
					  && NULL != D3DPERF_BeginEvent
					  && NULL != D3DPERF_EndEvent
					  , "Failed to initialize PIX events."
					  );
			}
#if BGFX_CONFIG_RENDERER_DIRECT3D9EX
			m_d3d9ex = NULL;

			Direct3DCreate9ExFn direct3DCreate9Ex = (Direct3DCreate9ExFn)bx::dlsym(m_d3d9dll, "Direct3DCreate9Ex");
			if (NULL != direct3DCreate9Ex)
			{
				direct3DCreate9Ex(D3D_SDK_VERSION, &m_d3d9ex);
				DX_CHECK(m_d3d9ex->QueryInterface(IID_IDirect3D9, (void**)&m_d3d9) );
				m_pool = D3DPOOL_DEFAULT;
			}
			else
#endif // BGFX_CONFIG_RENDERER_DIRECT3D9EX
			{
				Direct3DCreate9Fn direct3DCreate9 = (Direct3DCreate9Fn)bx::dlsym(m_d3d9dll, "Direct3DCreate9");
				BGFX_FATAL(NULL != direct3DCreate9, Fatal::UnableToInitialize, "Function Direct3DCreate9 not found.");
				m_d3d9 = direct3DCreate9(D3D_SDK_VERSION);
				m_pool = D3DPOOL_MANAGED;
			}

			BGFX_FATAL(m_d3d9, Fatal::UnableToInitialize, "Unable to create Direct3D.");

			m_adapter = D3DADAPTER_DEFAULT;
			m_deviceType = D3DDEVTYPE_HAL;

			uint32_t adapterCount = m_d3d9->GetAdapterCount();
			for (uint32_t ii = 0; ii < adapterCount; ++ii)
			{
				D3DADAPTER_IDENTIFIER9 identifier;
				HRESULT hr = m_d3d9->GetAdapterIdentifier(ii, 0, &identifier);
				if (SUCCEEDED(hr) )
				{
					BX_TRACE("Adapter #%d", ii);
					BX_TRACE("\tDriver: %s", identifier.Driver);
					BX_TRACE("\tDescription: %s", identifier.Description);
					BX_TRACE("\tDeviceName: %s", identifier.DeviceName);
					BX_TRACE("\tVendorId: 0x%08x, DeviceId: 0x%08x, SubSysId: 0x%08x, Revision: 0x%08x"
						, identifier.VendorId
						, identifier.DeviceId
						, identifier.SubSysId
						, identifier.Revision
						);

#if BGFX_CONFIG_DEBUG_PERFHUD
					if (0 != strstr(identifier.Description, "PerfHUD") )
					{
						m_adapter = ii;
						m_deviceType = D3DDEVTYPE_REF;
					}
#endif // BGFX_CONFIG_DEBUG_PERFHUD
				}
			}

			DX_CHECK(m_d3d9->GetAdapterIdentifier(m_adapter, 0, &m_identifier) );
			m_amd = m_identifier.VendorId == 0x1002;
			m_nvidia = m_identifier.VendorId == 0x10de;

			uint32_t behaviorFlags[] =
			{
				D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE | D3DCREATE_PUREDEVICE,
				D3DCREATE_MIXED_VERTEXPROCESSING    | D3DCREATE_FPU_PRESERVE,
				D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
			};

			for (uint32_t ii = 0; ii < BX_COUNTOF(behaviorFlags) && NULL == m_device; ++ii)
			{
#if 0 // BGFX_CONFIG_RENDERER_DIRECT3D9EX
				DX_CHECK(m_d3d9->CreateDeviceEx(m_adapter
						, m_deviceType
						, g_bgfxHwnd
						, behaviorFlags[ii]
						, &m_params
						, NULL
						, &m_device
						) );
#else
				DX_CHECK(m_d3d9->CreateDevice(m_adapter
					, m_deviceType
					, g_bgfxHwnd
					, behaviorFlags[ii]
					, &m_params
					, &m_device
					) );
#endif // BGFX_CONFIG_RENDERER_DIRECT3D9EX
			}

			BGFX_FATAL(m_device, Fatal::UnableToInitialize, "Unable to create Direct3D9 device.");

			m_numWindows = 1;

#if BGFX_CONFIG_RENDERER_DIRECT3D9EX
			if (NULL != m_d3d9ex)
			{
				DX_CHECK(m_device->QueryInterface(IID_IDirect3DDevice9Ex, (void**)&m_deviceEx) );
			}
#endif // BGFX_CONFIG_RENDERER_DIRECT3D9EX

			DX_CHECK(m_device->GetDeviceCaps(&m_caps) );

			// For shit GPUs that can create DX9 device but can't do simple stuff. GTFO!
			BGFX_FATAL( (D3DPTEXTURECAPS_SQUAREONLY & m_caps.TextureCaps) == 0, Fatal::MinimumRequiredSpecs, "D3DPTEXTURECAPS_SQUAREONLY");
			BGFX_FATAL( (D3DPTEXTURECAPS_MIPMAP & m_caps.TextureCaps) == D3DPTEXTURECAPS_MIPMAP, Fatal::MinimumRequiredSpecs, "D3DPTEXTURECAPS_MIPMAP");
			BGFX_FATAL( (D3DPTEXTURECAPS_ALPHA & m_caps.TextureCaps) == D3DPTEXTURECAPS_ALPHA, Fatal::MinimumRequiredSpecs, "D3DPTEXTURECAPS_ALPHA");
			BGFX_FATAL(m_caps.VertexShaderVersion >= D3DVS_VERSION(2, 0) && m_caps.PixelShaderVersion >= D3DPS_VERSION(2, 1)
					  , Fatal::MinimumRequiredSpecs
					  , "Shader Model Version (vs: %x, ps: %x)."
					  , m_caps.VertexShaderVersion
					  , m_caps.PixelShaderVersion
					  );
			BX_TRACE("Max vertex shader 3.0 instr. slots: %d", m_caps.MaxVertexShader30InstructionSlots);
			BX_TRACE("Max vertex shader constants: %d", m_caps.MaxVertexShaderConst);
			BX_TRACE("Max fragment shader 2.0 instr. slots: %d", m_caps.PS20Caps.NumInstructionSlots);
			BX_TRACE("Max fragment shader 3.0 instr. slots: %d", m_caps.MaxPixelShader30InstructionSlots);
			BX_TRACE("Num simultaneous render targets: %d", m_caps.NumSimultaneousRTs);

			g_caps.supported |= ( 0
								| BGFX_CAPS_TEXTURE_3D
								| BGFX_CAPS_TEXTURE_COMPARE_LEQUAL
								| BGFX_CAPS_VERTEX_ATTRIB_HALF
								| BGFX_CAPS_FRAGMENT_DEPTH
								| BGFX_CAPS_SWAP_CHAIN
								);
			g_caps.maxTextureSize = bx::uint32_min(m_caps.MaxTextureWidth, m_caps.MaxTextureHeight);

			m_caps.NumSimultaneousRTs = bx::uint32_min(m_caps.NumSimultaneousRTs, BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS);
			g_caps.maxFBAttachments = (uint8_t)m_caps.NumSimultaneousRTs;

			if (BX_ENABLED(BGFX_CONFIG_RENDERER_USE_EXTENSIONS) )
			{
				BX_TRACE("Extended formats:");
				for (uint32_t ii = 0; ii < ExtendedFormat::Count; ++ii)
				{
					ExtendedFormat& fmt = s_extendedFormats[ii];
					fmt.m_supported = SUCCEEDED(m_d3d9->CheckDeviceFormat(m_adapter, m_deviceType, adapterFormat, fmt.m_usage, fmt.m_type, fmt.m_fmt) );
					const char* fourcc = (const char*)&fmt.m_fmt;
					BX_TRACE("\t%2d: %c%c%c%c %s", ii, fourcc[0], fourcc[1], fourcc[2], fourcc[3], fmt.m_supported ? "supported" : "");
					BX_UNUSED(fourcc);
				}

				m_instancing = false
					|| s_extendedFormats[ExtendedFormat::Inst].m_supported
					|| (m_caps.VertexShaderVersion >= D3DVS_VERSION(3, 0) )
					;

				if (m_amd
				&&  s_extendedFormats[ExtendedFormat::Inst].m_supported)
				{   // AMD only
					m_device->SetRenderState(D3DRS_POINTSIZE, D3DFMT_INST);
				}

				if (s_extendedFormats[ExtendedFormat::Intz].m_supported)
				{
					s_textureFormat[TextureFormat::D24].m_fmt = D3DFMT_INTZ;
					s_textureFormat[TextureFormat::D32].m_fmt = D3DFMT_INTZ;
				}

				s_textureFormat[TextureFormat::BC4].m_fmt = s_extendedFormats[ExtendedFormat::Ati1].m_supported ? D3DFMT_ATI1 : D3DFMT_UNKNOWN;
				s_textureFormat[TextureFormat::BC5].m_fmt = s_extendedFormats[ExtendedFormat::Ati2].m_supported ? D3DFMT_ATI2 : D3DFMT_UNKNOWN;

				g_caps.supported |= m_instancing ? BGFX_CAPS_INSTANCING : 0;

				for (uint32_t ii = 0; ii < TextureFormat::Count; ++ii)
				{
					g_caps.formats[ii] = SUCCEEDED(m_d3d9->CheckDeviceFormat(m_adapter
						, m_deviceType
						, adapterFormat
						, 0
						, D3DRTYPE_TEXTURE
						, s_textureFormat[ii].m_fmt
						) ) ? 1 : 0;
				}
			}

			uint32_t index = 1;
			for (const D3DFORMAT* fmt = &s_checkColorFormats[index]; *fmt != D3DFMT_UNKNOWN; ++fmt, ++index)
			{
				for (; *fmt != D3DFMT_UNKNOWN; ++fmt)
				{
					if (SUCCEEDED(m_d3d9->CheckDeviceFormat(m_adapter, m_deviceType, adapterFormat, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, *fmt) ) )
					{
						s_colorFormat[index] = *fmt;
						break;
					}
				}

				for (; *fmt != D3DFMT_UNKNOWN; ++fmt);
			}

			m_fmtDepth = D3DFMT_D24S8;

#elif BX_PLATFORM_XBOX360
			m_params.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
			m_params.DisableAutoBackBuffer = FALSE;
			m_params.DisableAutoFrontBuffer = FALSE;
			m_params.FrontBufferFormat = D3DFMT_X8R8G8B8;
			m_params.FrontBufferColorSpace = D3DCOLORSPACE_RGB;

			m_d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
			BX_TRACE("Creating D3D9 %p", m_d3d9);

			XVIDEO_MODE videoMode;
			XGetVideoMode(&videoMode);
			if (!videoMode.fIsWideScreen)
			{
				m_params.Flags |= D3DPRESENTFLAG_NO_LETTERBOX;
			}

			BX_TRACE("Creating device");
			DX_CHECK(m_d3d9->CreateDevice(m_adapter
					, m_deviceType
					, NULL
					, D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_BUFFER_2_FRAMES
					, &m_params
					, &m_device
					) );

			BX_TRACE("Device %p", m_device);

			m_fmtDepth = D3DFMT_D24FS8;
#endif // BX_PLATFORM_WINDOWS

			{
				IDirect3DSwapChain9* swapChain;
				DX_CHECK(m_device->GetSwapChain(0, &swapChain) );

				// GPA increases swapchain ref count.
				//
				// This causes assert in debug. When debugger is present refcount
				// checks are off.
				setGraphicsDebuggerPresent(1 != getRefCount(swapChain) );

				DX_RELEASE(swapChain, 0);
			}

			postReset();

			m_initialized = true;
		}

		~RendererContextD3D9()
		{
			preReset();

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_indexBuffers); ++ii)
			{
				m_indexBuffers[ii].destroy();
			}

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_vertexBuffers); ++ii)
			{
				m_vertexBuffers[ii].destroy();
			}

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_shaders); ++ii)
			{
				m_shaders[ii].destroy();
			}

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_textures); ++ii)
			{
				m_textures[ii].destroy();
			}

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_vertexDecls); ++ii)
			{
				m_vertexDecls[ii].destroy();
			}

#if BGFX_CONFIG_RENDERER_DIRECT3D9EX
			if (NULL != m_d3d9ex)
			{
				DX_RELEASE(m_deviceEx, 1);
				DX_RELEASE(m_device, 0);
				DX_RELEASE(m_d3d9, 1);
				DX_RELEASE(m_d3d9ex, 0);
			}
			else
#endif // BGFX_CONFIG_RENDERER_DIRECT3D9EX
			{
				DX_RELEASE(m_device, 0);
				DX_RELEASE(m_d3d9, 0);
			}

#if BX_PLATFORM_WINDOWS
			bx::dlclose(m_d3d9dll);
#endif // BX_PLATFORM_WINDOWS

			m_initialized = false;
		}

		RendererType::Enum getRendererType() const BX_OVERRIDE
		{
			return RendererType::Direct3D9;
		}

		const char* getRendererName() const BX_OVERRIDE
		{
			return BGFX_RENDERER_DIRECT3D9_NAME;
		}

		void createIndexBuffer(IndexBufferHandle _handle, Memory* _mem) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].create(_mem->size, _mem->data);
		}

		void destroyIndexBuffer(IndexBufferHandle _handle) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].destroy();
		}

		void createVertexDecl(VertexDeclHandle _handle, const VertexDecl& _decl) BX_OVERRIDE
		{
			m_vertexDecls[_handle.idx].create(_decl);
		}

		void destroyVertexDecl(VertexDeclHandle _handle) BX_OVERRIDE
		{
			m_vertexDecls[_handle.idx].destroy();
		}

		void createVertexBuffer(VertexBufferHandle _handle, Memory* _mem, VertexDeclHandle _declHandle) BX_OVERRIDE
		{
			m_vertexBuffers[_handle.idx].create(_mem->size, _mem->data, _declHandle);
		}

		void destroyVertexBuffer(VertexBufferHandle _handle) BX_OVERRIDE
		{
			m_vertexBuffers[_handle.idx].destroy();
		}

		void createDynamicIndexBuffer(IndexBufferHandle _handle, uint32_t _size) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].create(_size, NULL);
		}

		void updateDynamicIndexBuffer(IndexBufferHandle _handle, uint32_t _offset, uint32_t _size, Memory* _mem) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].update(_offset, bx::uint32_min(_size, _mem->size), _mem->data);
		}

		void destroyDynamicIndexBuffer(IndexBufferHandle _handle) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].destroy();
		}

		void createDynamicVertexBuffer(VertexBufferHandle _handle, uint32_t _size) BX_OVERRIDE
		{
			VertexDeclHandle decl = BGFX_INVALID_HANDLE;
			m_vertexBuffers[_handle.idx].create(_size, NULL, decl);
		}

		void updateDynamicVertexBuffer(VertexBufferHandle _handle, uint32_t _offset, uint32_t _size, Memory* _mem) BX_OVERRIDE
		{
			m_vertexBuffers[_handle.idx].update(_offset, bx::uint32_min(_size, _mem->size), _mem->data);
		}

		void destroyDynamicVertexBuffer(VertexBufferHandle _handle) BX_OVERRIDE
		{
			m_vertexBuffers[_handle.idx].destroy();
		}

		void createShader(ShaderHandle _handle, Memory* _mem) BX_OVERRIDE
		{
			m_shaders[_handle.idx].create(_mem);
		}

		void destroyShader(ShaderHandle _handle) BX_OVERRIDE
		{
			m_shaders[_handle.idx].destroy();
		}

		void createProgram(ProgramHandle _handle, ShaderHandle _vsh, ShaderHandle _fsh) BX_OVERRIDE
		{
			m_program[_handle.idx].create(m_shaders[_vsh.idx], m_shaders[_fsh.idx]);
		}

		void destroyProgram(ProgramHandle _handle) BX_OVERRIDE
		{
			m_program[_handle.idx].destroy();
		}

		void createTexture(TextureHandle _handle, Memory* _mem, uint32_t _flags, uint8_t _skip) BX_OVERRIDE
		{
			m_textures[_handle.idx].create(_mem, _flags, _skip);
		}

		void updateTextureBegin(TextureHandle _handle, uint8_t _side, uint8_t _mip) BX_OVERRIDE
		{
			m_updateTexture = &m_textures[_handle.idx];
			m_updateTexture->updateBegin(_side, _mip);
		}

		void updateTexture(TextureHandle /*_handle*/, uint8_t _side, uint8_t _mip, const Rect& _rect, uint16_t _z, uint16_t _depth, uint16_t _pitch, const Memory* _mem) BX_OVERRIDE
		{
			m_updateTexture->update(_side, _mip, _rect, _z, _depth, _pitch, _mem);
		}

		void updateTextureEnd() BX_OVERRIDE
		{
			m_updateTexture->updateEnd();
			m_updateTexture = NULL;
		}

		void destroyTexture(TextureHandle _handle) BX_OVERRIDE
		{
			m_textures[_handle.idx].destroy();
		}

		void createFrameBuffer(FrameBufferHandle _handle, uint8_t _num, const TextureHandle* _textureHandles) BX_OVERRIDE
		{
			m_frameBuffers[_handle.idx].create(_num, _textureHandles);
		}

		void createFrameBuffer(FrameBufferHandle _handle, void* _nwh, uint32_t _width, uint32_t _height, TextureFormat::Enum _depthFormat) BX_OVERRIDE
		{
			uint16_t denseIdx = m_numWindows++;
			m_windows[denseIdx] = _handle;
			m_frameBuffers[_handle.idx].create(denseIdx, _nwh, _width, _height, _depthFormat);
		}

		void destroyFrameBuffer(FrameBufferHandle _handle) BX_OVERRIDE
		{
			uint16_t denseIdx = m_frameBuffers[_handle.idx].destroy();
			if (UINT16_MAX != denseIdx)
			{
				--m_numWindows;
				if (m_numWindows > 1)
				{
					FrameBufferHandle handle = m_windows[m_numWindows];
					m_windows[denseIdx] = handle;
					m_frameBuffers[handle.idx].m_denseIdx = denseIdx;
				}
			}
		}

		void createUniform(UniformHandle _handle, UniformType::Enum _type, uint16_t _num, const char* _name) BX_OVERRIDE
		{
			if (NULL != m_uniforms[_handle.idx])
			{
				BX_FREE(g_allocator, m_uniforms[_handle.idx]);
			}

			uint32_t size = BX_ALIGN_16(g_uniformTypeSize[_type]*_num);
			void* data = BX_ALLOC(g_allocator, size);
			memset(data, 0, size);
			m_uniforms[_handle.idx] = data;
			m_uniformReg.add(_handle, _name, data);
		}

		void destroyUniform(UniformHandle _handle) BX_OVERRIDE
		{
			BX_FREE(g_allocator, m_uniforms[_handle.idx]);
			m_uniforms[_handle.idx] = NULL;
		}

		void saveScreenShot(const char* _filePath) BX_OVERRIDE
		{
#if BX_PLATFORM_WINDOWS
			IDirect3DSurface9* surface;
			D3DDEVICE_CREATION_PARAMETERS dcp;
			DX_CHECK(m_device->GetCreationParameters(&dcp) );

			D3DDISPLAYMODE dm;
			DX_CHECK(m_d3d9->GetAdapterDisplayMode(dcp.AdapterOrdinal, &dm) );

			DX_CHECK(m_device->CreateOffscreenPlainSurface(dm.Width
				, dm.Height
				, D3DFMT_A8R8G8B8
				, D3DPOOL_SCRATCH
				, &surface
				, NULL
				) );

			DX_CHECK(m_device->GetFrontBufferData(0, surface) );

			D3DLOCKED_RECT rect;
			DX_CHECK(surface->LockRect(&rect
				, NULL
				, D3DLOCK_NO_DIRTY_UPDATE|D3DLOCK_NOSYSLOCK|D3DLOCK_READONLY
				) );

			RECT rc;
			GetClientRect(g_bgfxHwnd, &rc);
			POINT point;
			point.x = rc.left;
			point.y = rc.top;
			ClientToScreen(g_bgfxHwnd, &point);
			uint8_t* data = (uint8_t*)rect.pBits;
			uint32_t bytesPerPixel = rect.Pitch/dm.Width;

			g_callback->screenShot(_filePath
				, m_params.BackBufferWidth
				, m_params.BackBufferHeight
				, rect.Pitch
				, &data[point.y*rect.Pitch+point.x*bytesPerPixel]
				, m_params.BackBufferHeight*rect.Pitch
				, false
				);

			DX_CHECK(surface->UnlockRect() );
			DX_RELEASE(surface, 0);
#endif // BX_PLATFORM_WINDOWS
		}

		void updateViewName(uint8_t _id, const char* _name) BX_OVERRIDE
		{
			mbstowcs(&s_viewNameW[_id][0], _name, BX_COUNTOF(s_viewNameW[0]) );
		}

		void updateUniform(uint16_t _loc, const void* _data, uint32_t _size) BX_OVERRIDE
		{
			memcpy(m_uniforms[_loc], _data, _size);
		}

		void setMarker(const char* _marker, uint32_t _size) BX_OVERRIDE
		{
#if BGFX_CONFIG_DEBUG_PIX
			uint32_t size = _size*sizeof(wchar_t);
			wchar_t* name = (wchar_t*)alloca(size);
			mbstowcs(name, _marker, size-2);
			PIX_SETMARKER(D3DCOLOR_RGBA(0xff, 0xff, 0xff, 0xff), name);
#endif // BGFX_CONFIG_DEBUG_PIX
			BX_UNUSED(_marker, _size);
		}

		void submit(Frame* _render, ClearQuad& _clearQuad, TextVideoMemBlitter& _textVideoMemBlitter) BX_OVERRIDE;

		void blitSetup(TextVideoMemBlitter& _blitter) BX_OVERRIDE
		{
			uint32_t width  = m_params.BackBufferWidth;
			uint32_t height = m_params.BackBufferHeight;

			FrameBufferHandle fbh = BGFX_INVALID_HANDLE;
			setFrameBuffer(fbh, false);

			D3DVIEWPORT9 vp;
			vp.X = 0;
			vp.Y = 0;
			vp.Width = width;
			vp.Height = height;
			vp.MinZ = 0.0f;
			vp.MaxZ = 1.0f;

			IDirect3DDevice9* device = m_device;
			DX_CHECK(device->SetViewport(&vp) );
			DX_CHECK(device->SetRenderState(D3DRS_STENCILENABLE, FALSE) );
			DX_CHECK(device->SetRenderState(D3DRS_ZENABLE, FALSE) );
			DX_CHECK(device->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS) );
			DX_CHECK(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE) );
			DX_CHECK(device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE) );
			DX_CHECK(device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER) );
			DX_CHECK(device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE) );
			DX_CHECK(device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID) );

			ProgramD3D9& program = m_program[_blitter.m_program.idx];
			DX_CHECK(device->SetVertexShader(program.m_vsh->m_vertexShader) );
			DX_CHECK(device->SetPixelShader(program.m_fsh->m_pixelShader) );

			VertexBufferD3D9& vb = m_vertexBuffers[_blitter.m_vb->handle.idx];
			VertexDeclD3D9& vertexDecl = m_vertexDecls[_blitter.m_vb->decl.idx];
			DX_CHECK(device->SetStreamSource(0, vb.m_ptr, 0, vertexDecl.m_decl.m_stride) );
			DX_CHECK(device->SetVertexDeclaration(vertexDecl.m_ptr) );

			IndexBufferD3D9& ib = m_indexBuffers[_blitter.m_ib->handle.idx];
			DX_CHECK(device->SetIndices(ib.m_ptr) );

			float proj[16];
			mtxOrtho(proj, 0.0f, (float)width, (float)height, 0.0f, 0.0f, 1000.0f);

			PredefinedUniform& predefined = program.m_predefined[0];
			uint8_t flags = predefined.m_type;
			setShaderConstantF(flags, predefined.m_loc, proj, 4);

			m_textures[_blitter.m_texture.idx].commit(0);
		}

		void blitRender(TextVideoMemBlitter& _blitter, uint32_t _numIndices) BX_OVERRIDE
		{
			uint32_t numVertices = _numIndices*4/6;
			m_indexBuffers [_blitter.m_ib->handle.idx].update(0, _numIndices*2, _blitter.m_ib->data, true);
			m_vertexBuffers[_blitter.m_vb->handle.idx].update(0, numVertices*_blitter.m_decl.m_stride, _blitter.m_vb->data, true);

			DX_CHECK(m_device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST
				, 0
				, 0
				, numVertices
				, 0
				, _numIndices/3
				) );
		}

		void updateMsaa()
		{
			for (uint32_t ii = 1, last = 0; ii < BX_COUNTOF(s_checkMsaa); ++ii)
			{
				D3DMULTISAMPLE_TYPE msaa = s_checkMsaa[ii];
				DWORD quality;

				HRESULT hr = m_d3d9->CheckDeviceMultiSampleType(m_adapter
					, m_deviceType
					, m_params.BackBufferFormat
					, m_params.Windowed
					, msaa
					, &quality
					);

				if (SUCCEEDED(hr) )
				{
					s_msaa[ii].m_type = msaa;
					s_msaa[ii].m_quality = bx::uint32_imax(0, quality-1);
					last = ii;
				}
				else
				{
					s_msaa[ii] = s_msaa[last];
				}
			}
		}

		void updateResolution(const Resolution& _resolution)
		{
			if (m_params.BackBufferWidth  != _resolution.m_width
			||  m_params.BackBufferHeight != _resolution.m_height
			||  m_flags != _resolution.m_flags)
			{
				m_flags = _resolution.m_flags;

				m_textVideoMem.resize(false, _resolution.m_width, _resolution.m_height);
				m_textVideoMem.clear();

#if BX_PLATFORM_WINDOWS
				D3DDEVICE_CREATION_PARAMETERS dcp;
				DX_CHECK(m_device->GetCreationParameters(&dcp) );

				D3DDISPLAYMODE dm;
				DX_CHECK(m_d3d9->GetAdapterDisplayMode(dcp.AdapterOrdinal, &dm) );

				m_params.BackBufferFormat = dm.Format;
#endif // BX_PLATFORM_WINDOWS

				m_params.BackBufferWidth  = _resolution.m_width;
				m_params.BackBufferHeight = _resolution.m_height;
				m_params.FullScreen_RefreshRateInHz = BGFX_RESET_FULLSCREEN == (m_flags&BGFX_RESET_FULLSCREEN_MASK) ? 60 : 0;
				m_params.PresentationInterval = !!(m_flags&BGFX_RESET_VSYNC) ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;

				updateMsaa();

				Msaa& msaa = s_msaa[(m_flags&BGFX_RESET_MSAA_MASK)>>BGFX_RESET_MSAA_SHIFT];
				m_params.MultiSampleType    = msaa.m_type;
				m_params.MultiSampleQuality = msaa.m_quality;

				m_resolution = _resolution;

				preReset();
				DX_CHECK(m_device->Reset(&m_params) );
				postReset();
			}
		}

		void setFrameBuffer(FrameBufferHandle _fbh, bool _msaa = true)
		{
			if (isValid(m_fbh)
			&&  m_fbh.idx != _fbh.idx
			&&  m_rtMsaa)
			{
				FrameBufferD3D9& frameBuffer = m_frameBuffers[m_fbh.idx];
				frameBuffer.resolve();
			}

			if (!isValid(_fbh) )
			{
				DX_CHECK(m_device->SetRenderTarget(0, m_backBufferColor) );
				for (uint32_t ii = 1, num = g_caps.maxFBAttachments; ii < num; ++ii)
				{
					DX_CHECK(m_device->SetRenderTarget(ii, NULL) );
				}
				DX_CHECK(m_device->SetDepthStencilSurface(m_backBufferDepthStencil) );
			}
			else
			{
				const FrameBufferD3D9& frameBuffer = m_frameBuffers[_fbh.idx];

				// If frame buffer has only depth attachment D3DFMT_NULL
				// render target is created.
				uint32_t fbnum = bx::uint32_max(1, frameBuffer.m_num);

				for (uint32_t ii = 0; ii < fbnum; ++ii)
				{
					DX_CHECK(m_device->SetRenderTarget(ii, frameBuffer.m_color[ii]) );
				}

				for (uint32_t ii = fbnum, num = g_caps.maxFBAttachments; ii < num; ++ii)
				{
					DX_CHECK(m_device->SetRenderTarget(ii, NULL) );
				}

				IDirect3DSurface9* depthStencil = frameBuffer.m_depthStencil;
				DX_CHECK(m_device->SetDepthStencilSurface(NULL != depthStencil ? depthStencil : m_backBufferDepthStencil) );
			}

			m_fbh = _fbh;
			m_rtMsaa = _msaa;
		}

		void setShaderConstantF(uint8_t _flags, uint16_t _regIndex, const float* _val, uint16_t _numRegs)
		{
			if (_flags&BGFX_UNIFORM_FRAGMENTBIT)
			{
				DX_CHECK(m_device->SetPixelShaderConstantF(_regIndex, _val, _numRegs) );
			}
			else
			{
				DX_CHECK(m_device->SetVertexShaderConstantF(_regIndex, _val, _numRegs) );
			}
		}

		void reset()
		{
			preReset();

			HRESULT hr;

			do 
			{
				hr = m_device->Reset(&m_params);
			} while (FAILED(hr) );

			postReset();
		}

		static bool isLost(HRESULT _hr)
		{
			return D3DERR_DEVICELOST == _hr
				|| D3DERR_DRIVERINTERNALERROR == _hr
#if !defined(D3D_DISABLE_9EX)
				|| D3DERR_DEVICEHUNG == _hr
				|| D3DERR_DEVICEREMOVED == _hr
#endif // !defined(D3D_DISABLE_9EX)
				;
		}

		void flip() BX_OVERRIDE
		{
			if (NULL != m_device)
			{
#if BGFX_CONFIG_RENDERER_DIRECT3D9EX
				if (NULL != m_deviceEx)
				{
					DX_CHECK(m_deviceEx->WaitForVBlank(0) );
				}
#endif // BGFX_CONFIG_RENDERER_DIRECT3D9EX

				for (uint32_t ii = 0, num = m_numWindows; ii < num; ++ii)
				{
					HRESULT hr;
					if (0 == ii)
					{
						hr = m_swapChain->Present(NULL, NULL, g_bgfxHwnd, NULL, 0);
					}
					else
					{
						hr = m_frameBuffers[m_windows[ii].idx].present();
					}

#if BX_PLATFORM_WINDOWS
					if (isLost(hr) )
					{
						do
						{
							do 
							{
								hr = m_device->TestCooperativeLevel();
							}
							while (D3DERR_DEVICENOTRESET != hr);

							reset();
							hr = m_device->TestCooperativeLevel();
						}
						while (FAILED(hr) );

						break;
					}
					else if (FAILED(hr) )
					{
						BX_TRACE("Present failed with err 0x%08x.", hr);
					}
#endif // BX_PLATFORM_
				}
			}
		}

		void preReset()
		{
			invalidateSamplerState();

			for (uint32_t stage = 0; stage < BGFX_CONFIG_MAX_TEXTURE_SAMPLERS; ++stage)
			{
				DX_CHECK(m_device->SetTexture(stage, NULL) );
			}

			DX_CHECK(m_device->SetRenderTarget(0, m_backBufferColor) );
			for (uint32_t ii = 1, num = g_caps.maxFBAttachments; ii < num; ++ii)
			{
				DX_CHECK(m_device->SetRenderTarget(ii, NULL) );
			}
			DX_CHECK(m_device->SetDepthStencilSurface(m_backBufferDepthStencil) );
			DX_CHECK(m_device->SetVertexShader(NULL) );
			DX_CHECK(m_device->SetPixelShader(NULL) );
			DX_CHECK(m_device->SetStreamSource(0, NULL, 0, 0) );
			DX_CHECK(m_device->SetIndices(NULL) );

			DX_RELEASE(m_backBufferColor, 0);
			DX_RELEASE(m_backBufferDepthStencil, 0);
			DX_RELEASE(m_swapChain, 0);

			capturePreReset();

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_indexBuffers); ++ii)
			{
				m_indexBuffers[ii].preReset();
			}

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_vertexBuffers); ++ii)
			{
				m_vertexBuffers[ii].preReset();
			}

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_frameBuffers); ++ii)
			{
				m_frameBuffers[ii].preReset();
			}

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_textures); ++ii)
			{
				m_textures[ii].preReset();
			}
		}

		void postReset()
		{
			DX_CHECK(m_device->GetSwapChain(0, &m_swapChain) );
			DX_CHECK(m_swapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &m_backBufferColor) );
			DX_CHECK(m_device->GetDepthStencilSurface(&m_backBufferDepthStencil) );

			capturePostReset();

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_indexBuffers); ++ii)
			{
				m_indexBuffers[ii].postReset();
			}

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_vertexBuffers); ++ii)
			{
				m_vertexBuffers[ii].postReset();
			}

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_textures); ++ii)
			{
				m_textures[ii].postReset();
			}

			for (uint32_t ii = 0; ii < BX_COUNTOF(m_frameBuffers); ++ii)
			{
				m_frameBuffers[ii].postReset();
			}
		}

		void invalidateSamplerState()
		{
			for (uint32_t stage = 0; stage < BGFX_CONFIG_MAX_TEXTURE_SAMPLERS; ++stage)
			{
				m_samplerFlags[stage] = UINT32_MAX;
			}
		}

		void setSamplerState(uint8_t _stage, uint32_t _flags)
		{
			const uint32_t flags = _flags&( (~BGFX_TEXTURE_RESERVED_MASK) | BGFX_TEXTURE_SAMPLER_BITS_MASK);
			if (m_samplerFlags[_stage] != flags)
			{
				m_samplerFlags[_stage] = flags;
				IDirect3DDevice9* device = m_device;
				D3DTEXTUREADDRESS tau = s_textureAddress[(_flags&BGFX_TEXTURE_U_MASK)>>BGFX_TEXTURE_U_SHIFT];
				D3DTEXTUREADDRESS tav = s_textureAddress[(_flags&BGFX_TEXTURE_V_MASK)>>BGFX_TEXTURE_V_SHIFT];
				D3DTEXTUREADDRESS taw = s_textureAddress[(_flags&BGFX_TEXTURE_W_MASK)>>BGFX_TEXTURE_W_SHIFT];
				D3DTEXTUREFILTERTYPE minFilter = s_textureFilter[(_flags&BGFX_TEXTURE_MIN_MASK)>>BGFX_TEXTURE_MIN_SHIFT];
				D3DTEXTUREFILTERTYPE magFilter = s_textureFilter[(_flags&BGFX_TEXTURE_MAG_MASK)>>BGFX_TEXTURE_MAG_SHIFT];
				D3DTEXTUREFILTERTYPE mipFilter = s_textureFilter[(_flags&BGFX_TEXTURE_MIP_MASK)>>BGFX_TEXTURE_MIP_SHIFT];
				DX_CHECK(device->SetSamplerState(_stage, D3DSAMP_ADDRESSU, tau) );
				DX_CHECK(device->SetSamplerState(_stage, D3DSAMP_ADDRESSV, tav) );
				DX_CHECK(device->SetSamplerState(_stage, D3DSAMP_MINFILTER, minFilter) );
				DX_CHECK(device->SetSamplerState(_stage, D3DSAMP_MAGFILTER, magFilter) );
				DX_CHECK(device->SetSamplerState(_stage, D3DSAMP_MIPFILTER, mipFilter) );
				DX_CHECK(device->SetSamplerState(_stage, D3DSAMP_ADDRESSW, taw) );
			}
		}

		void capturePreReset()
		{
			if (NULL != m_captureSurface)
			{
				g_callback->captureEnd();
			}
			DX_RELEASE(m_captureSurface, 1);
			DX_RELEASE(m_captureTexture, 0);
			DX_RELEASE(m_captureResolve, 0);
		}

		void capturePostReset()
		{
			if (m_flags&BGFX_RESET_CAPTURE)
			{
				uint32_t width  = m_params.BackBufferWidth;
				uint32_t height = m_params.BackBufferHeight;
				D3DFORMAT fmt   = m_params.BackBufferFormat;

				DX_CHECK(m_device->CreateTexture(width
					, height
					, 1
					, 0
					, fmt
					, D3DPOOL_SYSTEMMEM
					, &m_captureTexture
					, NULL
					) );

				DX_CHECK(m_captureTexture->GetSurfaceLevel(0
					, &m_captureSurface
					) );

				if (m_params.MultiSampleType != D3DMULTISAMPLE_NONE)
				{
					DX_CHECK(m_device->CreateRenderTarget(width
						, height
						, fmt
						, D3DMULTISAMPLE_NONE
						, 0
						, false
						, &m_captureResolve
						, NULL
						) );
				}

				g_callback->captureBegin(width, height, width*4, TextureFormat::BGRA8, false);
			}
		}

		void capture()
		{
			if (NULL != m_captureSurface)
			{
				IDirect3DSurface9* resolve = m_backBufferColor;

				if (NULL != m_captureResolve)
				{
					resolve = m_captureResolve;
					DX_CHECK(m_device->StretchRect(m_backBufferColor
						, 0
						, m_captureResolve
						, NULL
						, D3DTEXF_NONE
						) );
				}

				HRESULT hr = m_device->GetRenderTargetData(resolve, m_captureSurface);
				if (SUCCEEDED(hr) )
				{
					D3DLOCKED_RECT rect;
					DX_CHECK(m_captureSurface->LockRect(&rect
						, NULL
						, D3DLOCK_NO_DIRTY_UPDATE|D3DLOCK_NOSYSLOCK|D3DLOCK_READONLY
						) );

					g_callback->captureFrame(rect.pBits, m_params.BackBufferHeight*rect.Pitch);

					DX_CHECK(m_captureSurface->UnlockRect() );
				}
			}
		}

		void commit(ConstantBuffer& _constantBuffer)
		{
			_constantBuffer.reset();

			IDirect3DDevice9* device = m_device;

			for (;;)
			{
				uint32_t opcode = _constantBuffer.read();

				if (UniformType::End == opcode)
				{
					break;
				}

				UniformType::Enum type;
				uint16_t loc;
				uint16_t num;
				uint16_t copy;
				ConstantBuffer::decodeOpcode(opcode, type, loc, num, copy);

				const char* data;
				if (copy)
				{
					data = _constantBuffer.read(g_uniformTypeSize[type]*num);
				}
				else
				{
					UniformHandle handle;
					memcpy(&handle, _constantBuffer.read(sizeof(UniformHandle) ), sizeof(UniformHandle) );
					data = (const char*)m_uniforms[handle.idx];
				}

#define CASE_IMPLEMENT_UNIFORM(_uniform, _dxsuffix, _type) \
				case UniformType::_uniform: \
				{ \
					_type* value = (_type*)data; \
					DX_CHECK(device->SetVertexShaderConstant##_dxsuffix(loc, value, num) ); \
				} \
				break; \
				\
				case UniformType::_uniform|BGFX_UNIFORM_FRAGMENTBIT: \
				{ \
					_type* value = (_type*)data; \
					DX_CHECK(device->SetPixelShaderConstant##_dxsuffix(loc, value, num) ); \
				} \
				break

				switch ( (int32_t)type)
				{
				case UniformType::Uniform3x3fv:
					{
						float* value = (float*)data;
						for (uint32_t ii = 0, count = num/3; ii < count; ++ii,  loc += 3, value += 9)
						{
							Matrix4 mtx;
							mtx.un.val[ 0] = value[0];
							mtx.un.val[ 1] = value[1];
							mtx.un.val[ 2] = value[2];
							mtx.un.val[ 3] = 0.0f;
							mtx.un.val[ 4] = value[3];
							mtx.un.val[ 5] = value[4];
							mtx.un.val[ 6] = value[5];
							mtx.un.val[ 7] = 0.0f;
							mtx.un.val[ 8] = value[6];
							mtx.un.val[ 9] = value[7];
							mtx.un.val[10] = value[8];
							mtx.un.val[11] = 0.0f;
							DX_CHECK(device->SetVertexShaderConstantF(loc, &mtx.un.val[0], 3) );
						}
					}
					break;

				case UniformType::Uniform3x3fv|BGFX_UNIFORM_FRAGMENTBIT:
					{
						float* value = (float*)data;
						for (uint32_t ii = 0, count = num/3; ii < count; ++ii, loc += 3, value += 9)
						{
							Matrix4 mtx;
							mtx.un.val[ 0] = value[0];
							mtx.un.val[ 1] = value[1];
							mtx.un.val[ 2] = value[2];
							mtx.un.val[ 3] = 0.0f;
							mtx.un.val[ 4] = value[3];
							mtx.un.val[ 5] = value[4];
							mtx.un.val[ 6] = value[5];
							mtx.un.val[ 7] = 0.0f;
							mtx.un.val[ 8] = value[6];
							mtx.un.val[ 9] = value[7];
							mtx.un.val[10] = value[8];
							mtx.un.val[11] = 0.0f;
							DX_CHECK(device->SetPixelShaderConstantF(loc, &mtx.un.val[0], 3) );
						}
					}
					break;

				CASE_IMPLEMENT_UNIFORM(Uniform1i,    I, int);
				CASE_IMPLEMENT_UNIFORM(Uniform1f,    F, float);
				CASE_IMPLEMENT_UNIFORM(Uniform1iv,   I, int);
				CASE_IMPLEMENT_UNIFORM(Uniform1fv,   F, float);
				CASE_IMPLEMENT_UNIFORM(Uniform2fv,   F, float);
				CASE_IMPLEMENT_UNIFORM(Uniform3fv,   F, float);
				CASE_IMPLEMENT_UNIFORM(Uniform4fv,   F, float);
				CASE_IMPLEMENT_UNIFORM(Uniform4x4fv, F, float);

				case UniformType::End:
					break;

				default:
					BX_TRACE("%4d: INVALID 0x%08x, t %d, l %d, n %d, c %d", _constantBuffer.getPos(), opcode, type, loc, num, copy);
					break;
				}

#undef CASE_IMPLEMENT_UNIFORM
			}
		}

		void clearQuad(ClearQuad& _clearQuad, const Rect& _rect, const Clear& _clear, const float _palette[][4])
		{
			IDirect3DDevice9* device = m_device;

			uint32_t numMrt = 1;
			FrameBufferHandle fbh = m_fbh;
			if (isValid(fbh) )
			{
				const FrameBufferD3D9& fb = m_frameBuffers[fbh.idx];
				numMrt = bx::uint32_max(1, fb.m_num);
			}

			if (1 == numMrt)
			{
				D3DCOLOR color = 0;
				DWORD flags    = 0;

				if (BGFX_CLEAR_COLOR_BIT & _clear.m_flags)
				{
					if (BGFX_CLEAR_COLOR_USE_PALETTE_BIT & _clear.m_flags)
					{
						uint8_t index = (uint8_t)bx::uint32_min(BGFX_CONFIG_MAX_CLEAR_COLOR_PALETTE-1, _clear.m_index[0]);
						const float* rgba = _palette[index];
						const float rr = rgba[0];
						const float gg = rgba[1];
						const float bb = rgba[2];
						const float aa = rgba[3];
						color = D3DCOLOR_COLORVALUE(rr, gg, bb, aa);
					}
					else
					{
						color = D3DCOLOR_RGBA(_clear.m_index[0], _clear.m_index[1], _clear.m_index[2], _clear.m_index[3]);
					}

					flags |= D3DCLEAR_TARGET;
					DX_CHECK(device->SetRenderState(D3DRS_COLORWRITEENABLE
						, D3DCOLORWRITEENABLE_RED
						| D3DCOLORWRITEENABLE_GREEN
						| D3DCOLORWRITEENABLE_BLUE
						| D3DCOLORWRITEENABLE_ALPHA
						) );
				}

				if (BGFX_CLEAR_DEPTH_BIT & _clear.m_flags)
				{
					flags |= D3DCLEAR_ZBUFFER;
					DX_CHECK(device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE) );
				}

				if (BGFX_CLEAR_STENCIL_BIT & _clear.m_flags)
				{
					flags |= D3DCLEAR_STENCIL;
				}

				if (0 != flags)
				{
					RECT rc;
					rc.left   = _rect.m_x;
					rc.top    = _rect.m_y;
					rc.right  = _rect.m_x + _rect.m_width;
					rc.bottom = _rect.m_y + _rect.m_height;
					DX_CHECK(device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE) );
					DX_CHECK(device->SetScissorRect(&rc) );
					DX_CHECK(device->Clear(0, NULL, flags, color, _clear.m_depth, _clear.m_stencil) );
					DX_CHECK(device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE) );
				}
			}
			else
			{
				DX_CHECK(device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE) );
				DX_CHECK(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE) );
				DX_CHECK(device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE) );

				if (BGFX_CLEAR_DEPTH_BIT & _clear.m_flags)
				{
					DX_CHECK(device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE) );
					DX_CHECK(device->SetRenderState(D3DRS_COLORWRITEENABLE
						, D3DCOLORWRITEENABLE_RED
						| D3DCOLORWRITEENABLE_GREEN
						| D3DCOLORWRITEENABLE_BLUE
						| D3DCOLORWRITEENABLE_ALPHA
						) );
					DX_CHECK(device->SetRenderState(D3DRS_ZENABLE, TRUE) );
					DX_CHECK(device->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS) );
				}
				else
				{
					DX_CHECK(device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE) );
					DX_CHECK(device->SetRenderState(D3DRS_COLORWRITEENABLE, 0) );
					DX_CHECK(device->SetRenderState(D3DRS_ZENABLE, FALSE) );
				}

				if (BGFX_CLEAR_STENCIL_BIT & _clear.m_flags)
				{
					DX_CHECK(device->SetRenderState(D3DRS_STENCILENABLE, TRUE) );
					DX_CHECK(device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, TRUE) );
					DX_CHECK(device->SetRenderState(D3DRS_STENCILREF, _clear.m_stencil) );
					DX_CHECK(device->SetRenderState(D3DRS_STENCILMASK, 0xff) );
					DX_CHECK(device->SetRenderState(D3DRS_STENCILFUNC,  D3DCMP_ALWAYS) );
					DX_CHECK(device->SetRenderState(D3DRS_STENCILFAIL,  D3DSTENCILOP_REPLACE) );
					DX_CHECK(device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_REPLACE) );
					DX_CHECK(device->SetRenderState(D3DRS_STENCILPASS,  D3DSTENCILOP_REPLACE) );
				}
				else
				{
					DX_CHECK(device->SetRenderState(D3DRS_STENCILENABLE, FALSE) );
				}

				VertexBufferD3D9& vb = m_vertexBuffers[_clearQuad.m_vb->handle.idx];
				VertexDeclD3D9& vertexDecl = m_vertexDecls[_clearQuad.m_vb->decl.idx];
				uint32_t stride = _clearQuad.m_decl.m_stride;

				{
					struct Vertex
					{
						float m_x;
						float m_y;
						float m_z;
					};

					Vertex* vertex = (Vertex*)_clearQuad.m_vb->data;
					BX_CHECK(stride == sizeof(Vertex), "Stride/Vertex mismatch (stride %d, sizeof(Vertex) %d)", stride, sizeof(Vertex) );

					const float depth = _clear.m_depth;

					vertex->m_x = -1.0f;
					vertex->m_y = -1.0f;
					vertex->m_z = depth;
					vertex++;
					vertex->m_x =  1.0f;
					vertex->m_y = -1.0f;
					vertex->m_z = depth;
					vertex++;
					vertex->m_x = -1.0f;
					vertex->m_y =  1.0f;
					vertex->m_z = depth;
					vertex++;
					vertex->m_x =  1.0f;
					vertex->m_y =  1.0f;
					vertex->m_z = depth;
				}

				vb.update(0, 4*stride, _clearQuad.m_vb->data);

				ProgramD3D9& program = m_program[_clearQuad.m_program[numMrt-1].idx];
				device->SetVertexShader(program.m_vsh->m_vertexShader);
				device->SetPixelShader(program.m_fsh->m_pixelShader);

				if (BGFX_CLEAR_COLOR_USE_PALETTE_BIT & _clear.m_flags)
				{
					float mrtClear[BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS][4];
					for (uint32_t ii = 0; ii < numMrt; ++ii)
					{
						uint8_t index = (uint8_t)bx::uint32_min(BGFX_CONFIG_MAX_CLEAR_COLOR_PALETTE-1, _clear.m_index[ii]);
						memcpy(mrtClear[ii], _palette[index], 16);
					}

					DX_CHECK(m_device->SetPixelShaderConstantF(0, mrtClear[0], numMrt) );
				}
				else
				{
					float rgba[4] =
					{
						_clear.m_index[0]*1.0f/255.0f,
						_clear.m_index[1]*1.0f/255.0f,
						_clear.m_index[2]*1.0f/255.0f,
						_clear.m_index[3]*1.0f/255.0f,
					};

					DX_CHECK(m_device->SetPixelShaderConstantF(0, rgba, 1) );
				}

				DX_CHECK(device->SetStreamSource(0, vb.m_ptr, 0, stride) );
				DX_CHECK(device->SetStreamSourceFreq(0, 1) );
				DX_CHECK(device->SetStreamSource(1, NULL, 0, 0) );
				DX_CHECK(device->SetVertexDeclaration(vertexDecl.m_ptr) );
				DX_CHECK(device->SetIndices(NULL) );
				DX_CHECK(device->DrawPrimitive(D3DPT_TRIANGLESTRIP
					, 0
					, 2
					) );
			}
		}

#if BX_PLATFORM_WINDOWS
		D3DCAPS9 m_caps;
#endif // BX_PLATFORM_WINDOWS

#if BGFX_CONFIG_RENDERER_DIRECT3D9EX
		IDirect3D9Ex* m_d3d9ex;
		IDirect3DDevice9Ex* m_deviceEx;
#endif // BGFX_CONFIG_RENDERER_DIRECT3D9EX

		IDirect3D9* m_d3d9;
		IDirect3DDevice9* m_device;
		D3DPOOL m_pool;

		IDirect3DSwapChain9* m_swapChain;
		uint16_t m_numWindows;
		FrameBufferHandle m_windows[BGFX_CONFIG_MAX_FRAME_BUFFERS];

		IDirect3DSurface9* m_backBufferColor;
		IDirect3DSurface9* m_backBufferDepthStencil;

		IDirect3DTexture9* m_captureTexture;
		IDirect3DSurface9* m_captureSurface;
		IDirect3DSurface9* m_captureResolve;

		IDirect3DVertexDeclaration9* m_instanceDataDecls[BGFX_CONFIG_MAX_INSTANCE_DATA_COUNT];

		void* m_d3d9dll;
		uint32_t m_adapter;
		D3DDEVTYPE m_deviceType;
		D3DPRESENT_PARAMETERS m_params;
		uint32_t m_flags;
		D3DADAPTER_IDENTIFIER9 m_identifier;
		Resolution m_resolution;

		bool m_initialized;
		bool m_amd;
		bool m_nvidia;
		bool m_instancing;

		D3DFORMAT m_fmtDepth;

		IndexBufferD3D9 m_indexBuffers[BGFX_CONFIG_MAX_INDEX_BUFFERS];
		VertexBufferD3D9 m_vertexBuffers[BGFX_CONFIG_MAX_VERTEX_BUFFERS];
		ShaderD3D9 m_shaders[BGFX_CONFIG_MAX_SHADERS];
		ProgramD3D9 m_program[BGFX_CONFIG_MAX_PROGRAMS];
		TextureD3D9 m_textures[BGFX_CONFIG_MAX_TEXTURES];
		VertexDeclD3D9 m_vertexDecls[BGFX_CONFIG_MAX_VERTEX_DECLS];
		FrameBufferD3D9 m_frameBuffers[BGFX_CONFIG_MAX_FRAME_BUFFERS];
		UniformRegistry m_uniformReg;
		void* m_uniforms[BGFX_CONFIG_MAX_UNIFORMS];

		uint32_t m_samplerFlags[BGFX_CONFIG_MAX_TEXTURE_SAMPLERS];

		TextureD3D9* m_updateTexture;
		uint8_t* m_updateTextureBits;
		uint32_t m_updateTexturePitch;
		uint8_t m_updateTextureSide;
		uint8_t m_updateTextureMip;

		TextVideoMem m_textVideoMem;

		FrameBufferHandle m_fbh;
		bool m_rtMsaa;
	};

	static RendererContextD3D9* s_renderD3D9;

	RendererContextI* rendererCreateD3D9()
	{
		s_renderD3D9 = BX_NEW(g_allocator, RendererContextD3D9);
		return s_renderD3D9;
	}

	void rendererDestroyD3D9()
	{
		BX_DELETE(g_allocator, s_renderD3D9);
		s_renderD3D9 = NULL;
	}

	void IndexBufferD3D9::create(uint32_t _size, void* _data)
	{
		m_size = _size;
		m_dynamic = NULL == _data;

		uint32_t usage = D3DUSAGE_WRITEONLY;
		D3DPOOL pool = s_renderD3D9->m_pool;

		if (m_dynamic)
		{
			usage |= D3DUSAGE_DYNAMIC;
			pool = D3DPOOL_DEFAULT;
		}

		DX_CHECK(s_renderD3D9->m_device->CreateIndexBuffer(m_size
			, usage
			, D3DFMT_INDEX16
			, pool
			, &m_ptr
			, NULL
			) );

		if (NULL != _data)
		{
			update(0, _size, _data);
		}
	}

	void IndexBufferD3D9::preReset()
	{
		if (m_dynamic)
		{
			DX_RELEASE(m_ptr, 0);
		}
	}

	void IndexBufferD3D9::postReset()
	{
		if (m_dynamic)
		{
			DX_CHECK(s_renderD3D9->m_device->CreateIndexBuffer(m_size
				, D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC
				, D3DFMT_INDEX16
				, D3DPOOL_DEFAULT
				, &m_ptr
				, NULL
				) );
		}
	}

	void VertexBufferD3D9::create(uint32_t _size, void* _data, VertexDeclHandle _declHandle)
	{
		m_size = _size;
		m_decl = _declHandle;
		m_dynamic = NULL == _data;

		uint32_t usage = D3DUSAGE_WRITEONLY;
		D3DPOOL pool = s_renderD3D9->m_pool;

		if (m_dynamic)
		{
			usage |= D3DUSAGE_DYNAMIC;
			pool = D3DPOOL_DEFAULT;
		}

		DX_CHECK(s_renderD3D9->m_device->CreateVertexBuffer(m_size
				, usage
				, 0
				, pool
				, &m_ptr
				, NULL 
				) );

		if (NULL != _data)
		{
			update(0, _size, _data);
		}
	}

	void VertexBufferD3D9::preReset()
	{
		if (m_dynamic)
		{
			DX_RELEASE(m_ptr, 0);
		}
	}

	void VertexBufferD3D9::postReset()
	{
		if (m_dynamic)
		{
			DX_CHECK(s_renderD3D9->m_device->CreateVertexBuffer(m_size
					, D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC
					, 0
					, D3DPOOL_DEFAULT
					, &m_ptr
					, NULL 
					) );
		}
	}

	static const D3DVERTEXELEMENT9 s_attrib[] =
	{
		{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0 },
		{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0 },
		{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      0 },
		{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BINORMAL,     0 },
		{ 0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,        0 },
		{ 0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,        1 },
		{ 0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
		{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0 },
		{ 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0 },
		{ 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     1 },
		{ 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     2 },
		{ 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     3 },
		{ 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     4 },
		{ 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     5 },
		{ 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     6 },
		{ 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     7 },
		D3DDECL_END()
	};
	BX_STATIC_ASSERT(Attrib::Count == BX_COUNTOF(s_attrib)-1);

	static const D3DDECLTYPE s_attribType[][4][2] =
	{
		{
			{ D3DDECLTYPE_UBYTE4,    D3DDECLTYPE_UBYTE4N   },
			{ D3DDECLTYPE_UBYTE4,    D3DDECLTYPE_UBYTE4N   },
			{ D3DDECLTYPE_UBYTE4,    D3DDECLTYPE_UBYTE4N   },
			{ D3DDECLTYPE_UBYTE4,    D3DDECLTYPE_UBYTE4N   },
		},
		{
			{ D3DDECLTYPE_SHORT2,    D3DDECLTYPE_SHORT2N   },
			{ D3DDECLTYPE_SHORT2,    D3DDECLTYPE_SHORT2N   },
			{ D3DDECLTYPE_SHORT4,    D3DDECLTYPE_SHORT4N   },
			{ D3DDECLTYPE_SHORT4,    D3DDECLTYPE_SHORT4N   },
		},
		{
			{ D3DDECLTYPE_FLOAT16_2, D3DDECLTYPE_FLOAT16_2 },
			{ D3DDECLTYPE_FLOAT16_2, D3DDECLTYPE_FLOAT16_2 },
			{ D3DDECLTYPE_FLOAT16_4, D3DDECLTYPE_FLOAT16_4 },
			{ D3DDECLTYPE_FLOAT16_4, D3DDECLTYPE_FLOAT16_4 },
		},
		{
			{ D3DDECLTYPE_FLOAT1,    D3DDECLTYPE_FLOAT1    },
			{ D3DDECLTYPE_FLOAT2,    D3DDECLTYPE_FLOAT2    },
			{ D3DDECLTYPE_FLOAT3,    D3DDECLTYPE_FLOAT3    },
			{ D3DDECLTYPE_FLOAT4,    D3DDECLTYPE_FLOAT4    },
		},
	};
	BX_STATIC_ASSERT(AttribType::Count == BX_COUNTOF(s_attribType) );

	static D3DVERTEXELEMENT9* fillVertexDecl(D3DVERTEXELEMENT9* _out, const VertexDecl& _decl)
	{
		D3DVERTEXELEMENT9* elem = _out;

		for (uint32_t attr = 0; attr < Attrib::Count; ++attr)
		{
			if (0xff != _decl.m_attributes[attr])
			{
				uint8_t num;
				AttribType::Enum type;
				bool normalized;
				bool asInt;
				_decl.decode(Attrib::Enum(attr), num, type, normalized, asInt);

				memcpy(elem, &s_attrib[attr], sizeof(D3DVERTEXELEMENT9) );

				elem->Type = s_attribType[type][num-1][normalized];
				elem->Offset = _decl.m_offset[attr];
				++elem;
			}
		}

		return elem;
	}

	static IDirect3DVertexDeclaration9* createVertexDeclaration(const VertexDecl& _decl, uint8_t _numInstanceData)
	{
		D3DVERTEXELEMENT9 vertexElements[Attrib::Count+1+BGFX_CONFIG_MAX_INSTANCE_DATA_COUNT];
		D3DVERTEXELEMENT9* elem = fillVertexDecl(vertexElements, _decl);

		const D3DVERTEXELEMENT9 inst = { 1, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 };

		for (uint32_t ii = 0; ii < _numInstanceData; ++ii)
		{
			memcpy(elem, &inst, sizeof(D3DVERTEXELEMENT9) );
			elem->UsageIndex = 8-_numInstanceData+ii;
			elem->Offset = ii*16;
			++elem;
		}

		memcpy(elem, &s_attrib[Attrib::Count], sizeof(D3DVERTEXELEMENT9) );

		IDirect3DVertexDeclaration9* ptr;
		DX_CHECK(s_renderD3D9->m_device->CreateVertexDeclaration(vertexElements, &ptr) );
		return ptr;
	}

	void VertexDeclD3D9::create(const VertexDecl& _decl)
	{
		memcpy(&m_decl, &_decl, sizeof(VertexDecl) );
		dump(m_decl);
		m_ptr = createVertexDeclaration(_decl, 0);
	}

	void ShaderD3D9::create(const Memory* _mem)
	{
		bx::MemoryReader reader(_mem->data, _mem->size);

		uint32_t magic;
		bx::read(&reader, magic);

		switch (magic)
		{
		case BGFX_CHUNK_MAGIC_FSH:
		case BGFX_CHUNK_MAGIC_VSH:
			break;

		default:
			BGFX_FATAL(false, Fatal::InvalidShader, "Unknown shader format %x.", magic);
			break;
		}

		bool fragment = BGFX_CHUNK_MAGIC_FSH == magic;

		uint32_t iohash;
		bx::read(&reader, iohash);

		uint16_t count;
		bx::read(&reader, count);

		m_numPredefined = 0;

		BX_TRACE("Shader consts %d", count);

		uint8_t fragmentBit = fragment ? BGFX_UNIFORM_FRAGMENTBIT : 0;

		if (0 < count)
		{
			for (uint32_t ii = 0; ii < count; ++ii)
			{
				uint8_t nameSize;
				bx::read(&reader, nameSize);

				char name[256];
				bx::read(&reader, &name, nameSize);
				name[nameSize] = '\0';

				uint8_t type;
				bx::read(&reader, type);

				uint8_t num;
				bx::read(&reader, num);

				uint16_t regIndex;
				bx::read(&reader, regIndex);

				uint16_t regCount;
				bx::read(&reader, regCount);

				const char* kind = "invalid";

				PredefinedUniform::Enum predefined = nameToPredefinedUniformEnum(name);
				if (PredefinedUniform::Count != predefined)
				{
					kind = "predefined";
					m_predefined[m_numPredefined].m_loc = regIndex;
					m_predefined[m_numPredefined].m_count = regCount;
					m_predefined[m_numPredefined].m_type = predefined|fragmentBit;
					m_numPredefined++;
				}
				else
				{
					const UniformInfo* info = s_renderD3D9->m_uniformReg.find(name);
					BX_CHECK(NULL != info, "User defined uniform '%s' is not found, it won't be set.", name);
					if (NULL != info)
					{
						if (NULL == m_constantBuffer)
						{
							m_constantBuffer = ConstantBuffer::create(1024);
						}

						kind = "user";
						m_constantBuffer->writeUniformHandle( (UniformType::Enum)(type|fragmentBit), regIndex, info->m_handle, regCount);
					}
				}

				BX_TRACE("\t%s: %s, type %2d, num %2d, r.index %3d, r.count %2d"
					, kind
					, name
					, type
					, num
					, regIndex
					, regCount
					);
				BX_UNUSED(kind);
			}

			if (NULL != m_constantBuffer)
			{
				m_constantBuffer->finish();
			}
		}

		uint16_t shaderSize;
		bx::read(&reader, shaderSize);

		const DWORD* code = (const DWORD*)reader.getDataPtr();

		if (fragment)
		{
			m_type = 1;
			DX_CHECK(s_renderD3D9->m_device->CreatePixelShader(code, &m_pixelShader) );
			BGFX_FATAL(NULL != m_pixelShader, bgfx::Fatal::InvalidShader, "Failed to create fragment shader.");
		}
		else
		{
			m_type = 0;
			DX_CHECK(s_renderD3D9->m_device->CreateVertexShader(code, &m_vertexShader) );
			BGFX_FATAL(NULL != m_vertexShader, bgfx::Fatal::InvalidShader, "Failed to create vertex shader.");
		}
	}

	void TextureD3D9::createTexture(uint32_t _width, uint32_t _height, uint8_t _numMips)
	{
		m_width = (uint16_t)_width;
		m_height = (uint16_t)_height;
		m_numMips = _numMips;
		m_type = Texture2D;
		const TextureFormat::Enum fmt = (TextureFormat::Enum)m_textureFormat;

		DWORD usage = 0;
		D3DPOOL pool = s_renderD3D9->m_pool;

		const bool renderTarget = 0 != (m_flags&BGFX_TEXTURE_RT_MASK);
		if (isDepth(fmt) )
		{
			usage = D3DUSAGE_DEPTHSTENCIL;
			pool  = D3DPOOL_DEFAULT;
		}
		else if (renderTarget)
		{
			usage = D3DUSAGE_RENDERTARGET;
			pool  = D3DPOOL_DEFAULT;
		}

		if (renderTarget)
		{
			uint32_t msaaQuality = ( (m_flags&BGFX_TEXTURE_RT_MSAA_MASK)>>BGFX_TEXTURE_RT_MSAA_SHIFT);
			msaaQuality = bx::uint32_satsub(msaaQuality, 1);

			bool bufferOnly = 0 != (m_flags&BGFX_TEXTURE_RT_BUFFER_ONLY);

			if (0 != msaaQuality
			||  bufferOnly)
			{
				const Msaa& msaa = s_msaa[msaaQuality];

				if (isDepth(fmt) )
				{
					DX_CHECK(s_renderD3D9->m_device->CreateDepthStencilSurface(
						  m_width
						, m_height
						, s_textureFormat[m_textureFormat].m_fmt
						, msaa.m_type
						, msaa.m_quality
						, FALSE
						, &m_surface
						, NULL
						) );
				}
				else
				{
					DX_CHECK(s_renderD3D9->m_device->CreateRenderTarget(
						  m_width
						, m_height
						, s_textureFormat[m_textureFormat].m_fmt
						, msaa.m_type
						, msaa.m_quality
						, FALSE
						, &m_surface
						, NULL
						) );
				}

				if (bufferOnly)
				{
					// This is render buffer, there is no sampling, no need
					// to create texture.
					return;
				}
			}
		}

		DX_CHECK(s_renderD3D9->m_device->CreateTexture(_width
			, _height
			, _numMips
			, usage
			, s_textureFormat[fmt].m_fmt
			, pool
			, &m_texture2d
			, NULL
			) );

		BGFX_FATAL(NULL != m_texture2d, Fatal::UnableToCreateTexture, "Failed to create texture (size: %dx%d, mips: %d, fmt: %d)."
			, _width
			, _height
			, _numMips
			, getName(fmt)
			);
	}

	void TextureD3D9::createVolumeTexture(uint32_t _width, uint32_t _height, uint32_t _depth, uint32_t _numMips)
	{
		m_type = Texture3D;
		const TextureFormat::Enum fmt = (TextureFormat::Enum)m_textureFormat;

		DX_CHECK(s_renderD3D9->m_device->CreateVolumeTexture(_width
			, _height
			, _depth
			, _numMips
			, 0
			, s_textureFormat[fmt].m_fmt
			, s_renderD3D9->m_pool
			, &m_texture3d
			, NULL
			) );

		BGFX_FATAL(NULL != m_texture3d, Fatal::UnableToCreateTexture, "Failed to create volume texture (size: %dx%dx%d, mips: %d, fmt: %s)."
			, _width
			, _height
			, _depth
			, _numMips
			, getName(fmt)
			);
	}

	void TextureD3D9::createCubeTexture(uint32_t _edge, uint32_t _numMips)
	{
		m_type = TextureCube;
		const TextureFormat::Enum fmt = (TextureFormat::Enum)m_textureFormat;

		DX_CHECK(s_renderD3D9->m_device->CreateCubeTexture(_edge
			, _numMips
			, 0
			, s_textureFormat[fmt].m_fmt
			, s_renderD3D9->m_pool
			, &m_textureCube
			, NULL
			) );

		BGFX_FATAL(NULL != m_textureCube, Fatal::UnableToCreateTexture, "Failed to create cube texture (edge: %d, mips: %d, fmt: %s)."
			, _edge
			, _numMips
			, getName(fmt)
			);
	}

	uint8_t* TextureD3D9::lock(uint8_t _side, uint8_t _lod, uint32_t& _pitch, uint32_t& _slicePitch, const Rect* _rect)
	{
		switch (m_type)
		{
		case Texture2D:
			{
				D3DLOCKED_RECT lockedRect;

				if (NULL != _rect)
				{
					RECT rect;
					rect.left = _rect->m_x;
					rect.top = _rect->m_y;
					rect.right = rect.left + _rect->m_width;
					rect.bottom = rect.top + _rect->m_height;
					DX_CHECK(m_texture2d->LockRect(_lod, &lockedRect, &rect, 0) );
				}
				else
				{
					DX_CHECK(m_texture2d->LockRect(_lod, &lockedRect, NULL, 0) );
				}

				_pitch = lockedRect.Pitch;
				_slicePitch = 0;
				return (uint8_t*)lockedRect.pBits;
			}

		case Texture3D:
			{
				D3DLOCKED_BOX box;
				DX_CHECK(m_texture3d->LockBox(_lod, &box, NULL, 0) );
				_pitch = box.RowPitch;
				_slicePitch = box.SlicePitch;
				return (uint8_t*)box.pBits;
			}

		case TextureCube:
			{
				D3DLOCKED_RECT lockedRect;

				if (NULL != _rect)
				{
					RECT rect;
					rect.left = _rect->m_x;
					rect.top = _rect->m_y;
					rect.right = rect.left + _rect->m_width;
					rect.bottom = rect.top + _rect->m_height;
					DX_CHECK(m_textureCube->LockRect(D3DCUBEMAP_FACES(_side), _lod, &lockedRect, &rect, 0) );
				}
				else
				{
					DX_CHECK(m_textureCube->LockRect(D3DCUBEMAP_FACES(_side), _lod, &lockedRect, NULL, 0) );
				}

				_pitch = lockedRect.Pitch;
				_slicePitch = 0;
				return (uint8_t*)lockedRect.pBits;
			}
		}

		BX_CHECK(false, "You should not be here.");
		_pitch = 0;
		_slicePitch = 0;
		return NULL;
	}

	void TextureD3D9::unlock(uint8_t _side, uint8_t _lod)
	{
		switch (m_type)
		{
		case Texture2D:
			{
				DX_CHECK(m_texture2d->UnlockRect(_lod) );
			}
			return;

		case Texture3D:
			{
				DX_CHECK(m_texture3d->UnlockBox(_lod) );
			}
			return;

		case TextureCube:
			{
				DX_CHECK(m_textureCube->UnlockRect(D3DCUBEMAP_FACES(_side), _lod) );
			}
			return;
		}

		BX_CHECK(false, "You should not be here.");
	}

	void TextureD3D9::dirty(uint8_t _side, const Rect& _rect, uint16_t _z, uint16_t _depth)
	{
		switch (m_type)
		{
		case Texture2D:
			{
				RECT rect;
				rect.left = _rect.m_x;
				rect.top = _rect.m_y;
				rect.right = rect.left + _rect.m_width;
				rect.bottom = rect.top + _rect.m_height;
				DX_CHECK(m_texture2d->AddDirtyRect(&rect) );
			}
			return;

		case Texture3D:
			{
				D3DBOX box;
				box.Left = _rect.m_x;
				box.Top = _rect.m_y;
				box.Right = box.Left + _rect.m_width;
				box.Bottom = box.Top + _rect.m_height;
				box.Front = _z;
				box.Back = box.Front + _depth;
				DX_CHECK(m_texture3d->AddDirtyBox(&box) );
			}
			return;

		case TextureCube:
			{
				RECT rect;
				rect.left = _rect.m_x;
				rect.top = _rect.m_y;
				rect.right = rect.left + _rect.m_width;
				rect.bottom = rect.top + _rect.m_height;
				DX_CHECK(m_textureCube->AddDirtyRect(D3DCUBEMAP_FACES(_side), &rect) );
			}
			return;
		}

		BX_CHECK(false, "You should not be here.");
	}

	void TextureD3D9::create(const Memory* _mem, uint32_t _flags, uint8_t _skip)
	{
		m_flags = _flags;

		ImageContainer imageContainer;

		if (imageParse(imageContainer, _mem->data, _mem->size) )
		{
			uint8_t numMips = imageContainer.m_numMips;
			const uint32_t startLod = bx::uint32_min(_skip, numMips-1);
			numMips -= startLod;
			const ImageBlockInfo& blockInfo = getBlockInfo(TextureFormat::Enum(imageContainer.m_format) );
			const uint32_t textureWidth  = bx::uint32_max(blockInfo.blockWidth,  imageContainer.m_width >>startLod);
			const uint32_t textureHeight = bx::uint32_max(blockInfo.blockHeight, imageContainer.m_height>>startLod);

			m_requestedFormat = imageContainer.m_format;
			m_textureFormat   = imageContainer.m_format;

			const TextureFormatInfo& tfi = s_textureFormat[m_requestedFormat];
			const bool convert = D3DFMT_UNKNOWN == tfi.m_fmt;

			uint8_t bpp = getBitsPerPixel(TextureFormat::Enum(m_textureFormat) );
			if (convert)
			{
				m_textureFormat = (uint8_t)TextureFormat::BGRA8;
				bpp = 32;
			}

			if (imageContainer.m_cubeMap)
			{
				createCubeTexture(textureWidth, numMips);
			}
			else if (imageContainer.m_depth > 1)
			{
				createVolumeTexture(textureWidth, textureHeight, imageContainer.m_depth, numMips);
			}
			else
			{
				createTexture(textureWidth, textureHeight, numMips);
			}

			BX_TRACE("Texture %3d: %s (requested: %s), %dx%d%s%s."
				, this - s_renderD3D9->m_textures
				, getName( (TextureFormat::Enum)m_textureFormat)
				, getName( (TextureFormat::Enum)m_requestedFormat)
				, textureWidth
				, textureHeight
				, imageContainer.m_cubeMap ? "x6" : ""
				, 0 != (m_flags&BGFX_TEXTURE_RT_MASK) ? " (render target)" : ""
				);

			if (0 != (_flags&BGFX_TEXTURE_RT_BUFFER_ONLY) )
			{
				return;
			}

			// For BC4 and B5 in DX9 LockRect returns wrong number of
			// bytes. If actual mip size is used it causes memory corruption.
			// http://www.aras-p.info/texts/D3D9GPUHacks.html#3dc
			const bool useMipSize = true
							&& imageContainer.m_format != TextureFormat::BC4
							&& imageContainer.m_format != TextureFormat::BC5
							;

			for (uint8_t side = 0, numSides = imageContainer.m_cubeMap ? 6 : 1; side < numSides; ++side)
			{
				uint32_t width     = textureWidth;
				uint32_t height    = textureHeight;
				uint32_t depth     = imageContainer.m_depth;
				uint32_t mipWidth  = imageContainer.m_width;
				uint32_t mipHeight = imageContainer.m_height;

				for (uint32_t lod = 0, num = numMips; lod < num; ++lod)
				{
					width     = bx::uint32_max(1, width);
					height    = bx::uint32_max(1, height);
					depth     = bx::uint32_max(1, depth);
					mipWidth  = bx::uint32_max(blockInfo.blockWidth,  mipWidth);
					mipHeight = bx::uint32_max(blockInfo.blockHeight, mipHeight);
					uint32_t mipSize = width*height*depth*bpp/8;

					ImageMip mip;
					if (imageGetRawData(imageContainer, side, lod+startLod, _mem->data, _mem->size, mip) )
					{
						uint32_t pitch;
						uint32_t slicePitch;
						uint8_t* bits = lock(side, lod, pitch, slicePitch);

						if (convert)
						{
							if (width  != mipWidth
							||  height != mipHeight)
							{
								uint32_t srcpitch = mipWidth*bpp/8;

								uint8_t* temp = (uint8_t*)BX_ALLOC(g_allocator, srcpitch*mipHeight);
								imageDecodeToBgra8(temp, mip.m_data, mip.m_width, mip.m_height, srcpitch, mip.m_format);

								uint32_t dstpitch = pitch;
								for (uint32_t yy = 0; yy < height; ++yy)
								{
									uint8_t* src = &temp[yy*srcpitch];
									uint8_t* dst = &bits[yy*dstpitch];
									memcpy(dst, src, dstpitch);
								}

								BX_FREE(g_allocator, temp);
							}
							else
							{
								imageDecodeToBgra8(bits, mip.m_data, mip.m_width, mip.m_height, pitch, mip.m_format);
							}
						}
						else
						{
							uint32_t size = useMipSize ? mip.m_size : mipSize;
							memcpy(bits, mip.m_data, size);
						}

						unlock(side, lod);
					}

					width     >>= 1;
					height    >>= 1;
					depth     >>= 1;
					mipWidth  >>= 1;
					mipHeight >>= 1;
				}
			}
		}
	}

	void TextureD3D9::updateBegin(uint8_t _side, uint8_t _mip)
	{
		uint32_t slicePitch;
		s_renderD3D9->m_updateTextureSide = _side;
		s_renderD3D9->m_updateTextureMip = _mip;
		s_renderD3D9->m_updateTextureBits = lock(_side, _mip, s_renderD3D9->m_updateTexturePitch, slicePitch);
	}

	void TextureD3D9::update(uint8_t _side, uint8_t _mip, const Rect& _rect, uint16_t _z, uint16_t _depth, uint16_t _pitch, const Memory* _mem)
	{
		const uint32_t bpp = getBitsPerPixel(TextureFormat::Enum(m_textureFormat) );
		const uint32_t rectpitch = _rect.m_width*bpp/8;
		const uint32_t srcpitch  = UINT16_MAX == _pitch ? rectpitch : _pitch;
		const uint32_t dstpitch  = s_renderD3D9->m_updateTexturePitch;
		uint8_t* bits = s_renderD3D9->m_updateTextureBits + _rect.m_y*dstpitch + _rect.m_x*bpp/8;

		const bool convert = m_textureFormat != m_requestedFormat;
		
		uint8_t* data = _mem->data;
		uint8_t* temp = NULL;

		if (convert)
		{
			uint8_t* temp = (uint8_t*)BX_ALLOC(g_allocator, rectpitch*_rect.m_height);
			imageDecodeToBgra8(temp, data, _rect.m_width, _rect.m_height, srcpitch, m_requestedFormat);
			data = temp;
		}

		{
			uint8_t* src = data;
			uint8_t* dst = bits;
			for (uint32_t yy = 0, height = _rect.m_height; yy < height; ++yy)
			{
				memcpy(dst, src, rectpitch);
				src += srcpitch;
				dst += dstpitch;
			}
		}

		if (NULL != temp)
		{
			BX_FREE(g_allocator, temp);
		}

		if (0 == _mip)
		{
			dirty(_side, _rect, _z, _depth);
		}
	}

	void TextureD3D9::updateEnd()
	{
		unlock(s_renderD3D9->m_updateTextureSide, s_renderD3D9->m_updateTextureMip);
	}

	void TextureD3D9::commit(uint8_t _stage, uint32_t _flags)
	{
		s_renderD3D9->setSamplerState(_stage, 0 == (BGFX_SAMPLER_DEFAULT_FLAGS & _flags) ? _flags : m_flags);
		DX_CHECK(s_renderD3D9->m_device->SetTexture(_stage, m_ptr) );
	}

	void TextureD3D9::resolve() const
	{
		if (NULL != m_surface
		&&  NULL != m_texture2d)
		{
			IDirect3DSurface9* surface;
			DX_CHECK(m_texture2d->GetSurfaceLevel(0, &surface) );
			DX_CHECK(s_renderD3D9->m_device->StretchRect(m_surface
				, NULL
				, surface
				, NULL
				, D3DTEXF_LINEAR
				) );
			DX_RELEASE(surface, 1);
		}
	}

	void TextureD3D9::preReset()
	{
		TextureFormat::Enum fmt = (TextureFormat::Enum)m_textureFormat;
		if (TextureFormat::Unknown != fmt
		&& (isDepth(fmt) || !!(m_flags&BGFX_TEXTURE_RT_MASK) ) )
		{
			DX_RELEASE(m_ptr, 0);
			DX_RELEASE(m_surface, 0);
		}
	}

	void TextureD3D9::postReset()
	{
		TextureFormat::Enum fmt = (TextureFormat::Enum)m_textureFormat;
		if (TextureFormat::Unknown != fmt
		&& (isDepth(fmt) || !!(m_flags&BGFX_TEXTURE_RT_MASK) ) )
		{
			createTexture(m_width, m_height, m_numMips);
		}
	}

	void FrameBufferD3D9::create(uint8_t _num, const TextureHandle* _handles)
	{
		for (uint32_t ii = 0; ii < BX_COUNTOF(m_color); ++ii)
		{
			m_color[ii] = NULL;
		}
		m_depthStencil = NULL;

		m_num = 0;
		m_needResolve = false;
		for (uint32_t ii = 0; ii < _num; ++ii)
		{
			TextureHandle handle = _handles[ii];
			if (isValid(handle) )
			{
				const TextureD3D9& texture = s_renderD3D9->m_textures[handle.idx];

				if (isDepth( (TextureFormat::Enum)texture.m_textureFormat) )
				{
					m_depthHandle = handle;
					if (NULL != texture.m_surface)
					{
						m_depthStencil = texture.m_surface;
						m_depthStencil->AddRef();
					}
					else
					{
						DX_CHECK(texture.m_texture2d->GetSurfaceLevel(0, &m_depthStencil) );
					}
				}
				else
				{
					m_colorHandle[m_num] = handle;
					if (NULL != texture.m_surface)
					{
						m_color[m_num] = texture.m_surface;
						m_color[m_num]->AddRef();
					}
					else
					{
						DX_CHECK(texture.m_texture2d->GetSurfaceLevel(0, &m_color[m_num]) );
					}
					m_num++;
				}

				m_needResolve |= (NULL != texture.m_surface) && (NULL != texture.m_texture2d);
			}
		}

		if (0 == m_num)
		{
			createNullColorRT();
		}
	}

	void FrameBufferD3D9::create(uint16_t _denseIdx, void* _nwh, uint32_t _width, uint32_t _height, TextureFormat::Enum _depthFormat)
	{
		BX_UNUSED(_depthFormat);

		m_hwnd = (HWND)_nwh;

		D3DPRESENT_PARAMETERS params;
		memcpy(&params, &s_renderD3D9->m_params, sizeof(D3DPRESENT_PARAMETERS) );
		params.BackBufferWidth  = bx::uint32_max(_width,  16);
		params.BackBufferHeight = bx::uint32_max(_height, 16);

		DX_CHECK(s_renderD3D9->m_device->CreateAdditionalSwapChain(&params, &m_swapChain) );
		DX_CHECK(m_swapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &m_color[0]) );

		DX_CHECK(s_renderD3D9->m_device->CreateDepthStencilSurface(
			  params.BackBufferWidth
			, params.BackBufferHeight
			, params.AutoDepthStencilFormat
			, params.MultiSampleType
			, params.MultiSampleQuality
			, FALSE
			, &m_depthStencil
			, NULL
			) );

		m_colorHandle[0].idx = invalidHandle;
		m_denseIdx = _denseIdx;
		m_num = 1;
		m_needResolve = false;
	}

	uint16_t FrameBufferD3D9::destroy()
	{
		if (NULL != m_hwnd)
		{
			DX_RELEASE(m_depthStencil, 0);
			DX_RELEASE(m_color[0],     0);
			DX_RELEASE(m_swapChain,    0);
		}
		else
		{
			for (uint32_t ii = 0, num = m_num; ii < num; ++ii)
			{
				m_colorHandle[ii].idx = invalidHandle;

				IDirect3DSurface9* ptr = m_color[ii];
				if (NULL != ptr)
				{
					ptr->Release();
					m_color[ii] = NULL;
				}
			}

			if (NULL != m_depthStencil)
			{
				if (0 == m_num)
				{
					IDirect3DSurface9* ptr = m_color[0];
					if (NULL != ptr)
					{
						ptr->Release();
						m_color[0] = NULL;
					}
				}

				m_depthStencil->Release();
				m_depthStencil = NULL;
			}
		}

		m_hwnd = NULL;
		m_num = 0;
		m_depthHandle.idx = invalidHandle;

		uint16_t denseIdx = m_denseIdx;
		m_denseIdx = UINT16_MAX;

		return denseIdx;
	}

	HRESULT FrameBufferD3D9::present()
	{
		return m_swapChain->Present(NULL, NULL, m_hwnd, NULL, 0);
	}

	void FrameBufferD3D9::resolve() const
	{
		if (m_needResolve)
		{
			if (isValid(m_depthHandle) )
			{
				const TextureD3D9& texture = s_renderD3D9->m_textures[m_depthHandle.idx];
				texture.resolve();
			}

			for (uint32_t ii = 0, num = m_num; ii < num; ++ii)
			{
				const TextureD3D9& texture = s_renderD3D9->m_textures[m_colorHandle[ii].idx];
				texture.resolve();
			}
		}
	}

	void FrameBufferD3D9::preReset()
	{
		if (NULL != m_hwnd)
		{
			DX_RELEASE(m_color[0],  0);
			DX_RELEASE(m_swapChain, 0);
		}
		else
		{
			for (uint32_t ii = 0, num = m_num; ii < num; ++ii)
			{
				m_color[ii]->Release();
				m_color[ii] = NULL;
			}

			if (isValid(m_depthHandle) )
			{
				if (0 == m_num)
				{
					m_color[0]->Release();
					m_color[0] = NULL;
				}

				m_depthStencil->Release();
				m_depthStencil = NULL;
			}
		}
	}

	void FrameBufferD3D9::postReset()
	{
		if (NULL != m_hwnd)
		{
			DX_CHECK(s_renderD3D9->m_device->CreateAdditionalSwapChain(&s_renderD3D9->m_params, &m_swapChain) );
			DX_CHECK(m_swapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &m_color[0]) );
		}
		else
		{
			for (uint32_t ii = 0, num = m_num; ii < num; ++ii)
			{
				TextureHandle th = m_colorHandle[ii];

				if (isValid(th) )
				{
					TextureD3D9& texture = s_renderD3D9->m_textures[th.idx];
					if (NULL != texture.m_surface)
					{
						m_color[ii] = texture.m_surface;
						m_color[ii]->AddRef();
					}
					else
					{
						DX_CHECK(texture.m_texture2d->GetSurfaceLevel(0, &m_color[ii]) );
					}
				}
			}

			if (isValid(m_depthHandle) )
			{
				TextureD3D9& texture = s_renderD3D9->m_textures[m_depthHandle.idx];
				if (NULL != texture.m_surface)
				{
					m_depthStencil = texture.m_surface;
					m_depthStencil->AddRef();
				}
				else
				{
					DX_CHECK(texture.m_texture2d->GetSurfaceLevel(0, &m_depthStencil) );
				}

				if (0 == m_num)
				{
					createNullColorRT();
				}
			}
		}
	}

	void FrameBufferD3D9::createNullColorRT()
	{
		const TextureD3D9& texture = s_renderD3D9->m_textures[m_depthHandle.idx];
		DX_CHECK(s_renderD3D9->m_device->CreateRenderTarget(texture.m_width
			, texture.m_height
			, D3DFMT_NULL
			, D3DMULTISAMPLE_NONE
			, 0
			, false
			, &m_color[0]
			, NULL
			) );
	}

	void RendererContextD3D9::submit(Frame* _render, ClearQuad& _clearQuad, TextVideoMemBlitter& _textVideoMemBlitter)
	{
		IDirect3DDevice9* device = m_device;

		PIX_BEGINEVENT(D3DCOLOR_RGBA(0xff, 0x00, 0x00, 0xff), L"rendererSubmit");

		updateResolution(_render->m_resolution);

		int64_t elapsed = -bx::getHPCounter();
		int64_t captureElapsed = 0;

		device->BeginScene();

		if (0 < _render->m_iboffset)
		{
			TransientIndexBuffer* ib = _render->m_transientIb;
			m_indexBuffers[ib->handle.idx].update(0, _render->m_iboffset, ib->data, true);
		}

		if (0 < _render->m_vboffset)
		{
			TransientVertexBuffer* vb = _render->m_transientVb;
			m_vertexBuffers[vb->handle.idx].update(0, _render->m_vboffset, vb->data, true);
		}

		_render->sort();

		RenderDraw currentState;
		currentState.clear();
		currentState.m_flags = BGFX_STATE_NONE;
		currentState.m_stencil = packStencil(BGFX_STENCIL_NONE, BGFX_STENCIL_NONE);

		Matrix4 viewProj[BGFX_CONFIG_MAX_VIEWS];
		for (uint32_t ii = 0; ii < BGFX_CONFIG_MAX_VIEWS; ++ii)
		{
			bx::float4x4_mul(&viewProj[ii].un.f4x4, &_render->m_view[ii].un.f4x4, &_render->m_proj[ii].un.f4x4);
		}

		Matrix4 invView;
		Matrix4 invProj;
		Matrix4 invViewProj;
		uint8_t invViewCached = 0xff;
		uint8_t invProjCached = 0xff;
		uint8_t invViewProjCached = 0xff;

		DX_CHECK(device->SetRenderState(D3DRS_FILLMODE, _render->m_debug&BGFX_DEBUG_WIREFRAME ? D3DFILL_WIREFRAME : D3DFILL_SOLID) );
		uint16_t programIdx = invalidHandle;
		SortKey key;
		uint8_t view = 0xff;
		FrameBufferHandle fbh = BGFX_INVALID_HANDLE;
		float alphaRef = 0.0f;
		uint32_t blendFactor = 0;

		const uint64_t pt = _render->m_debug&BGFX_DEBUG_WIREFRAME ? BGFX_STATE_PT_LINES : 0;
		uint8_t primIndex = uint8_t(pt>>BGFX_STATE_PT_SHIFT);
		PrimInfo prim = s_primInfo[primIndex];

		bool viewHasScissor = false;
		Rect viewScissorRect;
		viewScissorRect.clear();

		uint32_t statsNumPrimsSubmitted[BX_COUNTOF(s_primInfo)] = {};
		uint32_t statsNumPrimsRendered[BX_COUNTOF(s_primInfo)] = {};
		uint32_t statsNumInstances[BX_COUNTOF(s_primInfo)] = {};
		uint32_t statsNumIndices = 0;

		invalidateSamplerState();

		if (0 == (_render->m_debug&BGFX_DEBUG_IFH) )
		{
			for (uint32_t item = 0, numItems = _render->m_num; item < numItems; ++item)
			{
				const bool isCompute = key.decode(_render->m_sortKeys[item]);

				if (isCompute)
				{
					BX_CHECK(false, "Compute is not supported on DirectX 9.");
					continue;
				}

				const RenderDraw& draw = _render->m_renderItem[_render->m_sortValues[item] ].draw;

				const uint64_t newFlags = draw.m_flags;
				uint64_t changedFlags = currentState.m_flags ^ draw.m_flags;
				currentState.m_flags = newFlags;

				const uint64_t newStencil = draw.m_stencil;
				uint64_t changedStencil = currentState.m_stencil ^ draw.m_stencil;
				currentState.m_stencil = newStencil;

				if (key.m_view != view)
				{
					currentState.clear();
					currentState.m_scissor = !draw.m_scissor;
					changedFlags = BGFX_STATE_MASK;
					changedStencil = packStencil(BGFX_STENCIL_MASK, BGFX_STENCIL_MASK);
					currentState.m_flags = newFlags;
					currentState.m_stencil = newStencil;

					PIX_ENDEVENT();
					PIX_BEGINEVENT(D3DCOLOR_RGBA(0xff, 0x00, 0x00, 0xff), s_viewNameW[key.m_view]);

					view = key.m_view;
					programIdx = invalidHandle;

					if (_render->m_fb[view].idx != fbh.idx)
					{
						fbh = _render->m_fb[view];
						setFrameBuffer(fbh);
					}

					const Rect& rect = _render->m_rect[view];
					const Rect& scissorRect = _render->m_scissor[view];
					viewHasScissor = !scissorRect.isZero();
					viewScissorRect = viewHasScissor ? scissorRect : rect;

					D3DVIEWPORT9 vp;
					vp.X = rect.m_x;
					vp.Y = rect.m_y;
					vp.Width = rect.m_width;
					vp.Height = rect.m_height;
					vp.MinZ = 0.0f;
					vp.MaxZ = 1.0f;
					DX_CHECK(device->SetViewport(&vp) );

					Clear& clear = _render->m_clear[view];

					if (BGFX_CLEAR_NONE != clear.m_flags)
					{
						clearQuad(_clearQuad, rect, clear, _render->m_clearColor);
						prim = s_primInfo[BX_COUNTOF(s_primName)]; // Force primitive type update after clear quad.
					}

					DX_CHECK(device->SetRenderState(D3DRS_STENCILENABLE, FALSE) );
					DX_CHECK(device->SetRenderState(D3DRS_ZENABLE, TRUE) );
					DX_CHECK(device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS) );
					DX_CHECK(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE) );
					DX_CHECK(device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE) );
					DX_CHECK(device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER) );
				}

				uint16_t scissor = draw.m_scissor;
				if (currentState.m_scissor != scissor)
				{
					currentState.m_scissor = scissor;

					if (UINT16_MAX == scissor)
					{
						DX_CHECK(device->SetRenderState(D3DRS_SCISSORTESTENABLE, viewHasScissor) );
						if (viewHasScissor)
						{
							RECT rc;
							rc.left = viewScissorRect.m_x;
							rc.top = viewScissorRect.m_y;
							rc.right = viewScissorRect.m_x + viewScissorRect.m_width;
							rc.bottom = viewScissorRect.m_y + viewScissorRect.m_height;
							DX_CHECK(device->SetScissorRect(&rc) );
						}
					}
					else
					{
						Rect scissorRect;
						scissorRect.intersect(viewScissorRect, _render->m_rectCache.m_cache[scissor]);
						DX_CHECK(device->SetRenderState(D3DRS_SCISSORTESTENABLE, true) );
						RECT rc;
						rc.left = scissorRect.m_x;
						rc.top = scissorRect.m_y;
						rc.right = scissorRect.m_x + scissorRect.m_width;
						rc.bottom = scissorRect.m_y + scissorRect.m_height;
						DX_CHECK(device->SetScissorRect(&rc) );
					}
				}

				if (0 != changedStencil)
				{
					bool enable = 0 != newStencil;
					DX_CHECK(device->SetRenderState(D3DRS_STENCILENABLE, enable) );
					
					if (0 != newStencil)
					{
						uint32_t fstencil = unpackStencil(0, newStencil);
						uint32_t bstencil = unpackStencil(1, newStencil);
						uint32_t frontAndBack = bstencil != BGFX_STENCIL_NONE && bstencil != fstencil;
						DX_CHECK(device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, 0 != frontAndBack) );

						uint32_t fchanged = unpackStencil(0, changedStencil);
						if ( (BGFX_STENCIL_FUNC_REF_MASK|BGFX_STENCIL_FUNC_RMASK_MASK) & fchanged)
						{
							uint32_t ref = (fstencil&BGFX_STENCIL_FUNC_REF_MASK)>>BGFX_STENCIL_FUNC_REF_SHIFT;
							DX_CHECK(device->SetRenderState(D3DRS_STENCILREF, ref) );

							uint32_t rmask = (fstencil&BGFX_STENCIL_FUNC_RMASK_MASK)>>BGFX_STENCIL_FUNC_RMASK_SHIFT;
							DX_CHECK(device->SetRenderState(D3DRS_STENCILMASK, rmask) );
						}

// 						uint32_t bchanged = unpackStencil(1, changedStencil);
// 						if (BGFX_STENCIL_FUNC_RMASK_MASK & bchanged)
// 						{
// 							uint32_t wmask = (bstencil&BGFX_STENCIL_FUNC_RMASK_MASK)>>BGFX_STENCIL_FUNC_RMASK_SHIFT;
// 							DX_CHECK(device->SetRenderState(D3DRS_STENCILWRITEMASK, wmask) );
// 						}

						for (uint32_t ii = 0, num = frontAndBack+1; ii < num; ++ii)
						{
							uint32_t stencil = unpackStencil(ii, newStencil);
							uint32_t changed = unpackStencil(ii, changedStencil);

							if ( (BGFX_STENCIL_TEST_MASK|BGFX_STENCIL_FUNC_REF_MASK|BGFX_STENCIL_FUNC_RMASK_MASK) & changed)
							{
								uint32_t func = (stencil&BGFX_STENCIL_TEST_MASK)>>BGFX_STENCIL_TEST_SHIFT;
								DX_CHECK(device->SetRenderState(s_stencilFuncRs[ii], s_cmpFunc[func]) );
							}

							if ( (BGFX_STENCIL_OP_FAIL_S_MASK|BGFX_STENCIL_OP_FAIL_Z_MASK|BGFX_STENCIL_OP_PASS_Z_MASK) & changed)
							{
								uint32_t sfail = (stencil&BGFX_STENCIL_OP_FAIL_S_MASK)>>BGFX_STENCIL_OP_FAIL_S_SHIFT;
								DX_CHECK(device->SetRenderState(s_stencilFailRs[ii], s_stencilOp[sfail]) );

								uint32_t zfail = (stencil&BGFX_STENCIL_OP_FAIL_Z_MASK)>>BGFX_STENCIL_OP_FAIL_Z_SHIFT;
								DX_CHECK(device->SetRenderState(s_stencilZFailRs[ii], s_stencilOp[zfail]) );

								uint32_t zpass = (stencil&BGFX_STENCIL_OP_PASS_Z_MASK)>>BGFX_STENCIL_OP_PASS_Z_SHIFT;
								DX_CHECK(device->SetRenderState(s_stencilZPassRs[ii], s_stencilOp[zpass]) );
							}
						}
					}
				}

				if ( (0
					 | BGFX_STATE_CULL_MASK
					 | BGFX_STATE_DEPTH_WRITE
					 | BGFX_STATE_DEPTH_TEST_MASK
					 | BGFX_STATE_RGB_WRITE
					 | BGFX_STATE_ALPHA_WRITE
					 | BGFX_STATE_BLEND_MASK
					 | BGFX_STATE_BLEND_EQUATION_MASK
					 | BGFX_STATE_ALPHA_REF_MASK
					 | BGFX_STATE_PT_MASK
					 | BGFX_STATE_POINT_SIZE_MASK
					 | BGFX_STATE_MSAA
					 ) & changedFlags)
				{
					if (BGFX_STATE_CULL_MASK & changedFlags)
					{
						uint32_t cull = (newFlags&BGFX_STATE_CULL_MASK)>>BGFX_STATE_CULL_SHIFT;
						DX_CHECK(device->SetRenderState(D3DRS_CULLMODE, s_cullMode[cull]) );
					}

					if (BGFX_STATE_DEPTH_WRITE & changedFlags)
					{ 
						DX_CHECK(device->SetRenderState(D3DRS_ZWRITEENABLE, !!(BGFX_STATE_DEPTH_WRITE & newFlags) ) );
					}

					if (BGFX_STATE_DEPTH_TEST_MASK & changedFlags)
					{
						uint32_t func = (newFlags&BGFX_STATE_DEPTH_TEST_MASK)>>BGFX_STATE_DEPTH_TEST_SHIFT;
						DX_CHECK(device->SetRenderState(D3DRS_ZENABLE, 0 != func) );

						if (0 != func)
						{
							DX_CHECK(device->SetRenderState(D3DRS_ZFUNC, s_cmpFunc[func]) );
						}
					}

					if (BGFX_STATE_ALPHA_REF_MASK & changedFlags)
					{
						uint32_t ref = (newFlags&BGFX_STATE_ALPHA_REF_MASK)>>BGFX_STATE_ALPHA_REF_SHIFT;
						alphaRef = ref/255.0f;
					}

					if ( (BGFX_STATE_PT_POINTS|BGFX_STATE_POINT_SIZE_MASK) & changedFlags)
					{
						DX_CHECK(device->SetRenderState(D3DRS_POINTSIZE, castfu( (float)( (newFlags&BGFX_STATE_POINT_SIZE_MASK)>>BGFX_STATE_POINT_SIZE_SHIFT) ) ) );
					}

					if (BGFX_STATE_MSAA & changedFlags)
					{
						DX_CHECK(device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, (newFlags&BGFX_STATE_MSAA) == BGFX_STATE_MSAA) );
					}

					if ( (BGFX_STATE_ALPHA_WRITE|BGFX_STATE_RGB_WRITE) & changedFlags)
					{
						uint32_t writeEnable = (newFlags&BGFX_STATE_ALPHA_WRITE) ? D3DCOLORWRITEENABLE_ALPHA : 0;
 						writeEnable |= (newFlags&BGFX_STATE_RGB_WRITE) ? D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE : 0;
						DX_CHECK(device->SetRenderState(D3DRS_COLORWRITEENABLE, writeEnable) );
					}

					if ( (BGFX_STATE_BLEND_MASK|BGFX_STATE_BLEND_EQUATION_MASK) & changedFlags
					||  blendFactor != draw.m_rgba)
					{
						bool enabled = !!(BGFX_STATE_BLEND_MASK & newFlags);
						DX_CHECK(device->SetRenderState(D3DRS_ALPHABLENDENABLE, enabled) );

						if (enabled)
						{
							const uint32_t blend    = uint32_t( (newFlags&BGFX_STATE_BLEND_MASK)>>BGFX_STATE_BLEND_SHIFT);
							const uint32_t equation = uint32_t( (newFlags&BGFX_STATE_BLEND_EQUATION_MASK)>>BGFX_STATE_BLEND_EQUATION_SHIFT);

							const uint32_t srcRGB  = (blend    )&0xf;
							const uint32_t dstRGB  = (blend>> 4)&0xf;
							const uint32_t srcA    = (blend>> 8)&0xf;
							const uint32_t dstA    = (blend>>12)&0xf;

							const uint32_t equRGB = (equation   )&0x7;
							const uint32_t equA   = (equation>>3)&0x7;

 							DX_CHECK(device->SetRenderState(D3DRS_SRCBLEND,  s_blendFactor[srcRGB].m_src) );
							DX_CHECK(device->SetRenderState(D3DRS_DESTBLEND, s_blendFactor[dstRGB].m_dst) );
							DX_CHECK(device->SetRenderState(D3DRS_BLENDOP,   s_blendEquation[equRGB]) );

							const bool separate = srcRGB != srcA || dstRGB != dstA || equRGB != equA;

							DX_CHECK(device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, separate) );
							if (separate)
							{
								DX_CHECK(device->SetRenderState(D3DRS_SRCBLENDALPHA,  s_blendFactor[srcA].m_src) );
								DX_CHECK(device->SetRenderState(D3DRS_DESTBLENDALPHA, s_blendFactor[dstA].m_dst) );
								DX_CHECK(device->SetRenderState(D3DRS_BLENDOPALPHA,   s_blendEquation[equA]) );
							}

							if ( (s_blendFactor[srcRGB].m_factor || s_blendFactor[dstRGB].m_factor)
							&&  blendFactor != draw.m_rgba)
							{
								const uint32_t rgba = draw.m_rgba;
								D3DCOLOR color = D3DCOLOR_RGBA(rgba>>24
															, (rgba>>16)&0xff
															, (rgba>> 8)&0xff
															, (rgba    )&0xff
															);
								DX_CHECK(device->SetRenderState(D3DRS_BLENDFACTOR, color) );
							}
						}

						blendFactor = draw.m_rgba;
					}

					const uint64_t pt = _render->m_debug&BGFX_DEBUG_WIREFRAME ? BGFX_STATE_PT_LINES : newFlags&BGFX_STATE_PT_MASK;
					primIndex = uint8_t(pt>>BGFX_STATE_PT_SHIFT);
					prim = s_primInfo[primIndex];
				}

				bool programChanged = false;
				bool constantsChanged = draw.m_constBegin < draw.m_constEnd;
				rendererUpdateUniforms(this, _render->m_constantBuffer, draw.m_constBegin, draw.m_constEnd);

				if (key.m_program != programIdx)
				{
					programIdx = key.m_program;

					if (invalidHandle == programIdx)
					{
						device->SetVertexShader(NULL);
						device->SetPixelShader(NULL);
					}
					else
					{
						ProgramD3D9& program = m_program[programIdx];
						device->SetVertexShader(program.m_vsh->m_vertexShader);
						device->SetPixelShader(program.m_fsh->m_pixelShader);
					}

					programChanged = 
						constantsChanged = true;
				}

				if (invalidHandle != programIdx)
				{
					ProgramD3D9& program = m_program[programIdx];

					if (constantsChanged)
					{
						ConstantBuffer* vcb = program.m_vsh->m_constantBuffer;
						if (NULL != vcb)
						{
							commit(*vcb);
						}

						ConstantBuffer* fcb = program.m_fsh->m_constantBuffer;
						if (NULL != fcb)
						{
							commit(*fcb);
						}
					}

					for (uint32_t ii = 0, num = program.m_numPredefined; ii < num; ++ii)
					{
						PredefinedUniform& predefined = program.m_predefined[ii];
						uint8_t flags = predefined.m_type&BGFX_UNIFORM_FRAGMENTBIT;
						switch (predefined.m_type&(~BGFX_UNIFORM_FRAGMENTBIT) )
						{
						case PredefinedUniform::ViewRect:
							{
								float rect[4];
								rect[0] = _render->m_rect[view].m_x;
								rect[1] = _render->m_rect[view].m_y;
								rect[2] = _render->m_rect[view].m_width;
								rect[3] = _render->m_rect[view].m_height;

								setShaderConstantF(flags, predefined.m_loc, &rect[0], 1);
							}
							break;

						case PredefinedUniform::ViewTexel:
							{
								float rect[4];
								rect[0] = 1.0f/float(_render->m_rect[view].m_width);
								rect[1] = 1.0f/float(_render->m_rect[view].m_height);

								setShaderConstantF(flags, predefined.m_loc, &rect[0], 1);
							}
							break;

						case PredefinedUniform::View:
							{
								setShaderConstantF(flags, predefined.m_loc, _render->m_view[view].un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::InvView:
							{
								if (view != invViewCached)
								{
									invViewCached = view;
									bx::float4x4_inverse(&invView.un.f4x4, &_render->m_view[view].un.f4x4);
								}

								setShaderConstantF(flags, predefined.m_loc, invView.un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::Proj:
							{
								setShaderConstantF(flags, predefined.m_loc, _render->m_proj[view].un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::InvProj:
							{
								if (view != invProjCached)
								{
									invProjCached = view;
									bx::float4x4_inverse(&invProj.un.f4x4, &_render->m_proj[view].un.f4x4);
								}

								setShaderConstantF(flags, predefined.m_loc, invProj.un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::ViewProj:
							{
								setShaderConstantF(flags, predefined.m_loc, viewProj[view].un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::InvViewProj:
							{
								if (view != invViewProjCached)
								{
									invViewProjCached = view;
									bx::float4x4_inverse(&invViewProj.un.f4x4, &viewProj[view].un.f4x4);
								}

								setShaderConstantF(flags, predefined.m_loc, invViewProj.un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::Model:
							{
 								const Matrix4& model = _render->m_matrixCache.m_cache[draw.m_matrix];
								setShaderConstantF(flags, predefined.m_loc, model.un.val, bx::uint32_min(draw.m_num*4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::ModelView:
							{
								Matrix4 modelView;
								const Matrix4& model = _render->m_matrixCache.m_cache[draw.m_matrix];
								bx::float4x4_mul(&modelView.un.f4x4, &model.un.f4x4, &_render->m_view[view].un.f4x4);
								setShaderConstantF(flags, predefined.m_loc, modelView.un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::ModelViewProj:
							{
								Matrix4 modelViewProj;
								const Matrix4& model = _render->m_matrixCache.m_cache[draw.m_matrix];
								bx::float4x4_mul(&modelViewProj.un.f4x4, &model.un.f4x4, &viewProj[view].un.f4x4);
								setShaderConstantF(flags, predefined.m_loc, modelViewProj.un.val, bx::uint32_min(4, predefined.m_count) );
							}
							break;

						case PredefinedUniform::AlphaRef:
							{
								setShaderConstantF(flags, predefined.m_loc, &alphaRef, 1);
							}
							break;

						default:
							BX_CHECK(false, "predefined %d not handled", predefined.m_type);
							break;
						}
					}
				}

				{
					for (uint32_t stage = 0; stage < BGFX_CONFIG_MAX_TEXTURE_SAMPLERS; ++stage)
					{
						const Sampler& sampler = draw.m_sampler[stage];
						Sampler& current = currentState.m_sampler[stage];
						if (current.m_idx != sampler.m_idx
						||  current.m_flags != sampler.m_flags
						||  programChanged)
						{
							if (invalidHandle != sampler.m_idx)
							{
								m_textures[sampler.m_idx].commit(stage, sampler.m_flags);
							}
							else
							{
								DX_CHECK(device->SetTexture(stage, NULL) );
							}
						}

						current = sampler;
					}
				}

				if (programChanged
				||  currentState.m_vertexBuffer.idx != draw.m_vertexBuffer.idx
				||  currentState.m_instanceDataBuffer.idx != draw.m_instanceDataBuffer.idx
				||  currentState.m_instanceDataOffset != draw.m_instanceDataOffset
				||  currentState.m_instanceDataStride != draw.m_instanceDataStride)
				{
					currentState.m_vertexBuffer = draw.m_vertexBuffer;
					currentState.m_instanceDataBuffer.idx = draw.m_instanceDataBuffer.idx;
					currentState.m_instanceDataOffset = draw.m_instanceDataOffset;
					currentState.m_instanceDataStride = draw.m_instanceDataStride;

					uint16_t handle = draw.m_vertexBuffer.idx;
					if (invalidHandle != handle)
					{
						const VertexBufferD3D9& vb = m_vertexBuffers[handle];

						uint16_t decl = !isValid(vb.m_decl) ? draw.m_vertexDecl.idx : vb.m_decl.idx;
						const VertexDeclD3D9& vertexDecl = m_vertexDecls[decl];
						DX_CHECK(device->SetStreamSource(0, vb.m_ptr, 0, vertexDecl.m_decl.m_stride) );

						if (isValid(draw.m_instanceDataBuffer)
						&&  m_instancing)
						{
							const VertexBufferD3D9& inst = m_vertexBuffers[draw.m_instanceDataBuffer.idx];
							DX_CHECK(device->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA|draw.m_numInstances) );
							DX_CHECK(device->SetStreamSourceFreq(1, UINT(D3DSTREAMSOURCE_INSTANCEDATA|1) ) );
							DX_CHECK(device->SetStreamSource(1, inst.m_ptr, draw.m_instanceDataOffset, draw.m_instanceDataStride) );

							IDirect3DVertexDeclaration9* ptr = createVertexDeclaration(vertexDecl.m_decl, draw.m_instanceDataStride/16);
							DX_CHECK(device->SetVertexDeclaration(ptr) );
							DX_RELEASE(ptr, 0);
						}
						else
						{
							DX_CHECK(device->SetStreamSourceFreq(0, 1) );
							DX_CHECK(device->SetStreamSource(1, NULL, 0, 0) );
							DX_CHECK(device->SetVertexDeclaration(vertexDecl.m_ptr) );
						}
					}
					else
					{
						DX_CHECK(device->SetStreamSource(0, NULL, 0, 0) );
						DX_CHECK(device->SetStreamSource(1, NULL, 0, 0) );
					}
				}

				if (currentState.m_indexBuffer.idx != draw.m_indexBuffer.idx)
				{
					currentState.m_indexBuffer = draw.m_indexBuffer;

					uint16_t handle = draw.m_indexBuffer.idx;
					if (invalidHandle != handle)
					{
						const IndexBufferD3D9& ib = m_indexBuffers[handle];
						DX_CHECK(device->SetIndices(ib.m_ptr) );
					}
					else
					{
						DX_CHECK(device->SetIndices(NULL) );
					}
				}

				if (isValid(currentState.m_vertexBuffer) )
				{
					uint32_t numVertices = draw.m_numVertices;
					if (UINT32_MAX == numVertices)
					{
						const VertexBufferD3D9& vb = m_vertexBuffers[currentState.m_vertexBuffer.idx];
						uint16_t decl = !isValid(vb.m_decl) ? draw.m_vertexDecl.idx : vb.m_decl.idx;
						const VertexDeclD3D9& vertexDecl = m_vertexDecls[decl];
						numVertices = vb.m_size/vertexDecl.m_decl.m_stride;
					}

					uint32_t numIndices = 0;
					uint32_t numPrimsSubmitted = 0;
					uint32_t numInstances = 0;
					uint32_t numPrimsRendered = 0;

					if (isValid(draw.m_indexBuffer) )
					{
						if (UINT32_MAX == draw.m_numIndices)
						{
							numIndices = m_indexBuffers[draw.m_indexBuffer.idx].m_size/2;
							numPrimsSubmitted = numIndices/prim.m_div - prim.m_sub;
							numInstances = draw.m_numInstances;
							numPrimsRendered = numPrimsSubmitted*draw.m_numInstances;

							DX_CHECK(device->DrawIndexedPrimitive(prim.m_type
								, draw.m_startVertex
								, 0
								, numVertices
								, 0
								, numPrimsSubmitted
								) );
						}
						else if (prim.m_min <= draw.m_numIndices)
						{
							numIndices = draw.m_numIndices;
							numPrimsSubmitted = numIndices/prim.m_div - prim.m_sub;
							numInstances = draw.m_numInstances;
							numPrimsRendered = numPrimsSubmitted*draw.m_numInstances;

							DX_CHECK(device->DrawIndexedPrimitive(prim.m_type
								, draw.m_startVertex
								, 0
								, numVertices
								, draw.m_startIndex
								, numPrimsSubmitted
								) );
						}
					}
					else
					{
						numPrimsSubmitted = numVertices/prim.m_div - prim.m_sub;
						numInstances = draw.m_numInstances;
						numPrimsRendered = numPrimsSubmitted*draw.m_numInstances;

						DX_CHECK(device->DrawPrimitive(prim.m_type
							, draw.m_startVertex
							, numPrimsSubmitted
							) );
					}

					statsNumPrimsSubmitted[primIndex] += numPrimsSubmitted;
					statsNumPrimsRendered[primIndex]  += numPrimsRendered;
					statsNumInstances[primIndex]      += numInstances;
					statsNumIndices += numIndices;
				}
			}

			PIX_ENDEVENT();

			if (0 < _render->m_num)
			{
				captureElapsed = -bx::getHPCounter();
				capture();
				captureElapsed += bx::getHPCounter();
			}
		}

		int64_t now = bx::getHPCounter();
		elapsed += now;

		static int64_t last = now;
		int64_t frameTime = now - last;
		last = now;

		static int64_t min = frameTime;
		static int64_t max = frameTime;
		min = min > frameTime ? frameTime : min;
		max = max < frameTime ? frameTime : max;

		if (_render->m_debug & (BGFX_DEBUG_IFH|BGFX_DEBUG_STATS) )
		{
			PIX_BEGINEVENT(D3DCOLOR_RGBA(0x40, 0x40, 0x40, 0xff), L"debugstats");

			TextVideoMem& tvm = m_textVideoMem;

			static int64_t next = now;

			if (now >= next)
			{
				next = now + bx::getHPFrequency();

				double freq = double(bx::getHPFrequency() );
				double toMs = 1000.0/freq;

				tvm.clear();
				uint16_t pos = 0;
				tvm.printf(0, pos++, BGFX_CONFIG_DEBUG ? 0x89 : 0x8f, " %s / " BX_COMPILER_NAME " / " BX_CPU_NAME " / " BX_ARCH_NAME " / " BX_PLATFORM_NAME " "
					, getRendererName()
					);

				const D3DADAPTER_IDENTIFIER9& identifier = m_identifier;
				tvm.printf(0, pos++, 0x0f, " Device: %s (%s)", identifier.Description, identifier.Driver);

				pos = 10;
				tvm.printf(10, pos++, 0x8e, "       Frame: %7.3f, % 7.3f \x1f, % 7.3f \x1e [ms] / % 6.2f FPS "
					, double(frameTime)*toMs
					, double(min)*toMs
					, double(max)*toMs
					, freq/frameTime
					);

				const uint32_t msaa = (m_resolution.m_flags&BGFX_RESET_MSAA_MASK)>>BGFX_RESET_MSAA_SHIFT;
				tvm.printf(10, pos++, 0x8e, " Reset flags: [%c] vsync, [%c] MSAAx%d "
					, !!(m_resolution.m_flags&BGFX_RESET_VSYNC) ? '\xfe' : ' '
					, 0 != msaa ? '\xfe' : ' '
					, 1<<msaa
					);

				double elapsedCpuMs = double(elapsed)*toMs;
				tvm.printf(10, pos++, 0x8e, "  Draw calls: %4d / CPU %3.4f [ms]"
					, _render->m_num
					, elapsedCpuMs
					);
				for (uint32_t ii = 0; ii < BX_COUNTOF(s_primName); ++ii)
				{
					tvm.printf(10, pos++, 0x8e, "    %8s: %7d (#inst: %5d), submitted: %7d"
						, s_primName[ii]
						, statsNumPrimsRendered[ii]
						, statsNumInstances[ii]
						, statsNumPrimsSubmitted[ii]
						);
				}

				tvm.printf(10, pos++, 0x8e, "     Indices: %7d", statsNumIndices);
				tvm.printf(10, pos++, 0x8e, "    DVB size: %7d", _render->m_vboffset);
				tvm.printf(10, pos++, 0x8e, "    DIB size: %7d", _render->m_iboffset);

				double captureMs = double(captureElapsed)*toMs;
				tvm.printf(10, pos++, 0x8e, "     Capture: %3.4f [ms]", captureMs);

				uint8_t attr[2] = { 0x89, 0x8a };
				uint8_t attrIndex = _render->m_waitSubmit < _render->m_waitRender;

				tvm.printf(10, pos++, attr[attrIndex&1], " Submit wait: %3.4f [ms]", _render->m_waitSubmit*toMs);
				tvm.printf(10, pos++, attr[(attrIndex+1)&1], " Render wait: %3.4f [ms]", _render->m_waitRender*toMs);

				min = frameTime;
				max = frameTime;
			}

			blit(this, _textVideoMemBlitter, tvm);

			PIX_ENDEVENT();
		}
		else if (_render->m_debug & BGFX_DEBUG_TEXT)
		{
			PIX_BEGINEVENT(D3DCOLOR_RGBA(0x40, 0x40, 0x40, 0xff), L"debugtext");

			blit(this, _textVideoMemBlitter, _render->m_textVideoMem);

			PIX_ENDEVENT();
		}

		device->EndScene();
	}
} // namespace bgfx

#else

namespace bgfx
{
	RendererContextI* rendererCreateD3D9()
	{
		return NULL;
	}

	void rendererDestroyD3D9()
	{
	}
} // namespace bgfx

#endif // BGFX_CONFIG_RENDERER_DIRECT3D9
/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "bgfx_p.h"

#if (BGFX_CONFIG_RENDERER_OPENGLES || BGFX_CONFIG_RENDERER_OPENGL)
#	include "renderer_gl.h"
#	include <bx/timer.h>
#	include <bx/uint32_t.h>

namespace bgfx
{
	static char s_viewName[BGFX_CONFIG_MAX_VIEWS][256];

	struct PrimInfo
	{
		GLenum m_type;
		uint32_t m_min;
		uint32_t m_div;
		uint32_t m_sub;
	};

	static const PrimInfo s_primInfo[] =
	{
		{ GL_TRIANGLES,      3, 3, 0 },
		{ GL_TRIANGLE_STRIP, 3, 1, 2 },
		{ GL_LINES,          2, 2, 0 },
		{ GL_POINTS,         1, 1, 0 },
	};

	static const char* s_primName[] =
	{
		"TriList",
		"TriStrip",
		"Line",
		"Point",
	};

	static const char* s_attribName[] =
	{
		"a_position",
		"a_normal",
		"a_tangent",
		"a_bitangent",
		"a_color0",
		"a_color1",
		"a_indices",
		"a_weight",
		"a_texcoord0",
		"a_texcoord1",
		"a_texcoord2",
		"a_texcoord3",
		"a_texcoord4",
		"a_texcoord5",
		"a_texcoord6",
		"a_texcoord7",
	};
	BX_STATIC_ASSERT(Attrib::Count == BX_COUNTOF(s_attribName) );

	static const char* s_instanceDataName[] =
	{
		"i_data0",
		"i_data1",
		"i_data2",
		"i_data3",
		"i_data4",
	};
	BX_STATIC_ASSERT(BGFX_CONFIG_MAX_INSTANCE_DATA_COUNT == BX_COUNTOF(s_instanceDataName) );

	static const GLenum s_access[] =
	{
		GL_READ_ONLY,
		GL_WRITE_ONLY,
		GL_READ_WRITE,
	};
	BX_STATIC_ASSERT(Access::Count == BX_COUNTOF(s_access) );

	static const GLenum s_attribType[] =
	{
		GL_UNSIGNED_BYTE,
		GL_SHORT,
		GL_HALF_FLOAT,
		GL_FLOAT,
	};
	BX_STATIC_ASSERT(AttribType::Count == BX_COUNTOF(s_attribType) );

	struct Blend
	{
		GLenum m_src;
		GLenum m_dst;
		bool m_factor;
	};

	static const Blend s_blendFactor[] =
	{
		{ 0,                           0,                           false }, // ignored
		{ GL_ZERO,                     GL_ZERO,                     false }, // ZERO
		{ GL_ONE,                      GL_ONE,                      false }, // ONE
		{ GL_SRC_COLOR,                GL_SRC_COLOR,                false }, // SRC_COLOR
		{ GL_ONE_MINUS_SRC_COLOR,      GL_ONE_MINUS_SRC_COLOR,      false }, // INV_SRC_COLOR
		{ GL_SRC_ALPHA,                GL_SRC_ALPHA,                false }, // SRC_ALPHA
		{ GL_ONE_MINUS_SRC_ALPHA,      GL_ONE_MINUS_SRC_ALPHA,      false }, // INV_SRC_ALPHA
		{ GL_DST_ALPHA,                GL_DST_ALPHA,                false }, // DST_ALPHA
		{ GL_ONE_MINUS_DST_ALPHA,      GL_ONE_MINUS_DST_ALPHA,      false }, // INV_DST_ALPHA
		{ GL_DST_COLOR,                GL_DST_COLOR,                false }, // DST_COLOR
		{ GL_ONE_MINUS_DST_COLOR,      GL_ONE_MINUS_DST_COLOR,      false }, // INV_DST_COLOR
		{ GL_SRC_ALPHA_SATURATE,       GL_ONE,                      false }, // SRC_ALPHA_SAT
		{ GL_CONSTANT_COLOR,           GL_CONSTANT_COLOR,           true  }, // FACTOR
		{ GL_ONE_MINUS_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR, true  }, // INV_FACTOR
	};

	static const GLenum s_blendEquation[] =
	{
		GL_FUNC_ADD,
		GL_FUNC_SUBTRACT,
		GL_FUNC_REVERSE_SUBTRACT,
		GL_MIN,
		GL_MAX,
	};

	static const GLenum s_cmpFunc[] =
	{
		0, // ignored
		GL_LESS,
		GL_LEQUAL,
		GL_EQUAL,
		GL_GEQUAL,
		GL_GREATER,
		GL_NOTEQUAL,
		GL_NEVER,
		GL_ALWAYS,
	};

	static const GLenum s_stencilOp[] =
	{
		GL_ZERO,
		GL_KEEP,
		GL_REPLACE,
		GL_INCR_WRAP,
		GL_INCR,
		GL_DECR_WRAP,
		GL_DECR,
		GL_INVERT,
	};

	static const GLenum s_stencilFace[] =
	{
		GL_FRONT_AND_BACK,
		GL_FRONT,
		GL_BACK,
	};

	static const GLenum s_textureAddress[] =
	{
		GL_REPEAT,
		GL_MIRRORED_REPEAT,
		GL_CLAMP_TO_EDGE,
	};

	static const GLenum s_textureFilterMag[] =
	{
		GL_LINEAR,
		GL_NEAREST,
		GL_LINEAR,
	};

	static const GLenum s_textureFilterMin[][3] =
	{
		{ GL_LINEAR,  GL_LINEAR_MIPMAP_LINEAR,  GL_NEAREST_MIPMAP_LINEAR  },
		{ GL_NEAREST, GL_LINEAR_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_NEAREST },
		{ GL_LINEAR,  GL_LINEAR_MIPMAP_LINEAR,  GL_NEAREST_MIPMAP_LINEAR  },
	};

	struct TextureFormatInfo
	{
		GLenum m_internalFmt;
		GLenum m_fmt;
		GLenum m_type;
		bool m_supported;
	};

	static TextureFormatInfo s_textureFormat[] =
	{
		{ GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,            GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,            GL_ZERO,                         false }, // BC1
		{ GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,            GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,            GL_ZERO,                         false }, // BC2
		{ GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,            GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,            GL_ZERO,                         false }, // BC3
		{ GL_COMPRESSED_LUMINANCE_LATC1_EXT,           GL_COMPRESSED_LUMINANCE_LATC1_EXT,           GL_ZERO,                         false }, // BC4
		{ GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT,     GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT,     GL_ZERO,                         false }, // BC5
		{ GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB,     GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB,     GL_ZERO,                         false }, // BC6H
		{ GL_COMPRESSED_RGBA_BPTC_UNORM_ARB,           GL_COMPRESSED_RGBA_BPTC_UNORM_ARB,           GL_ZERO,                         false }, // BC7
		{ GL_ETC1_RGB8_OES,                            GL_ETC1_RGB8_OES,                            GL_ZERO,                         false }, // ETC1
		{ GL_COMPRESSED_RGB8_ETC2,                     GL_COMPRESSED_RGB8_ETC2,                     GL_ZERO,                         false }, // ETC2
		{ GL_COMPRESSED_RGBA8_ETC2_EAC,                GL_COMPRESSED_RGBA8_ETC2_EAC,                GL_ZERO,                         false }, // ETC2A
		{ GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2, GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2, GL_ZERO,                         false }, // ETC2A1
		{ GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG,          GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG,          GL_ZERO,                         false }, // PTC12
		{ GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG,          GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG,          GL_ZERO,                         false }, // PTC14
		{ GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG,         GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG,         GL_ZERO,                         false }, // PTC12A
		{ GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG,         GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG,         GL_ZERO,                         false }, // PTC14A
		{ GL_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG,         GL_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG,         GL_ZERO,                         false }, // PTC22
		{ GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG,         GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG,         GL_ZERO,                         false }, // PTC24
		{ GL_ZERO,                                     GL_ZERO,                                     GL_ZERO,                         true  }, // Unknown
		{ GL_ZERO,                                     GL_ZERO,                                     GL_ZERO,                         true  }, // R1
		{ GL_LUMINANCE,                                GL_LUMINANCE,                                GL_UNSIGNED_BYTE,                true  }, // R8
		{ GL_R16,                                      GL_RED,                                      GL_UNSIGNED_SHORT,               true  }, // R16
		{ GL_R16F,                                     GL_RED,                                      GL_HALF_FLOAT,                   true  }, // R16F
		{ GL_R32UI,                                    GL_RED,                                      GL_UNSIGNED_INT,                 true  }, // R32
		{ GL_R32F,                                     GL_RED,                                      GL_FLOAT,                        true  }, // R32F
		{ GL_RG8,                                      GL_RG,                                       GL_UNSIGNED_BYTE,                true  }, // RG8
		{ GL_RG16,                                     GL_RG,                                       GL_UNSIGNED_SHORT,               true  }, // RG16
		{ GL_RG16F,                                    GL_RG,                                       GL_FLOAT,                        true  }, // RG16F
		{ GL_RG32UI,                                   GL_RG,                                       GL_UNSIGNED_INT,                 true  }, // RG32
		{ GL_RG32F,                                    GL_RG,                                       GL_FLOAT,                        true  }, // RG32F
		{ GL_RGBA,                                     GL_RGBA,                                     GL_UNSIGNED_BYTE,                true  }, // BGRA8
		{ GL_RGBA16,                                   GL_RGBA,                                     GL_UNSIGNED_BYTE,                true  }, // RGBA16
		{ GL_RGBA16F,                                  GL_RGBA,                                     GL_HALF_FLOAT,                   true  }, // RGBA16F
		{ GL_RGBA32UI,                                 GL_RGBA,                                     GL_UNSIGNED_INT,                 true  }, // RGBA32
		{ GL_RGBA32F,                                  GL_RGBA,                                     GL_FLOAT,                        true  }, // RGBA32F
		{ GL_RGB565,                                   GL_RGB,                                      GL_UNSIGNED_SHORT_5_6_5,         true  }, // R5G6B5
		{ GL_RGBA4,                                    GL_RGBA,                                     GL_UNSIGNED_SHORT_4_4_4_4,       true  }, // RGBA4
		{ GL_RGB5_A1,                                  GL_RGBA,                                     GL_UNSIGNED_SHORT_5_5_5_1,       true  }, // RGB5A1
		{ GL_RGB10_A2,                                 GL_RGBA,                                     GL_UNSIGNED_INT_2_10_10_10_REV,  true  }, // RGB10A2
		{ GL_R11F_G11F_B10F,                           GL_RGB,                                      GL_UNSIGNED_INT_10F_11F_11F_REV, true  }, // R11G11B10F
		{ GL_ZERO,                                     GL_ZERO,                                     GL_ZERO,                         true  }, // UnknownDepth
		{ GL_DEPTH_COMPONENT16,                        GL_DEPTH_COMPONENT,                          GL_UNSIGNED_SHORT,               false }, // D16
		{ GL_DEPTH_COMPONENT24,                        GL_DEPTH_COMPONENT,                          GL_UNSIGNED_INT,                 false }, // D24
		{ GL_DEPTH24_STENCIL8,                         GL_DEPTH_STENCIL,                            GL_UNSIGNED_INT_24_8,            false }, // D24S8
		{ GL_DEPTH_COMPONENT32,                        GL_DEPTH_COMPONENT,                          GL_UNSIGNED_INT,                 false }, // D32
		{ GL_DEPTH_COMPONENT32F,                       GL_DEPTH_COMPONENT,                          GL_FLOAT,                        false }, // D16F
		{ GL_DEPTH_COMPONENT32F,                       GL_DEPTH_COMPONENT,                          GL_FLOAT,                        false }, // D24F
		{ GL_DEPTH_COMPONENT32F,                       GL_DEPTH_COMPONENT,                          GL_FLOAT,                        false }, // D32F
		{ GL_STENCIL_INDEX8,                           GL_DEPTH_STENCIL,                            GL_UNSIGNED_BYTE,                false }, // D0S8
	};
	BX_STATIC_ASSERT(TextureFormat::Count == BX_COUNTOF(s_textureFormat) );

	static GLenum s_imageFormat[] =
	{
		GL_ZERO,           // BC1
		GL_ZERO,           // BC2
		GL_ZERO,           // BC3
		GL_ZERO,           // BC4
		GL_ZERO,           // BC5
		GL_ZERO,           // BC6H
		GL_ZERO,           // BC7
		GL_ZERO,           // ETC1
		GL_ZERO,           // ETC2
		GL_ZERO,           // ETC2A
		GL_ZERO,           // ETC2A1
		GL_ZERO,           // PTC12
		GL_ZERO,           // PTC14
		GL_ZERO,           // PTC12A
		GL_ZERO,           // PTC14A
		GL_ZERO,           // PTC22
		GL_ZERO,           // PTC24
		GL_ZERO,           // Unknown
		GL_ZERO,           // R1
		GL_R8,             // R8
		GL_R16,            // R16
		GL_R16F,           // R16F
		GL_R32UI,          // R32
		GL_R32F,           // R32F
		GL_RG8,            // RG8
		GL_RG16,           // RG16
		GL_RG16F,          // RG16F
		GL_RG32UI,         // RG32
		GL_RG32F,          // RG32F
		GL_RGBA8,          // BGRA8
		GL_RGBA16,         // RGBA16
		GL_RGBA16F,        // RGBA16F
		GL_RGBA32UI,       // RGBA32
		GL_RGBA32F,        // RGBA32F
		GL_RGB565,         // R5G6B5
		GL_RGBA4,          // RGBA4
		GL_RGB5_A1,        // RGB5A1
		GL_RGB10_A2,       // RGB10A2
		GL_R11F_G11F_B10F, // R11G11B10F
		GL_ZERO,           // UnknownDepth
		GL_ZERO,           // D16
		GL_ZERO,           // D24
		GL_ZERO,           // D24S8
		GL_ZERO,           // D32
		GL_ZERO,           // D16F
		GL_ZERO,           // D24F
		GL_ZERO,           // D32F
		GL_ZERO,           // D0S8
	};
	BX_STATIC_ASSERT(TextureFormat::Count == BX_COUNTOF(s_imageFormat) );

	struct Extension
	{
		enum Enum
		{
			AMD_conservative_depth,

			ANGLE_depth_texture,
			ANGLE_framebuffer_blit,
			ANGLE_framebuffer_multisample,
			ANGLE_instanced_arrays,
			ANGLE_texture_compression_dxt1,
			ANGLE_texture_compression_dxt3,
			ANGLE_texture_compression_dxt5,
			ANGLE_translated_shader_source,

			APPLE_texture_format_BGRA8888,
			APPLE_texture_max_level,

			ARB_compute_shader,
			ARB_conservative_depth,
			ARB_debug_label,
			ARB_debug_output,
			ARB_depth_buffer_float,
			ARB_depth_clamp,
			ARB_draw_buffers_blend,
			ARB_draw_instanced,
			ARB_ES3_compatibility,
			ARB_framebuffer_object,
			ARB_framebuffer_sRGB,
			ARB_get_program_binary,
			ARB_half_float_pixel,
			ARB_half_float_vertex,
			ARB_instanced_arrays,
			ARB_map_buffer_range,
			ARB_multisample,
			ARB_occlusion_query,
			ARB_occlusion_query2,
			ARB_program_interface_query,
			ARB_sampler_objects,
			ARB_seamless_cube_map,
			ARB_shader_bit_encoding,
			ARB_shader_image_load_store,
			ARB_shader_storage_buffer_object,
			ARB_shader_texture_lod,
			ARB_texture_compression_bptc,
			ARB_texture_compression_rgtc,
			ARB_texture_float,
			ARB_texture_multisample,
			ARB_texture_rg,
			ARB_texture_rgb10_a2ui,
			ARB_texture_stencil8,
			ARB_texture_storage,
			ARB_texture_swizzle,
			ARB_timer_query,
			ARB_uniform_buffer_object,
			ARB_vertex_array_object,
			ARB_vertex_type_2_10_10_10_rev,

			ATI_meminfo,

			CHROMIUM_color_buffer_float_rgb,
			CHROMIUM_color_buffer_float_rgba,
			CHROMIUM_depth_texture,
			CHROMIUM_framebuffer_multisample,
			CHROMIUM_texture_compression_dxt3,
			CHROMIUM_texture_compression_dxt5,

			EXT_bgra,
			EXT_blend_color,
			EXT_blend_minmax,
			EXT_blend_subtract,
			EXT_compressed_ETC1_RGB8_sub_texture,
			EXT_debug_label,
			EXT_debug_marker,
			EXT_draw_buffers,
			EXT_frag_depth,
			EXT_framebuffer_blit,
			EXT_framebuffer_object,
			EXT_framebuffer_sRGB,
			EXT_occlusion_query_boolean,
			EXT_packed_float,
			EXT_read_format_bgra,
			EXT_shader_image_load_store,
			EXT_shader_texture_lod,
			EXT_shadow_samplers,
			EXT_texture_array,
			EXT_texture_compression_dxt1,
			EXT_texture_compression_latc,
			EXT_texture_compression_rgtc,
			EXT_texture_compression_s3tc,
			EXT_texture_filter_anisotropic,
			EXT_texture_format_BGRA8888,
			EXT_texture_rg,
			EXT_texture_sRGB,
			EXT_texture_storage,
			EXT_texture_swizzle,
			EXT_texture_type_2_10_10_10_REV,
			EXT_timer_query,
			EXT_unpack_subimage,

			GOOGLE_depth_texture,

			GREMEDY_string_marker,
			GREMEDY_frame_terminator,

			IMG_multisampled_render_to_texture,
			IMG_read_format,
			IMG_shader_binary,
			IMG_texture_compression_pvrtc,
			IMG_texture_compression_pvrtc2,
			IMG_texture_format_BGRA8888,

			INTEL_fragment_shader_ordering,

			KHR_debug,

			MOZ_WEBGL_compressed_texture_s3tc,
			MOZ_WEBGL_depth_texture,

			NV_draw_buffers,
			NVX_gpu_memory_info,

			OES_compressed_ETC1_RGB8_texture,
			OES_depth24,
			OES_depth32,
			OES_depth_texture,
			OES_fragment_precision_high,
			OES_get_program_binary,
			OES_required_internalformat,
			OES_packed_depth_stencil,
			OES_read_format,
			OES_rgb8_rgba8,
			OES_standard_derivatives,
			OES_texture_3D,
			OES_texture_float,
			OES_texture_float_linear,
			OES_texture_npot,
			OES_texture_half_float,
			OES_texture_half_float_linear,
			OES_vertex_array_object,
			OES_vertex_half_float,
			OES_vertex_type_10_10_10_2,

			WEBGL_compressed_texture_etc1,
			WEBGL_compressed_texture_s3tc,
			WEBGL_compressed_texture_pvrtc,
			WEBGL_depth_texture,

			WEBKIT_EXT_texture_filter_anisotropic,
			WEBKIT_WEBGL_compressed_texture_s3tc,
			WEBKIT_WEBGL_depth_texture,

			Count
		};

		const char* m_name;
		bool m_supported;
		bool m_initialize;
	};

	static Extension s_extension[Extension::Count] =
	{
		{ "AMD_conservative_depth",                false,                             true  },

		{ "ANGLE_depth_texture",                   false,                             true  },
		{ "ANGLE_framebuffer_blit",                false,                             true  },
		{ "ANGLE_framebuffer_multisample",         false,                             false },
		{ "ANGLE_instanced_arrays",                false,                             true  },
		{ "ANGLE_texture_compression_dxt1",        false,                             true  },
		{ "ANGLE_texture_compression_dxt3",        false,                             true  },
		{ "ANGLE_texture_compression_dxt5",        false,                             true  },
		{ "ANGLE_translated_shader_source",        false,                             true  },

		{ "APPLE_texture_format_BGRA8888",         false,                             true  },
		{ "APPLE_texture_max_level",               false,                             true  },

		{ "ARB_compute_shader",                    BGFX_CONFIG_RENDERER_OPENGL >= 43, true  },
		{ "ARB_conservative_depth",                BGFX_CONFIG_RENDERER_OPENGL >= 42, true  },
		{ "ARB_debug_label",                       false,                             true  },
		{ "ARB_debug_output",                      BGFX_CONFIG_RENDERER_OPENGL >= 43, true  },
		{ "ARB_depth_buffer_float",                BGFX_CONFIG_RENDERER_OPENGL >= 33, true  },
		{ "ARB_depth_clamp",                       BGFX_CONFIG_RENDERER_OPENGL >= 32, true  },
		{ "ARB_draw_buffers_blend",                BGFX_CONFIG_RENDERER_OPENGL >= 40, true  },
		{ "ARB_draw_instanced",                    BGFX_CONFIG_RENDERER_OPENGL >= 33, true  },
		{ "ARB_ES3_compatibility",                 BGFX_CONFIG_RENDERER_OPENGL >= 43, true  },
		{ "ARB_framebuffer_object",                BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "ARB_framebuffer_sRGB",                  BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "ARB_get_program_binary",                BGFX_CONFIG_RENDERER_OPENGL >= 41, true  },
		{ "ARB_half_float_pixel",                  BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "ARB_half_float_vertex",                 BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "ARB_instanced_arrays",                  BGFX_CONFIG_RENDERER_OPENGL >= 33, true  },
		{ "ARB_map_buffer_range",                  BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "ARB_multisample",                       false,                             true  },
		{ "ARB_occlusion_query",                   BGFX_CONFIG_RENDERER_OPENGL >= 33, true  },
		{ "ARB_occlusion_query2",                  BGFX_CONFIG_RENDERER_OPENGL >= 33, true  },
		{ "ARB_program_interface_query",           BGFX_CONFIG_RENDERER_OPENGL >= 43, true  },
		{ "ARB_sampler_objects",                   BGFX_CONFIG_RENDERER_OPENGL >= 33, true  },
		{ "ARB_seamless_cube_map",                 BGFX_CONFIG_RENDERER_OPENGL >= 32, true  },
		{ "ARB_shader_bit_encoding",               BGFX_CONFIG_RENDERER_OPENGL >= 33, true  },
		{ "ARB_shader_image_load_store",           BGFX_CONFIG_RENDERER_OPENGL >= 42, true  },
		{ "ARB_shader_storage_buffer_object",      BGFX_CONFIG_RENDERER_OPENGL >= 43, true  },
		{ "ARB_shader_texture_lod",                BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "ARB_texture_compression_bptc",          BGFX_CONFIG_RENDERER_OPENGL >= 44, true  },
		{ "ARB_texture_compression_rgtc",          BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "ARB_texture_float",                     BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "ARB_texture_multisample",               BGFX_CONFIG_RENDERER_OPENGL >= 32, true  },
		{ "ARB_texture_rg",                        BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "ARB_texture_rgb10_a2ui",                BGFX_CONFIG_RENDERER_OPENGL >= 33, true  },
		{ "ARB_texture_stencil8",                  false,                             true  },
		{ "ARB_texture_storage",                   BGFX_CONFIG_RENDERER_OPENGL >= 42, true  },
		{ "ARB_texture_swizzle",                   BGFX_CONFIG_RENDERER_OPENGL >= 33, true  },
		{ "ARB_timer_query",                       BGFX_CONFIG_RENDERER_OPENGL >= 33, true  },
		{ "ARB_uniform_buffer_object",             BGFX_CONFIG_RENDERER_OPENGL >= 31, true  },
		{ "ARB_vertex_array_object",               BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "ARB_vertex_type_2_10_10_10_rev",        false,                             true  },

		{ "ATI_meminfo",                           false,                             true  },

		{ "CHROMIUM_color_buffer_float_rgb",       false,                             true  },
		{ "CHROMIUM_color_buffer_float_rgba",      false,                             true  },
		{ "CHROMIUM_depth_texture",                false,                             true  },
		{ "CHROMIUM_framebuffer_multisample",      false,                             true  },
		{ "CHROMIUM_texture_compression_dxt3",     false,                             true  },
		{ "CHROMIUM_texture_compression_dxt5",     false,                             true  },

		{ "EXT_bgra",                              false,                             true  },
		{ "EXT_blend_color",                       BGFX_CONFIG_RENDERER_OPENGL >= 31, true  },
		{ "EXT_blend_minmax",                      BGFX_CONFIG_RENDERER_OPENGL >= 14, true  },
		{ "EXT_blend_subtract",                    BGFX_CONFIG_RENDERER_OPENGL >= 14, true  },
		{ "EXT_compressed_ETC1_RGB8_sub_texture",  false,                             true  }, // GLES2 extension.
		{ "EXT_debug_label",                       false,                             true  },
		{ "EXT_debug_marker",                      false,                             true  },
		{ "EXT_draw_buffers",                      false,                             true  }, // GLES2 extension.
		{ "EXT_frag_depth",                        false,                             true  }, // GLES2 extension.
		{ "EXT_framebuffer_blit",                  BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "EXT_framebuffer_object",                BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "EXT_framebuffer_sRGB",                  BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "EXT_occlusion_query_boolean",           false,                             true  },
		{ "EXT_packed_float",                      BGFX_CONFIG_RENDERER_OPENGL >= 33, true  },
		{ "EXT_read_format_bgra",                  false,                             true  },
		{ "EXT_shader_image_load_store",           false,                             true  },
		{ "EXT_shader_texture_lod",                false,                             true  }, // GLES2 extension.
		{ "EXT_shadow_samplers",                   false,                             true  },
		{ "EXT_texture_array",                     BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "EXT_texture_compression_dxt1",          false,                             true  },
		{ "EXT_texture_compression_latc",          false,                             true  },
		{ "EXT_texture_compression_rgtc",          BGFX_CONFIG_RENDERER_OPENGL >= 30, true  },
		{ "EXT_texture_compression_s3tc",          false,                             true  },
		{ "EXT_texture_filter_anisotropic",        false,                             true  },
		{ "EXT_texture_format_BGRA8888",           false,                             true  },
		{ "EXT_texture_rg",                        false,                             true  }, // GLES2 extension.
		{ "EXT_texture_sRGB",                      false,                             true  },
		{ "EXT_texture_storage",                   false,                             true  },
		{ "EXT_texture_swizzle",                   false,                             true  },
		{ "EXT_texture_type_2_10_10_10_REV",       false,                             true  },
		{ "EXT_timer_query",                       false,                             true  },
		{ "EXT_unpack_subimage",                   false,                             true  },

		{ "GOOGLE_depth_texture",                  false,                             true  },

		{ "GREMEDY_string_marker",                 false,                             true  },
		{ "GREMEDY_frame_terminator",              false,                             true  },

		{ "IMG_multisampled_render_to_texture",    false,                             true  },
		{ "IMG_read_format",                       false,                             true  },
		{ "IMG_shader_binary",                     false,                             true  },
		{ "IMG_texture_compression_pvrtc",         false,                             true  },
		{ "IMG_texture_compression_pvrtc2",        false,                             true  },
		{ "IMG_texture_format_BGRA8888",           false,                             true  },

		{ "INTEL_fragment_shader_ordering",        false,                             true  },

		{ "KHR_debug",                             BGFX_CONFIG_RENDERER_OPENGL >= 43, true  },

		{ "MOZ_WEBGL_compressed_texture_s3tc",     false,                             true  },
		{ "MOZ_WEBGL_depth_texture",               false,                             true  },

		{ "NV_draw_buffers",                       false,                             true  }, // GLES2 extension.
		{ "NVX_gpu_memory_info",                   false,                             true  },

		{ "OES_compressed_ETC1_RGB8_texture",      false,                             true  },
		{ "OES_depth24",                           false,                             true  },
		{ "OES_depth32",                           false,                             true  },
		{ "OES_depth_texture",                     false,                             true  },
		{ "OES_fragment_precision_high",           false,                             true  },
		{ "OES_get_program_binary",                false,                             true  },
		{ "OES_required_internalformat",           false,                             true  },
		{ "OES_packed_depth_stencil",              false,                             true  },
		{ "OES_read_format",                       false,                             true  },
		{ "OES_rgb8_rgba8",                        false,                             true  },
		{ "OES_standard_derivatives",              false,                             true  },
		{ "OES_texture_3D",                        false,                             true  },
		{ "OES_texture_float",                     false,                             true  },
		{ "OES_texture_float_linear",              false,                             true  },
		{ "OES_texture_npot",                      false,                             true  },
		{ "OES_texture_half_float",                false,                             true  },
		{ "OES_texture_half_float_linear",         false,                             true  },
		{ "OES_vertex_array_object",               false,                             !BX_PLATFORM_IOS },
		{ "OES_vertex_half_float",                 false,                             true  },
		{ "OES_vertex_type_10_10_10_2",            false,                             true  },

		{ "WEBGL_compressed_texture_etc1",         false,                             true  },
		{ "WEBGL_compressed_texture_s3tc",         false,                             true  },
		{ "WEBGL_compressed_texture_pvrtc",        false,                             true  },
		{ "WEBGL_depth_texture",                   false,                             true  },

		{ "WEBKIT_EXT_texture_filter_anisotropic", false,                             true  },
		{ "WEBKIT_WEBGL_compressed_texture_s3tc",  false,                             true  },
		{ "WEBKIT_WEBGL_depth_texture",            false,                             true  },
	};

	static const char* s_ARB_shader_texture_lod[] =
	{
		"texture2DLod",
		"texture2DProjLod",
		"texture3DLod",
		"texture3DProjLod",
		"textureCubeLod",
		"shadow2DLod",
		"shadow2DProjLod",
		NULL
		// "texture1DLod",
		// "texture1DProjLod",
		// "shadow1DLod",
		// "shadow1DProjLod",
	};

	static const char* s_EXT_shader_texture_lod[] =
	{
		"texture2DLod",
		"texture2DProjLod",
		"textureCubeLod",
		NULL
		// "texture2DGrad",
		// "texture2DProjGrad",
		// "textureCubeGrad",
	};

	static const char* s_EXT_shadow_samplers[] =
	{
		"shadow2D",
		"shadow2DProj",
		NULL
	};

	static const char* s_OES_standard_derivatives[] =
	{
		"dFdx",
		"dFdy",
		"fwidth",
		NULL
	};

	static const char* s_OES_texture_3D[] =
	{
		"texture3D",
		"texture3DProj",
		"texture3DLod",
		"texture3DProjLod",
		NULL
	};

	static void GL_APIENTRY stubVertexAttribDivisor(GLuint /*_index*/, GLuint /*_divisor*/)
	{
	}

	static void GL_APIENTRY stubDrawArraysInstanced(GLenum _mode, GLint _first, GLsizei _count, GLsizei /*_primcount*/)
	{
		GL_CHECK(glDrawArrays(_mode, _first, _count) );
	}

	static void GL_APIENTRY stubDrawElementsInstanced(GLenum _mode, GLsizei _count, GLenum _type, const GLvoid* _indices, GLsizei /*_primcount*/)
	{
		GL_CHECK(glDrawElements(_mode, _count, _type, _indices) );
	}

	static void GL_APIENTRY stubFrameTerminatorGREMEDY()
	{
	}

	static void GL_APIENTRY stubInsertEventMarker(GLsizei /*_length*/, const char* /*_marker*/)
	{
	}

	static void GL_APIENTRY stubInsertEventMarkerGREMEDY(GLsizei _length, const char* _marker)
	{
		// If <marker> is a null-terminated string then <length> should not
		// include the terminator.
		//
		// If <length> is 0 then <marker> is assumed to be null-terminated.

		uint32_t size = (0 == _length ? (uint32_t)strlen(_marker) : _length) + 1;
		size *= sizeof(wchar_t);
		wchar_t* name = (wchar_t*)alloca(size);
		mbstowcs(name, _marker, size-2);
		GL_CHECK(glStringMarkerGREMEDY(_length, _marker) );
	}

	static void GL_APIENTRY stubObjectLabel(GLenum /*_identifier*/, GLuint /*_name*/, GLsizei /*_length*/, const char* /*_label*/)
	{
	}

	typedef void (*PostSwapBuffersFn)(uint32_t _width, uint32_t _height);

	static const char* getGLString(GLenum _name)
	{
		const char* str = (const char*)glGetString(_name);
		glGetError(); // ignore error if glGetString returns NULL.
		if (NULL != str)
		{
			return str;
		}

		return "<unknown>";
	}

	static uint32_t getGLStringHash(GLenum _name)
	{
		const char* str = (const char*)glGetString(_name);
		glGetError(); // ignore error if glGetString returns NULL.
		if (NULL != str)
		{
			return bx::hashMurmur2A(str, (uint32_t)strlen(str) );
		}

		return 0;
	}

	void dumpExtensions(const char* _extensions)
	{
		if (NULL != _extensions)
		{
			char name[1024];
			const char* pos = _extensions;
			const char* end = _extensions + strlen(_extensions);
			while (pos < end)
			{
				uint32_t len;
				const char* space = strchr(pos, ' ');
				if (NULL != space)
				{
					len = bx::uint32_min(sizeof(name), (uint32_t)(space - pos) );
				}
				else
				{
					len = bx::uint32_min(sizeof(name), (uint32_t)strlen(pos) );
				}

				strncpy(name, pos, len);
				name[len] = '\0';

				BX_TRACE("\t%s", name);

				pos += len+1;
			}
		}
	}

	const char* toString(GLenum _enum)
	{
#if defined(GL_DEBUG_SOURCE_API_ARB)
		switch (_enum)
		{
		case GL_DEBUG_SOURCE_API_ARB:               return "API";
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB:     return "WinSys";
		case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB:   return "Shader";
		case GL_DEBUG_SOURCE_THIRD_PARTY_ARB:       return "3rdparty";
		case GL_DEBUG_SOURCE_APPLICATION_ARB:       return "Application";
		case GL_DEBUG_SOURCE_OTHER_ARB:             return "Other";
		case GL_DEBUG_TYPE_ERROR_ARB:               return "Error";
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB: return "Deprecated behavior";
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:  return "Undefined behavior";
		case GL_DEBUG_TYPE_PORTABILITY_ARB:         return "Portability";
		case GL_DEBUG_TYPE_PERFORMANCE_ARB:         return "Performance";
		case GL_DEBUG_TYPE_OTHER_ARB:               return "Other";
		case GL_DEBUG_SEVERITY_HIGH_ARB:            return "High";
		case GL_DEBUG_SEVERITY_MEDIUM_ARB:          return "Medium";
		case GL_DEBUG_SEVERITY_LOW_ARB:             return "Low";
		default:
			break;
		}
#else
		BX_UNUSED(_enum);
#endif // defined(GL_DEBUG_SOURCE_API_ARB)

		return "<unknown>";
	}

	void GL_APIENTRY debugProcCb(GLenum _source, GLenum _type, GLuint _id, GLenum _severity, GLsizei /*_length*/, const GLchar* _message, const void* /*_userParam*/)
	{
		BX_TRACE("src %s, type %s, id %d, severity %s, '%s'"
				, toString(_source)
				, toString(_type)
				, _id
				, toString(_severity)
				, _message
				);
		BX_UNUSED(_source, _type, _id, _severity, _message);
	}

	GLint glGet(GLenum _pname)
	{
		GLint result = 0;
		glGetIntegerv(_pname, &result);
		GLenum err = glGetError();
		BX_WARN(0 == err, "glGetIntegerv(0x%04x, ...) failed with GL error: 0x%04x.", _pname, err);
		return 0 == err ? result : 0;
	}

	void setTextureFormat(TextureFormat::Enum _format, GLenum _internalFmt, GLenum _fmt, GLenum _type = GL_ZERO)
	{
		TextureFormatInfo& tfi = s_textureFormat[_format];
		tfi.m_internalFmt = _internalFmt;
		tfi.m_fmt         = _fmt;
		tfi.m_type        = _type;
	}

	bool isTextureFormatValid(TextureFormat::Enum _format)
	{
		GLuint id;
		GL_CHECK(glGenTextures(1, &id) );
		GL_CHECK(glBindTexture(GL_TEXTURE_2D, id) );

		const TextureFormatInfo& tfi = s_textureFormat[_format];

		GLsizei size = (16*16*getBitsPerPixel(_format) )/8;
		void* data = alloca(size);

		if (isCompressed(_format) )
		{
			glCompressedTexImage2D(GL_TEXTURE_2D, 0, tfi.m_internalFmt, 16, 16, 0, size, data);
		}
		else
		{
			glTexImage2D(GL_TEXTURE_2D, 0, tfi.m_internalFmt, 16, 16, 0, tfi.m_fmt, tfi.m_type, data);
		}

		GLenum err = glGetError();
		BX_WARN(0 == err, "TextureFormat::%s is not supported (%x: %s).", getName(_format), err, glEnumName(err) );

		GL_CHECK(glDeleteTextures(1, &id) );

		return 0 == err;
	}

	struct RendererContextGL : public RendererContextI
	{
		RendererContextGL()
			: m_numWindows(1)
			, m_rtMsaa(false)
			, m_capture(NULL)
			, m_captureSize(0)
			, m_maxAnisotropy(0.0f)
			, m_maxMsaa(0)
			, m_vao(0)
			, m_vaoSupport(false)
			, m_samplerObjectSupport(false)
			, m_shadowSamplersSupport(false)
			, m_programBinarySupport(false)
			, m_textureSwizzleSupport(false)
			, m_depthTextureSupport(false)
			, m_flip(false)
			, m_hash( (BX_PLATFORM_WINDOWS<<1) | BX_ARCH_64BIT)
			, m_backBufferFbo(0)
			, m_msaaBackBufferFbo(0)
		{
			m_fbh.idx = invalidHandle;
			memset(m_uniforms, 0, sizeof(m_uniforms) );
			memset(&m_resolution, 0, sizeof(m_resolution) );

			setRenderContextSize(BGFX_DEFAULT_WIDTH, BGFX_DEFAULT_HEIGHT);

			m_vendor = getGLString(GL_VENDOR);
			m_renderer = getGLString(GL_RENDERER);
			m_version = getGLString(GL_VERSION);
			m_glslVersion = getGLString(GL_SHADING_LANGUAGE_VERSION);

			GLint numCmpFormats = 0;
			GL_CHECK(glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &numCmpFormats) );
			BX_TRACE("GL_NUM_COMPRESSED_TEXTURE_FORMATS %d", numCmpFormats);

			GLint* cmpFormat = NULL;

			if (0 < numCmpFormats)
			{
				numCmpFormats = numCmpFormats > 256 ? 256 : numCmpFormats;
				cmpFormat = (GLint*)alloca(sizeof(GLint)*numCmpFormats);
				GL_CHECK(glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, cmpFormat) );

				for (GLint ii = 0; ii < numCmpFormats; ++ii)
				{
					GLint internalFmt = cmpFormat[ii];
					uint32_t fmt = uint32_t(TextureFormat::Unknown);
					for (uint32_t jj = 0; jj < fmt; ++jj)
					{
						if (s_textureFormat[jj].m_internalFmt == (GLenum)internalFmt)
						{
							s_textureFormat[jj].m_supported = true;
							fmt = jj;
						}
					}

					BX_TRACE("  %3d: %8x %s", ii, internalFmt, getName( (TextureFormat::Enum)fmt) );
				}
			}

			if (BX_ENABLED(BGFX_CONFIG_DEBUG) )
			{
#define GL_GET(_pname, _min) BX_TRACE("  " #_pname " %d (min: %d)", glGet(_pname), _min)
				BX_TRACE("Defaults:");
#if BGFX_CONFIG_RENDERER_OPENGL >= 41 || BGFX_CONFIG_RENDERER_OPENGLES
				GL_GET(GL_MAX_FRAGMENT_UNIFORM_VECTORS, 16);
				GL_GET(GL_MAX_VERTEX_UNIFORM_VECTORS, 128);
				GL_GET(GL_MAX_VARYING_VECTORS, 8);
#else
				GL_GET(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, 16 * 4);
				GL_GET(GL_MAX_VERTEX_UNIFORM_COMPONENTS, 128 * 4);
				GL_GET(GL_MAX_VARYING_FLOATS, 8 * 4);
#endif // BGFX_CONFIG_RENDERER_OPENGL >= 41 || BGFX_CONFIG_RENDERER_OPENGLES
				GL_GET(GL_MAX_VERTEX_ATTRIBS, 8);
				GL_GET(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, 8);
				GL_GET(GL_MAX_CUBE_MAP_TEXTURE_SIZE, 16);
				GL_GET(GL_MAX_TEXTURE_IMAGE_UNITS, 8);
				GL_GET(GL_MAX_TEXTURE_SIZE, 64);
				GL_GET(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, 0);
				GL_GET(GL_MAX_RENDERBUFFER_SIZE, 1);
#undef GL_GET

				BX_TRACE("      Vendor: %s", m_vendor);
				BX_TRACE("    Renderer: %s", m_renderer);
				BX_TRACE("     Version: %s", m_version);
				BX_TRACE("GLSL version: %s", m_glslVersion);
			}

			// Initial binary shader hash depends on driver version.
			m_hash = ( (BX_PLATFORM_WINDOWS<<1) | BX_ARCH_64BIT)
				^ (uint64_t(getGLStringHash(GL_VENDOR  ) )<<32)
				^ (uint64_t(getGLStringHash(GL_RENDERER) )<<0 )
				^ (uint64_t(getGLStringHash(GL_VERSION ) )<<16)
				;

			if (BX_ENABLED(BGFX_CONFIG_RENDERER_USE_EXTENSIONS) )
			{
				const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
				glGetError(); // ignore error if glGetString returns NULL.
				if (NULL != extensions)
				{
					char name[1024];
					const char* pos = extensions;
					const char* end = extensions + strlen(extensions);
					uint32_t index = 0;
					while (pos < end)
					{
						uint32_t len;
						const char* space = strchr(pos, ' ');
						if (NULL != space)
						{
							len = bx::uint32_min(sizeof(name), (uint32_t)(space - pos) );
						}
						else
						{
							len = bx::uint32_min(sizeof(name), (uint32_t)strlen(pos) );
						}

						strncpy(name, pos, len);
						name[len] = '\0';

						bool supported = false;
						for (uint32_t ii = 0; ii < Extension::Count; ++ii)
						{
							Extension& extension = s_extension[ii];
							if (!extension.m_supported
							&&  extension.m_initialize)
							{
								const char* ext = name;
								if (0 == strncmp(ext, "GL_", 3) ) // skip GL_
								{
									ext += 3;
								}

								if (0 == strcmp(ext, extension.m_name) )
								{
									extension.m_supported = true;
									supported = true;
									break;
								}
							}
						}

						BX_TRACE("GL_EXTENSION %3d%s: %s", index, supported ? " (supported)" : "", name);
						BX_UNUSED(supported);

						pos += len+1;
						++index;
					}

					BX_TRACE("Supported extensions:");
					for (uint32_t ii = 0; ii < Extension::Count; ++ii)
					{
						if (s_extension[ii].m_supported)
						{
							BX_TRACE("\t%2d: %s", ii, s_extension[ii].m_name);
						}
					}
				}
			}

			bool bc123Supported = 0
				|| s_extension[Extension::EXT_texture_compression_s3tc        ].m_supported
				|| s_extension[Extension::MOZ_WEBGL_compressed_texture_s3tc   ].m_supported
				|| s_extension[Extension::WEBGL_compressed_texture_s3tc       ].m_supported
				|| s_extension[Extension::WEBKIT_WEBGL_compressed_texture_s3tc].m_supported
				;
			s_textureFormat[TextureFormat::BC1].m_supported |= bc123Supported
				|| s_extension[Extension::ANGLE_texture_compression_dxt1].m_supported
				|| s_extension[Extension::EXT_texture_compression_dxt1  ].m_supported
				;

			if (!s_textureFormat[TextureFormat::BC1].m_supported
			&& ( s_textureFormat[TextureFormat::BC2].m_supported || s_textureFormat[TextureFormat::BC3].m_supported) )
			{
				// If RGBA_S3TC_DXT1 is not supported, maybe RGB_S3TC_DXT1 is?
				for (GLint ii = 0; ii < numCmpFormats; ++ii)
				{
					if (GL_COMPRESSED_RGB_S3TC_DXT1_EXT == cmpFormat[ii])
					{
						setTextureFormat(TextureFormat::BC1, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_COMPRESSED_RGB_S3TC_DXT1_EXT);
						s_textureFormat[TextureFormat::BC1].m_supported   = true;
						break;
					}
				}
			}

			s_textureFormat[TextureFormat::BC2].m_supported |= bc123Supported
				|| s_extension[Extension::ANGLE_texture_compression_dxt3   ].m_supported
				|| s_extension[Extension::CHROMIUM_texture_compression_dxt3].m_supported
				;

			s_textureFormat[TextureFormat::BC3].m_supported |= bc123Supported
				|| s_extension[Extension::ANGLE_texture_compression_dxt5   ].m_supported
				|| s_extension[Extension::CHROMIUM_texture_compression_dxt5].m_supported
				;

			if (s_extension[Extension::EXT_texture_compression_latc].m_supported)
			{
				setTextureFormat(TextureFormat::BC4, GL_COMPRESSED_LUMINANCE_LATC1_EXT,       GL_COMPRESSED_LUMINANCE_LATC1_EXT);
				setTextureFormat(TextureFormat::BC5, GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT, GL_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT);
			}

			if (s_extension[Extension::ARB_texture_compression_rgtc].m_supported
			||  s_extension[Extension::EXT_texture_compression_rgtc].m_supported)
			{
				setTextureFormat(TextureFormat::BC4, GL_COMPRESSED_RED_RGTC1, GL_COMPRESSED_RED_RGTC1);
				setTextureFormat(TextureFormat::BC5, GL_COMPRESSED_RG_RGTC2,  GL_COMPRESSED_RG_RGTC2);
			}

			bool etc1Supported = 0
				|| s_extension[Extension::OES_compressed_ETC1_RGB8_texture].m_supported
				|| s_extension[Extension::WEBGL_compressed_texture_etc1   ].m_supported
				;
			s_textureFormat[TextureFormat::ETC1].m_supported |= etc1Supported;

			bool etc2Supported = !!(BGFX_CONFIG_RENDERER_OPENGLES >= 30)
				|| s_extension[Extension::ARB_ES3_compatibility].m_supported
				;
			s_textureFormat[TextureFormat::ETC2  ].m_supported |= etc2Supported;
			s_textureFormat[TextureFormat::ETC2A ].m_supported |= etc2Supported;
			s_textureFormat[TextureFormat::ETC2A1].m_supported |= etc2Supported;

			if (!s_textureFormat[TextureFormat::ETC1].m_supported
			&&   s_textureFormat[TextureFormat::ETC2].m_supported)
			{
				// When ETC2 is supported override ETC1 texture format settings.
				s_textureFormat[TextureFormat::ETC1].m_internalFmt = GL_COMPRESSED_RGB8_ETC2;
				s_textureFormat[TextureFormat::ETC1].m_fmt         = GL_COMPRESSED_RGB8_ETC2;
				s_textureFormat[TextureFormat::ETC1].m_supported   = true;
			}

			bool ptc1Supported = 0
				|| s_extension[Extension::IMG_texture_compression_pvrtc ].m_supported
				|| s_extension[Extension::WEBGL_compressed_texture_pvrtc].m_supported
				;
			s_textureFormat[TextureFormat::PTC12 ].m_supported |= ptc1Supported;
			s_textureFormat[TextureFormat::PTC14 ].m_supported |= ptc1Supported;
			s_textureFormat[TextureFormat::PTC12A].m_supported |= ptc1Supported;
			s_textureFormat[TextureFormat::PTC14A].m_supported |= ptc1Supported;

			bool ptc2Supported = s_extension[Extension::IMG_texture_compression_pvrtc2].m_supported;
			s_textureFormat[TextureFormat::PTC22].m_supported |= ptc2Supported;
			s_textureFormat[TextureFormat::PTC24].m_supported |= ptc2Supported;

			if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES) )
			{
				setTextureFormat(TextureFormat::D32, GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);

				if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 30) )
				{
					setTextureFormat(TextureFormat::R16,    GL_R16UI,    GL_RED_INTEGER,  GL_UNSIGNED_SHORT);
					setTextureFormat(TextureFormat::RGBA16, GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT);
				}
				else
				{
					setTextureFormat(TextureFormat::RGBA16F, GL_RGBA, GL_RGBA, GL_HALF_FLOAT);

					if (BX_ENABLED(BX_PLATFORM_IOS) )
					{
						setTextureFormat(TextureFormat::D16,   GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT);
						setTextureFormat(TextureFormat::D24S8, GL_DEPTH_STENCIL,   GL_DEPTH_STENCIL,   GL_UNSIGNED_INT_24_8);
					}
				}
			}

			if (s_extension[Extension::EXT_texture_format_BGRA8888  ].m_supported
			||  s_extension[Extension::EXT_bgra                     ].m_supported
			||  s_extension[Extension::IMG_texture_format_BGRA8888  ].m_supported
			||  s_extension[Extension::APPLE_texture_format_BGRA8888].m_supported)
			{
				if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL) )
				{
					m_readPixelsFmt = GL_BGRA;
				}

				s_textureFormat[TextureFormat::BGRA8].m_fmt = GL_BGRA;

				// Mixing GLES and GL extensions here. OpenGL EXT_bgra and
				// APPLE_texture_format_BGRA8888 wants
				// format to be BGRA but internal format to stay RGBA, but
				// EXT_texture_format_BGRA8888 wants both format and internal
				// format to be BGRA.
				//
				// Reference:
				// https://www.khronos.org/registry/gles/extensions/EXT/EXT_texture_format_BGRA8888.txt
				// https://www.opengl.org/registry/specs/EXT/bgra.txt
				// https://www.khronos.org/registry/gles/extensions/APPLE/APPLE_texture_format_BGRA8888.txt
				if (!s_extension[Extension::EXT_bgra                     ].m_supported
				&&  !s_extension[Extension::APPLE_texture_format_BGRA8888].m_supported)
				{
					s_textureFormat[TextureFormat::BGRA8].m_internalFmt = GL_BGRA;
				}

				if (!isTextureFormatValid(TextureFormat::BGRA8) )
				{
					// Revert back to RGBA if texture can't be created.
					setTextureFormat(TextureFormat::BGRA8, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE);
				}
			}

			if (!BX_ENABLED(BX_PLATFORM_EMSCRIPTEN) )
			{
				for (uint32_t ii = 0; ii < TextureFormat::Count; ++ii)
				{
					if (TextureFormat::Unknown != ii
					&&  TextureFormat::UnknownDepth != ii)
					{
						s_textureFormat[ii].m_supported = isTextureFormatValid( (TextureFormat::Enum)ii);
					}
				}
			}

			for (uint32_t ii = 0; ii < TextureFormat::Count; ++ii)
			{
				g_caps.formats[ii] = s_textureFormat[ii].m_supported ? 1 : 0;
			}

			g_caps.supported |= !!(BGFX_CONFIG_RENDERER_OPENGL || BGFX_CONFIG_RENDERER_OPENGLES >= 30) || s_extension[Extension::OES_texture_3D].m_supported
				? BGFX_CAPS_TEXTURE_3D
				: 0
				;
			g_caps.supported |= !!(BGFX_CONFIG_RENDERER_OPENGL || BGFX_CONFIG_RENDERER_OPENGLES >= 30) || s_extension[Extension::EXT_shadow_samplers].m_supported
				? BGFX_CAPS_TEXTURE_COMPARE_ALL
				: 0
				;
			g_caps.supported |= !!(BGFX_CONFIG_RENDERER_OPENGL || BGFX_CONFIG_RENDERER_OPENGLES >= 30) || s_extension[Extension::OES_vertex_half_float].m_supported
				? BGFX_CAPS_VERTEX_ATTRIB_HALF
				: 0
				;
			g_caps.supported |= !!(BGFX_CONFIG_RENDERER_OPENGL || BGFX_CONFIG_RENDERER_OPENGLES >= 30) || s_extension[Extension::EXT_frag_depth].m_supported
				? BGFX_CAPS_FRAGMENT_DEPTH
				: 0
				;
			g_caps.supported |= s_extension[Extension::ARB_draw_buffers_blend].m_supported
				? BGFX_CAPS_BLEND_INDEPENDENT
				: 0
				;
			g_caps.supported |= s_extension[Extension::INTEL_fragment_shader_ordering].m_supported
				? BGFX_CAPS_FRAGMENT_ORDERING
				: 0
				;

			g_caps.maxTextureSize = glGet(GL_MAX_TEXTURE_SIZE);

			if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL)
			||  BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 30)
			||  s_extension[Extension::EXT_draw_buffers].m_supported)
			{
				g_caps.maxFBAttachments = bx::uint32_min(glGet(GL_MAX_COLOR_ATTACHMENTS), BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS);
			}

			m_vaoSupport = !!(BGFX_CONFIG_RENDERER_OPENGLES >= 30)
				|| s_extension[Extension::ARB_vertex_array_object].m_supported
				|| s_extension[Extension::OES_vertex_array_object].m_supported
				;

			if (BX_ENABLED(BX_PLATFORM_NACL) )
			{
				m_vaoSupport &= NULL != glGenVertexArrays
					&& NULL != glDeleteVertexArrays
					&& NULL != glBindVertexArray
					;
			}

			if (m_vaoSupport)
			{
				GL_CHECK(glGenVertexArrays(1, &m_vao) );
			}

			m_samplerObjectSupport = !!(BGFX_CONFIG_RENDERER_OPENGLES >= 30)
				|| s_extension[Extension::ARB_sampler_objects].m_supported
				;

			m_shadowSamplersSupport = !!(BGFX_CONFIG_RENDERER_OPENGL || BGFX_CONFIG_RENDERER_OPENGLES >= 30)
				|| s_extension[Extension::EXT_shadow_samplers].m_supported
				;

			m_programBinarySupport = !!(BGFX_CONFIG_RENDERER_OPENGLES >= 30)
				|| s_extension[Extension::ARB_get_program_binary].m_supported
				|| s_extension[Extension::OES_get_program_binary].m_supported
				|| s_extension[Extension::IMG_shader_binary     ].m_supported
				;

			m_textureSwizzleSupport = false
				|| s_extension[Extension::ARB_texture_swizzle].m_supported
				|| s_extension[Extension::EXT_texture_swizzle].m_supported
				;

			m_depthTextureSupport = !!(BGFX_CONFIG_RENDERER_OPENGL || BGFX_CONFIG_RENDERER_OPENGLES >= 30)
				|| s_extension[Extension::ANGLE_depth_texture       ].m_supported
				|| s_extension[Extension::CHROMIUM_depth_texture    ].m_supported
				|| s_extension[Extension::GOOGLE_depth_texture      ].m_supported
				|| s_extension[Extension::OES_depth_texture         ].m_supported
				|| s_extension[Extension::MOZ_WEBGL_depth_texture   ].m_supported
				|| s_extension[Extension::WEBGL_depth_texture       ].m_supported
				|| s_extension[Extension::WEBKIT_WEBGL_depth_texture].m_supported
				;

			g_caps.supported |= m_depthTextureSupport 
				? BGFX_CAPS_TEXTURE_COMPARE_LEQUAL
				: 0
				;

			g_caps.supported |= !!(BGFX_CONFIG_RENDERER_OPENGLES >= 31)
				|| s_extension[Extension::ARB_compute_shader].m_supported
				? BGFX_CAPS_COMPUTE
				: 0
				;

			g_caps.supported |= GlContext::isSwapChainSupported()
				? BGFX_CAPS_SWAP_CHAIN
				: 0
				;

			if (s_extension[Extension::EXT_texture_filter_anisotropic].m_supported)
			{
				GL_CHECK(glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &m_maxAnisotropy) );
			}

			if (s_extension[Extension::ARB_texture_multisample].m_supported
			||  s_extension[Extension::ANGLE_framebuffer_multisample].m_supported)
			{
				GL_CHECK(glGetIntegerv(GL_MAX_SAMPLES, &m_maxMsaa) );
			}

			if (s_extension[Extension::OES_read_format].m_supported
			&& (s_extension[Extension::IMG_read_format].m_supported	|| s_extension[Extension::EXT_read_format_bgra].m_supported) )
			{
				m_readPixelsFmt = GL_BGRA;
			}
			else
			{
				m_readPixelsFmt = GL_RGBA;
			}

			if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 30) )
			{
				g_caps.supported |= BGFX_CAPS_INSTANCING;
			}
			else
			{
				if (!BX_ENABLED(BX_PLATFORM_IOS) )
				{
					if (s_extension[Extension::ARB_instanced_arrays].m_supported
					||  s_extension[Extension::ANGLE_instanced_arrays].m_supported)
					{
						if (NULL != glVertexAttribDivisor
						&&  NULL != glDrawArraysInstanced
						&&  NULL != glDrawElementsInstanced)
						{
							g_caps.supported |= BGFX_CAPS_INSTANCING;
						}
					}
				}

				if (0 == (g_caps.supported & BGFX_CAPS_INSTANCING) )
				{
					glVertexAttribDivisor   = stubVertexAttribDivisor;
					glDrawArraysInstanced   = stubDrawArraysInstanced;
					glDrawElementsInstanced = stubDrawElementsInstanced;
				}
			}

			if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL >= 31)
			||  BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 30) )
			{
				s_textureFormat[TextureFormat::R8].m_internalFmt = GL_R8;
				s_textureFormat[TextureFormat::R8].m_fmt         = GL_RED;
			}

#if BGFX_CONFIG_RENDERER_OPENGL
			if (s_extension[Extension::ARB_debug_output].m_supported
			||  s_extension[Extension::KHR_debug].m_supported)
			{
				GL_CHECK(glDebugMessageCallback(debugProcCb, NULL) );
				GL_CHECK(glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_MEDIUM_ARB, 0, NULL, GL_TRUE) );
			}

			if (s_extension[Extension::ARB_seamless_cube_map].m_supported)
			{
				GL_CHECK(glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS) );
			}

			if (s_extension[Extension::ARB_depth_clamp].m_supported)
			{
				GL_CHECK(glEnable(GL_DEPTH_CLAMP) );
			}
#endif // BGFX_CONFIG_RENDERER_OPENGL

			if (NULL == glFrameTerminatorGREMEDY
			||  !s_extension[Extension::GREMEDY_frame_terminator].m_supported)
			{
				glFrameTerminatorGREMEDY = stubFrameTerminatorGREMEDY;
			}

			if (NULL == glInsertEventMarker
			||  !s_extension[Extension::EXT_debug_marker].m_supported)
			{
				glInsertEventMarker = (NULL != glStringMarkerGREMEDY && s_extension[Extension::GREMEDY_string_marker].m_supported)
					? stubInsertEventMarkerGREMEDY
					: stubInsertEventMarker
					;
			}

			if (NULL == glObjectLabel)
			{
				glObjectLabel = stubObjectLabel;
			}

			if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL) )
			{
				m_queries.create();
			}
		}

		~RendererContextGL()
		{
			if (m_vaoSupport)
			{
				GL_CHECK(glBindVertexArray(0) );
				GL_CHECK(glDeleteVertexArrays(1, &m_vao) );
				m_vao = 0;
			}

			captureFinish();

			invalidateCache();

			if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL) )
			{
				m_queries.destroy();
			}

			destroyMsaaFbo();
			m_glctx.destroy();

			m_flip = false;
		}

		RendererType::Enum getRendererType() const BX_OVERRIDE
		{
			if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL) )
			{
				return RendererType::OpenGL;
			}

			return RendererType::OpenGLES;
		}

		const char* getRendererName() const BX_OVERRIDE
		{
			return BGFX_RENDERER_OPENGL_NAME;
		}

		void flip()
		{
			if (m_flip)
			{
				for (uint32_t ii = 1, num = m_numWindows; ii < num; ++ii)
				{
					m_glctx.swap(m_frameBuffers[m_windows[ii].idx].m_swapChain);
				}
				m_glctx.swap();
			}
		}

		void createIndexBuffer(IndexBufferHandle _handle, Memory* _mem) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].create(_mem->size, _mem->data);
		}

		void destroyIndexBuffer(IndexBufferHandle _handle) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].destroy();
		}

		void createVertexDecl(VertexDeclHandle _handle, const VertexDecl& _decl) BX_OVERRIDE
		{
			VertexDecl& decl = m_vertexDecls[_handle.idx];
			memcpy(&decl, &_decl, sizeof(VertexDecl) );
			dump(decl);
		}

		void destroyVertexDecl(VertexDeclHandle /*_handle*/) BX_OVERRIDE
		{
		}

		void createVertexBuffer(VertexBufferHandle _handle, Memory* _mem, VertexDeclHandle _declHandle) BX_OVERRIDE
		{
			m_vertexBuffers[_handle.idx].create(_mem->size, _mem->data, _declHandle);
		}

		void destroyVertexBuffer(VertexBufferHandle _handle) BX_OVERRIDE
		{
			m_vertexBuffers[_handle.idx].destroy();
		}

		void createDynamicIndexBuffer(IndexBufferHandle _handle, uint32_t _size) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].create(_size, NULL);
		}

		void updateDynamicIndexBuffer(IndexBufferHandle _handle, uint32_t _offset, uint32_t _size, Memory* _mem) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].update(_offset, bx::uint32_min(_size, _mem->size), _mem->data);
		}

		void destroyDynamicIndexBuffer(IndexBufferHandle _handle) BX_OVERRIDE
		{
			m_indexBuffers[_handle.idx].destroy();
		}

		void createDynamicVertexBuffer(VertexBufferHandle _handle, uint32_t _size) BX_OVERRIDE
		{
			VertexDeclHandle decl = BGFX_INVALID_HANDLE;
			m_vertexBuffers[_handle.idx].create(_size, NULL, decl);
		}

		void updateDynamicVertexBuffer(VertexBufferHandle _handle, uint32_t _offset, uint32_t _size, Memory* _mem) BX_OVERRIDE
		{
			m_vertexBuffers[_handle.idx].update(_offset, bx::uint32_min(_size, _mem->size), _mem->data);
		}

		void destroyDynamicVertexBuffer(VertexBufferHandle _handle) BX_OVERRIDE
		{
			m_vertexBuffers[_handle.idx].destroy();
		}

		void createShader(ShaderHandle _handle, Memory* _mem) BX_OVERRIDE
		{
			m_shaders[_handle.idx].create(_mem);
		}

		void destroyShader(ShaderHandle _handle) BX_OVERRIDE
		{
			m_shaders[_handle.idx].destroy();
		}

		void createProgram(ProgramHandle _handle, ShaderHandle _vsh, ShaderHandle _fsh) BX_OVERRIDE
		{
			ShaderGL dummyFragmentShader;
			m_program[_handle.idx].create(m_shaders[_vsh.idx], isValid(_fsh) ? m_shaders[_fsh.idx] : dummyFragmentShader);
		}

		void destroyProgram(ProgramHandle _handle) BX_OVERRIDE
		{
			m_program[_handle.idx].destroy();
		}

		void createTexture(TextureHandle _handle, Memory* _mem, uint32_t _flags, uint8_t _skip) BX_OVERRIDE
		{
			m_textures[_handle.idx].create(_mem, _flags, _skip);
		}

		void updateTextureBegin(TextureHandle /*_handle*/, uint8_t /*_side*/, uint8_t /*_mip*/) BX_OVERRIDE
		{
		}

		void updateTexture(TextureHandle _handle, uint8_t _side, uint8_t _mip, const Rect& _rect, uint16_t _z, uint16_t _depth, uint16_t _pitch, const Memory* _mem) BX_OVERRIDE
		{
			m_textures[_handle.idx].update(_side, _mip, _rect, _z, _depth, _pitch, _mem);
		}

		void updateTextureEnd() BX_OVERRIDE
		{
		}

		void destroyTexture(TextureHandle _handle) BX_OVERRIDE
		{
			m_textures[_handle.idx].destroy();
		}

		void createFrameBuffer(FrameBufferHandle _handle, uint8_t _num, const TextureHandle* _textureHandles) BX_OVERRIDE
		{
			m_frameBuffers[_handle.idx].create(_num, _textureHandles);
		}

		void createFrameBuffer(FrameBufferHandle _handle, void* _nwh, uint32_t _width, uint32_t _height, TextureFormat::Enum _depthFormat) BX_OVERRIDE
		{
			uint16_t denseIdx = m_numWindows++;
			m_windows[denseIdx] = _handle;
			m_frameBuffers[_handle.idx].create(denseIdx, _nwh, _width, _height, _depthFormat);
		}

		void destroyFrameBuffer(FrameBufferHandle _handle) BX_OVERRIDE
		{
			uint16_t denseIdx = m_frameBuffers[_handle.idx].destroy();
			if (UINT16_MAX != denseIdx)
			{
				--m_numWindows;
				if (m_numWindows > 1)
				{
					FrameBufferHandle handle = m_windows[m_numWindows];
					m_windows[denseIdx] = handle;
					m_frameBuffers[handle.idx].m_denseIdx = denseIdx;
				}
			}
		}

		void createUniform(UniformHandle _handle, UniformType::Enum _type, uint16_t _num, const char* _name) BX_OVERRIDE
		{
			if (NULL != m_uniforms[_handle.idx])
			{
				BX_FREE(g_allocator, m_uniforms[_handle.idx]);
			}

			uint32_t size = g_uniformTypeSize[_type]*_num;
			void* data = BX_ALLOC(g_allocator, size);
			memset(data, 0, size);
			m_uniforms[_handle.idx] = data;
			m_uniformReg.add(_handle, _name, m_uniforms[_handle.idx]);
		}

		void destroyUniform(UniformHandle _handle) BX_OVERRIDE
		{
			BX_FREE(g_allocator, m_uniforms[_handle.idx]);
			m_uniforms[_handle.idx] = NULL;
		}

		void saveScreenShot(const char* _filePath) BX_OVERRIDE
		{
			uint32_t length = m_resolution.m_width*m_resolution.m_height*4;
			uint8_t* data = (uint8_t*)BX_ALLOC(g_allocator, length);

			uint32_t width = m_resolution.m_width;
			uint32_t height = m_resolution.m_height;

			GL_CHECK(glReadPixels(0
				, 0
				, width
				, height
				, m_readPixelsFmt
				, GL_UNSIGNED_BYTE
				, data
				) );

			if (GL_RGBA == m_readPixelsFmt)
			{
				imageSwizzleBgra8(width, height, width*4, data, data);
			}

			g_callback->screenShot(_filePath
				, width
				, height
				, width*4
				, data
				, length
				, true
				);
			BX_FREE(g_allocator, data);
		}

		void updateViewName(uint8_t _id, const char* _name) BX_OVERRIDE
		{
			bx::strlcpy(&s_viewName[_id][0], _name, BX_COUNTOF(s_viewName[0]) );
		}

		void updateUniform(uint16_t _loc, const void* _data, uint32_t _size) BX_OVERRIDE
		{
			memcpy(m_uniforms[_loc], _data, _size);
		}

		void setMarker(const char* _marker, uint32_t _size) BX_OVERRIDE
		{
			GL_CHECK(glInsertEventMarker(_size, _marker) );
		}

		void submit(Frame* _render, ClearQuad& _clearQuad, TextVideoMemBlitter& _textVideoMemBlitter) BX_OVERRIDE;

		void blitSetup(TextVideoMemBlitter& _blitter) BX_OVERRIDE
		{
			if (0 != m_vao)
			{
				GL_CHECK(glBindVertexArray(m_vao) );
			}

			uint32_t width = m_resolution.m_width;
			uint32_t height = m_resolution.m_height;

			GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, m_backBufferFbo) );
			GL_CHECK(glViewport(0, 0, width, height) );

			GL_CHECK(glDisable(GL_SCISSOR_TEST) );
			GL_CHECK(glDisable(GL_STENCIL_TEST) );
			GL_CHECK(glDisable(GL_DEPTH_TEST) );
			GL_CHECK(glDepthFunc(GL_ALWAYS) );
			GL_CHECK(glDisable(GL_CULL_FACE) );
			GL_CHECK(glDisable(GL_BLEND) );
			GL_CHECK(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE) );

			ProgramGL& program = m_program[_blitter.m_program.idx];
			GL_CHECK(glUseProgram(program.m_id) );
			GL_CHECK(glUniform1i(program.m_sampler[0], 0) );

			float proj[16];
			mtxOrtho(proj, 0.0f, (float)width, (float)height, 0.0f, 0.0f, 1000.0f);

			GL_CHECK(glUniformMatrix4fv(program.m_predefined[0].m_loc
				, 1
				, GL_FALSE
				, proj
				) );

			GL_CHECK(glActiveTexture(GL_TEXTURE0) );
			GL_CHECK(glBindTexture(GL_TEXTURE_2D, m_textures[_blitter.m_texture.idx].m_id) );
		}

		void blitRender(TextVideoMemBlitter& _blitter, uint32_t _numIndices) BX_OVERRIDE
		{
			uint32_t numVertices = _numIndices*4/6;
			m_indexBuffers[_blitter.m_ib->handle.idx].update(0, _numIndices*2, _blitter.m_ib->data);
			m_vertexBuffers[_blitter.m_vb->handle.idx].update(0, numVertices*_blitter.m_decl.m_stride, _blitter.m_vb->data);

			VertexBufferGL& vb = m_vertexBuffers[_blitter.m_vb->handle.idx];
			GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, vb.m_id) );

			IndexBufferGL& ib = m_indexBuffers[_blitter.m_ib->handle.idx];
			GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.m_id) );

			ProgramGL& program = m_program[_blitter.m_program.idx];
			program.bindAttributes(_blitter.m_decl, 0);

			GL_CHECK(glDrawElements(GL_TRIANGLES
				, _numIndices
				, GL_UNSIGNED_SHORT
				, (void*)0
				) );
		}

		void updateResolution(const Resolution& _resolution)
		{
			if (m_resolution.m_width != _resolution.m_width
			||  m_resolution.m_height != _resolution.m_height
			||  m_resolution.m_flags != _resolution.m_flags)
			{
				m_textVideoMem.resize(false, _resolution.m_width, _resolution.m_height);
				m_textVideoMem.clear();

				m_resolution = _resolution;

				uint32_t msaa = (m_resolution.m_flags&BGFX_RESET_MSAA_MASK)>>BGFX_RESET_MSAA_SHIFT;
				msaa = bx::uint32_min(m_maxMsaa, msaa == 0 ? 0 : 1<<msaa);
				bool vsync = !!(m_resolution.m_flags&BGFX_RESET_VSYNC);
				setRenderContextSize(_resolution.m_width, _resolution.m_height, msaa, vsync);
				updateCapture();
			}
		}

		uint32_t setFrameBuffer(FrameBufferHandle _fbh, uint32_t _height, bool _msaa = true)
		{
			if (isValid(m_fbh)
			&&  m_fbh.idx != _fbh.idx
			&&  m_rtMsaa)
			{
				FrameBufferGL& frameBuffer = m_frameBuffers[m_fbh.idx];
				frameBuffer.resolve();
			}

			m_glctx.makeCurrent(NULL);

			if (!isValid(_fbh) )
			{
				GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, m_msaaBackBufferFbo) );
			}
			else
			{
				FrameBufferGL& frameBuffer = m_frameBuffers[_fbh.idx];
				_height = frameBuffer.m_height;
				if (UINT16_MAX != frameBuffer.m_denseIdx)
				{
					m_glctx.makeCurrent(frameBuffer.m_swapChain);
					GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, 0) );
				}
				else
				{
					m_glctx.makeCurrent(NULL);
					GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer.m_fbo[0]) );
				}
			}

			m_fbh = _fbh;
			m_rtMsaa = _msaa;

			return _height;
		}

		uint32_t getNumRt() const
		{
			if (isValid(m_fbh) )
			{
				const FrameBufferGL& frameBuffer = m_frameBuffers[m_fbh.idx];
				return frameBuffer.m_num;
			}

			return 1;
		}

		void createMsaaFbo(uint32_t _width, uint32_t _height, uint32_t _msaa)
		{
			if (0 == m_msaaBackBufferFbo // iOS
			&&  1 < _msaa)
			{
				GL_CHECK(glGenFramebuffers(1, &m_msaaBackBufferFbo) );
				GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, m_msaaBackBufferFbo) );
				GL_CHECK(glGenRenderbuffers(BX_COUNTOF(m_msaaBackBufferRbos), m_msaaBackBufferRbos) );
				GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, m_msaaBackBufferRbos[0]) );
				GL_CHECK(glRenderbufferStorageMultisample(GL_RENDERBUFFER, _msaa, GL_RGBA8, _width, _height) );
				GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, m_msaaBackBufferRbos[1]) );
				GL_CHECK(glRenderbufferStorageMultisample(GL_RENDERBUFFER, _msaa, GL_DEPTH24_STENCIL8, _width, _height) );
				GL_CHECK(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaBackBufferRbos[0]) );

				GLenum attachment = BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL) || BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 30) 
					? GL_DEPTH_STENCIL_ATTACHMENT
					: GL_DEPTH_ATTACHMENT 
					;
				GL_CHECK(glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, m_msaaBackBufferRbos[1]) );

				BX_CHECK(GL_FRAMEBUFFER_COMPLETE ==  glCheckFramebufferStatus(GL_FRAMEBUFFER)
					, "glCheckFramebufferStatus failed 0x%08x"
					, glCheckFramebufferStatus(GL_FRAMEBUFFER)
					);

				GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, m_msaaBackBufferFbo) );
			}
		}

		void destroyMsaaFbo()
		{
			if (m_backBufferFbo != m_msaaBackBufferFbo // iOS
			&&  0 != m_msaaBackBufferFbo)
			{
				GL_CHECK(glDeleteFramebuffers(1, &m_msaaBackBufferFbo) );
				GL_CHECK(glDeleteRenderbuffers(BX_COUNTOF(m_msaaBackBufferRbos), m_msaaBackBufferRbos) );
				m_msaaBackBufferFbo = 0;
			}
		}

		void blitMsaaFbo()
		{
			if (m_backBufferFbo != m_msaaBackBufferFbo // iOS
			&&  0 != m_msaaBackBufferFbo)
			{
				GL_CHECK(glDisable(GL_SCISSOR_TEST) );
				GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, m_backBufferFbo) );
				GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaBackBufferFbo) );
				GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0) );
				uint32_t width = m_resolution.m_width;
				uint32_t height = m_resolution.m_height;
				GLenum filter = BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL) || BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES < 30) 
					? GL_NEAREST 
					: GL_LINEAR
					;
				GL_CHECK(glBlitFramebuffer(0
					, 0
					, width
					, height
					, 0
					, 0
					, width
					, height
					, GL_COLOR_BUFFER_BIT
					, filter
					) );
				GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, m_backBufferFbo) );
			}
		}

		void setRenderContextSize(uint32_t _width, uint32_t _height, uint32_t _msaa = 0, bool _vsync = false)
		{
			if (_width  != 0
			||  _height != 0)
			{
				if (!m_glctx.isValid() )
				{
					m_glctx.create(_width, _height);

#if BX_PLATFORM_IOS
					// iOS: need to figure out how to deal with FBO created by context.
					m_backBufferFbo = m_msaaBackBufferFbo = m_glctx.getFbo();
#endif // BX_PLATFORM_IOS
				}
				else
				{
					destroyMsaaFbo();

					m_glctx.resize(_width, _height, _vsync);

					createMsaaFbo(_width, _height, _msaa);
				}
			}

			m_flip = true;
		}

		void invalidateCache()
		{
			if (m_vaoSupport)
			{
				m_vaoStateCache.invalidate();
			}

			if ( (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL) ||  BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 30) )
			&&  m_samplerObjectSupport)
			{
				m_samplerStateCache.invalidate();
			}
		}

		void setSamplerState(uint32_t _stage, uint32_t _numMips, uint32_t _flags)
		{
			if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL)
			||  BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 30) )
			{
				if (0 == (BGFX_SAMPLER_DEFAULT_FLAGS & _flags) )
				{
					_flags &= ~BGFX_TEXTURE_RESERVED_MASK;
					_flags &= BGFX_TEXTURE_SAMPLER_BITS_MASK;
					_flags |= _numMips<<BGFX_TEXTURE_RESERVED_SHIFT;
					GLuint sampler = m_samplerStateCache.find(_flags);

					if (UINT32_MAX == sampler)
					{
						sampler = m_samplerStateCache.add(_flags);

						GL_CHECK(glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, s_textureAddress[(_flags&BGFX_TEXTURE_U_MASK)>>BGFX_TEXTURE_U_SHIFT]) );
						GL_CHECK(glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, s_textureAddress[(_flags&BGFX_TEXTURE_V_MASK)>>BGFX_TEXTURE_V_SHIFT]) );
						GL_CHECK(glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, s_textureAddress[(_flags&BGFX_TEXTURE_W_MASK)>>BGFX_TEXTURE_W_SHIFT]) );

						const uint32_t mag = (_flags&BGFX_TEXTURE_MAG_MASK)>>BGFX_TEXTURE_MAG_SHIFT;
						const uint32_t min = (_flags&BGFX_TEXTURE_MIN_MASK)>>BGFX_TEXTURE_MIN_SHIFT;
						const uint32_t mip = (_flags&BGFX_TEXTURE_MIP_MASK)>>BGFX_TEXTURE_MIP_SHIFT;
						GLenum minFilter = s_textureFilterMin[min][1 < _numMips ? mip+1 : 0];
						GL_CHECK(glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, s_textureFilterMag[mag]) );
						GL_CHECK(glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, minFilter) );
						if (0 != (_flags & (BGFX_TEXTURE_MIN_ANISOTROPIC|BGFX_TEXTURE_MAG_ANISOTROPIC) )
						&&  0.0f < m_maxAnisotropy)
						{
							GL_CHECK(glSamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY_EXT, m_maxAnisotropy) );
						}

						if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 30)
						||  m_shadowSamplersSupport)
						{
							const uint32_t cmpFunc = (_flags&BGFX_TEXTURE_COMPARE_MASK)>>BGFX_TEXTURE_COMPARE_SHIFT;
							if (0 == cmpFunc)
							{
								GL_CHECK(glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_NONE) );
							}
							else
							{
								GL_CHECK(glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE) );
								GL_CHECK(glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_FUNC, s_cmpFunc[cmpFunc]) );
							}
						}
					}

					GL_CHECK(glBindSampler(_stage, sampler) );
				}
				else
				{
					GL_CHECK(glBindSampler(_stage, 0) );
				}
			}
		}

		void updateCapture()
		{
			if (m_resolution.m_flags&BGFX_RESET_CAPTURE)
			{
				m_captureSize = m_resolution.m_width*m_resolution.m_height*4;
				m_capture = BX_REALLOC(g_allocator, m_capture, m_captureSize);
				g_callback->captureBegin(m_resolution.m_width, m_resolution.m_height, m_resolution.m_width*4, TextureFormat::BGRA8, true);
			}
			else
			{
				captureFinish();
			}
		}

		void capture()
		{
			if (NULL != m_capture)
			{
				GL_CHECK(glReadPixels(0
					, 0
					, m_resolution.m_width
					, m_resolution.m_height
					, m_readPixelsFmt
					, GL_UNSIGNED_BYTE
					, m_capture
					) );

				g_callback->captureFrame(m_capture, m_captureSize);
			}
		}

		void captureFinish()
		{
			if (NULL != m_capture)
			{
				g_callback->captureEnd();
				BX_FREE(g_allocator, m_capture);
				m_capture = NULL;
				m_captureSize = 0;
			}
		}

		bool programFetchFromCache(GLuint programId, uint64_t _id)
		{
			_id ^= m_hash;

			bool cached = false;

			if (m_programBinarySupport)
			{
				uint32_t length = g_callback->cacheReadSize(_id);
				cached = length > 0;

				if (cached)
				{
					void* data = BX_ALLOC(g_allocator, length);
					if (g_callback->cacheRead(_id, data, length) )
					{
						bx::MemoryReader reader(data, length);

						GLenum format;
						bx::read(&reader, format);

						GL_CHECK(glProgramBinary(programId, format, reader.getDataPtr(), (GLsizei)reader.remaining() ) );
					}

					BX_FREE(g_allocator, data);
				}

#if BGFX_CONFIG_RENDERER_OPENGL
				GL_CHECK(glProgramParameteri(programId, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE) );
#endif // BGFX_CONFIG_RENDERER_OPENGL
			}

			return cached;
		}

		void programCache(GLuint programId, uint64_t _id)
		{
			_id ^= m_hash;

			if (m_programBinarySupport)
			{
				GLint programLength;
				GLenum format;
				GL_CHECK(glGetProgramiv(programId, GL_PROGRAM_BINARY_LENGTH, &programLength) );

				if (0 < programLength)
				{
					uint32_t length = programLength + 4;
					uint8_t* data = (uint8_t*)BX_ALLOC(g_allocator, length);
					GL_CHECK(glGetProgramBinary(programId, programLength, NULL, &format, &data[4]) );
					*(uint32_t*)data = format;

					g_callback->cacheWrite(_id, data, length);

					BX_FREE(g_allocator, data);
				}
			}
		}

		void commit(ConstantBuffer& _constantBuffer)
		{
			_constantBuffer.reset();

			for (;;)
			{
				uint32_t opcode = _constantBuffer.read();

				if (UniformType::End == opcode)
				{
					break;
				}

				UniformType::Enum type;
				uint16_t ignore;
				uint16_t num;
				uint16_t copy;
				ConstantBuffer::decodeOpcode(opcode, type, ignore, num, copy);

				const char* data;
				if (copy)
				{
					data = _constantBuffer.read(g_uniformTypeSize[type]*num);
				}
				else
				{
					UniformHandle handle;
					memcpy(&handle, _constantBuffer.read(sizeof(UniformHandle) ), sizeof(UniformHandle) );
					data = (const char*)m_uniforms[handle.idx];
				}

				uint32_t loc = _constantBuffer.read();

#define CASE_IMPLEMENT_UNIFORM(_uniform, _glsuffix, _dxsuffix, _type) \
		case UniformType::_uniform: \
				{ \
					_type* value = (_type*)data; \
					GL_CHECK(glUniform##_glsuffix(loc, num, value) ); \
				} \
				break;

#define CASE_IMPLEMENT_UNIFORM_T(_uniform, _glsuffix, _dxsuffix, _type) \
		case UniformType::_uniform: \
				{ \
					_type* value = (_type*)data; \
					GL_CHECK(glUniform##_glsuffix(loc, num, GL_FALSE, value) ); \
				} \
				break;

				switch (type)
				{
//				case ConstantType::Uniform1iv:
//					{
//						int* value = (int*)data;
//						BX_TRACE("Uniform1iv sampler %d, loc %d (num %d, copy %d)", *value, loc, num, copy);
//						GL_CHECK(glUniform1iv(loc, num, value) );
//					}
//					break;

					CASE_IMPLEMENT_UNIFORM(Uniform1i, 1iv, I, int);
					CASE_IMPLEMENT_UNIFORM(Uniform1f, 1fv, F, float);
					CASE_IMPLEMENT_UNIFORM(Uniform1iv, 1iv, I, int);
					CASE_IMPLEMENT_UNIFORM(Uniform1fv, 1fv, F, float);
					CASE_IMPLEMENT_UNIFORM(Uniform2fv, 2fv, F, float);
					CASE_IMPLEMENT_UNIFORM(Uniform3fv, 3fv, F, float);
					CASE_IMPLEMENT_UNIFORM(Uniform4fv, 4fv, F, float);
					CASE_IMPLEMENT_UNIFORM_T(Uniform3x3fv, Matrix3fv, F, float);
					CASE_IMPLEMENT_UNIFORM_T(Uniform4x4fv, Matrix4fv, F, float);

				case UniformType::End:
					break;

				default:
					BX_TRACE("%4d: INVALID 0x%08x, t %d, l %d, n %d, c %d", _constantBuffer.getPos(), opcode, type, loc, num, copy);
					break;
				}

#undef CASE_IMPLEMENT_UNIFORM
#undef CASE_IMPLEMENT_UNIFORM_T

			}
		}

		void clearQuad(ClearQuad& _clearQuad, const Rect& _rect, const Clear& _clear, uint32_t _height, const float _palette[][4])
		{
			uint32_t numMrt = 1;
			FrameBufferHandle fbh = m_fbh;
			if (isValid(fbh) )
			{
				const FrameBufferGL& fb = m_frameBuffers[fbh.idx];
				numMrt = bx::uint32_max(1, fb.m_num);
			}

			if (1 == numMrt)
			{
				GLuint flags = 0;
				if (BGFX_CLEAR_COLOR_BIT & _clear.m_flags)
				{
					if (BGFX_CLEAR_COLOR_USE_PALETTE_BIT & _clear.m_flags)
					{
						uint8_t index = (uint8_t)bx::uint32_min(BGFX_CONFIG_MAX_CLEAR_COLOR_PALETTE-1, _clear.m_index[0]);
						const float* rgba = _palette[index];
						const float rr = rgba[0];
						const float gg = rgba[1];
						const float bb = rgba[2];
						const float aa = rgba[3];
						GL_CHECK(glClearColor(rr, gg, bb, aa) );
					}
					else
					{
						float rr = _clear.m_index[0]*1.0f/255.0f;
						float gg = _clear.m_index[1]*1.0f/255.0f;
						float bb = _clear.m_index[2]*1.0f/255.0f;
						float aa = _clear.m_index[3]*1.0f/255.0f;
						GL_CHECK(glClearColor(rr, gg, bb, aa) );
					}

					flags |= GL_COLOR_BUFFER_BIT;
					GL_CHECK(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE) );
				}

				if (BGFX_CLEAR_DEPTH_BIT & _clear.m_flags)
				{
					flags |= GL_DEPTH_BUFFER_BIT;
					GL_CHECK(glClearDepth(_clear.m_depth) );
					GL_CHECK(glDepthMask(GL_TRUE) );
				}

				if (BGFX_CLEAR_STENCIL_BIT & _clear.m_flags)
				{
					flags |= GL_STENCIL_BUFFER_BIT;
					GL_CHECK(glClearStencil(_clear.m_stencil) );
				}

				if (0 != flags)
				{
					GL_CHECK(glEnable(GL_SCISSOR_TEST) );
					GL_CHECK(glScissor(_rect.m_x, _height-_rect.m_height-_rect.m_y, _rect.m_width, _rect.m_height) );
					GL_CHECK(glClear(flags) );
					GL_CHECK(glDisable(GL_SCISSOR_TEST) );
				}
			}
			else
			{
				const GLuint defaultVao = m_vao;
				if (0 != defaultVao)
				{
					GL_CHECK(glBindVertexArray(defaultVao) );
				}

				GL_CHECK(glDisable(GL_SCISSOR_TEST) );
				GL_CHECK(glDisable(GL_CULL_FACE) );
				GL_CHECK(glDisable(GL_BLEND) );

				GLboolean colorMask = !!(BGFX_CLEAR_COLOR_BIT & _clear.m_flags);
				GL_CHECK(glColorMask(colorMask, colorMask, colorMask, colorMask) );

				if (BGFX_CLEAR_DEPTH_BIT & _clear.m_flags)
				{
					GL_CHECK(glEnable(GL_DEPTH_TEST) );
					GL_CHECK(glDepthFunc(GL_ALWAYS) );
					GL_CHECK(glDepthMask(GL_TRUE) );
				}
				else
				{
					GL_CHECK(glDisable(GL_DEPTH_TEST) );
				}

				if (BGFX_CLEAR_STENCIL_BIT & _clear.m_flags)
				{
					GL_CHECK(glEnable(GL_STENCIL_TEST) );
					GL_CHECK(glStencilFuncSeparate(GL_FRONT_AND_BACK, GL_ALWAYS, _clear.m_stencil,  0xff) );
					GL_CHECK(glStencilOpSeparate(GL_FRONT_AND_BACK, GL_REPLACE, GL_REPLACE, GL_REPLACE) );
				}
				else
				{
					GL_CHECK(glDisable(GL_STENCIL_TEST) );
				}

				VertexBufferGL& vb = m_vertexBuffers[_clearQuad.m_vb->handle.idx];
				VertexDecl& vertexDecl = m_vertexDecls[_clearQuad.m_vb->decl.idx];

				{
					struct Vertex
					{
						float m_x;
						float m_y;
						float m_z;
					};
					
					Vertex* vertex = (Vertex*)_clearQuad.m_vb->data;
					BX_CHECK(vertexDecl.m_stride == sizeof(Vertex), "Stride/Vertex mismatch (stride %d, sizeof(Vertex) %d)", vertexDecl.m_stride, sizeof(Vertex) );

					const float depth = _clear.m_depth;

					vertex->m_x = -1.0f;
					vertex->m_y = -1.0f;
					vertex->m_z = depth;
					vertex++;
					vertex->m_x =  1.0f;
					vertex->m_y = -1.0f;
					vertex->m_z = depth;
					vertex++;
					vertex->m_x = -1.0f;
					vertex->m_y =  1.0f;
					vertex->m_z = depth;
					vertex++;
					vertex->m_x =  1.0f;
					vertex->m_y =  1.0f;
					vertex->m_z = depth;
				}

				vb.update(0, 4*_clearQuad.m_decl.m_stride, _clearQuad.m_vb->data);

				GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, vb.m_id) );

				ProgramGL& program = m_program[_clearQuad.m_program[numMrt-1].idx];
				GL_CHECK(glUseProgram(program.m_id) );
				program.bindAttributes(vertexDecl, 0);

				if (BGFX_CLEAR_COLOR_USE_PALETTE_BIT & _clear.m_flags)
				{
					float mrtClear[BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS][4];
					for (uint32_t ii = 0; ii < numMrt; ++ii)
					{
						uint8_t index = (uint8_t)bx::uint32_min(BGFX_CONFIG_MAX_CLEAR_COLOR_PALETTE-1, _clear.m_index[ii]);
						memcpy(mrtClear[ii], _palette[index], 16);
					}

					GL_CHECK(glUniform4fv(0, numMrt, mrtClear[0]) );
				}
				else
				{
					float rgba[4] =
					{
						_clear.m_index[0]*1.0f/255.0f,
						_clear.m_index[1]*1.0f/255.0f,
						_clear.m_index[2]*1.0f/255.0f,
						_clear.m_index[3]*1.0f/255.0f,
					};
					GL_CHECK(glUniform4fv(0, 1, rgba) );
				}

				GL_CHECK(glDrawArrays(GL_TRIANGLE_STRIP
					, 0
					, 4
					) );
			}
		}

		uint16_t m_numWindows;
		FrameBufferHandle m_windows[BGFX_CONFIG_MAX_FRAME_BUFFERS];

		IndexBufferGL m_indexBuffers[BGFX_CONFIG_MAX_INDEX_BUFFERS];
		VertexBufferGL m_vertexBuffers[BGFX_CONFIG_MAX_VERTEX_BUFFERS];
		ShaderGL m_shaders[BGFX_CONFIG_MAX_SHADERS];
		ProgramGL m_program[BGFX_CONFIG_MAX_PROGRAMS];
		TextureGL m_textures[BGFX_CONFIG_MAX_TEXTURES];
		VertexDecl m_vertexDecls[BGFX_CONFIG_MAX_VERTEX_DECLS];
		FrameBufferGL m_frameBuffers[BGFX_CONFIG_MAX_FRAME_BUFFERS];
		UniformRegistry m_uniformReg;
		void* m_uniforms[BGFX_CONFIG_MAX_UNIFORMS];
		QueriesGL m_queries;

		VaoStateCache m_vaoStateCache;
		SamplerStateCache m_samplerStateCache;

		TextVideoMem m_textVideoMem;
		bool m_rtMsaa;

		FrameBufferHandle m_fbh;

		Resolution m_resolution;
		void* m_capture;
		uint32_t m_captureSize;
		float m_maxAnisotropy;
		int32_t m_maxMsaa;
		GLuint m_vao;
		bool m_vaoSupport;
		bool m_samplerObjectSupport;
		bool m_shadowSamplersSupport;
		bool m_programBinarySupport;
		bool m_textureSwizzleSupport;
		bool m_depthTextureSupport;
		bool m_flip;

		uint64_t m_hash;

		GLenum m_readPixelsFmt;
		GLuint m_backBufferFbo;
		GLuint m_msaaBackBufferFbo;
		GLuint m_msaaBackBufferRbos[2];
		GlContext m_glctx;

		const char* m_vendor;
		const char* m_renderer;
		const char* m_version;
		const char* m_glslVersion;
	};

	RendererContextGL* s_renderGL;

	RendererContextI* rendererCreateGL()
	{
		s_renderGL = BX_NEW(g_allocator, RendererContextGL);
		return s_renderGL;
	}

	void rendererDestroyGL()
	{
		BX_DELETE(g_allocator, s_renderGL);
		s_renderGL = NULL;
	}

	const char* glslTypeName(GLuint _type)
	{
#define GLSL_TYPE(_ty) case _ty: return #_ty

		switch (_type)
		{
			GLSL_TYPE(GL_INT);
			GLSL_TYPE(GL_INT_VEC2);
			GLSL_TYPE(GL_INT_VEC3);
			GLSL_TYPE(GL_INT_VEC4);		
			GLSL_TYPE(GL_UNSIGNED_INT);
			GLSL_TYPE(GL_UNSIGNED_INT_VEC2);
			GLSL_TYPE(GL_UNSIGNED_INT_VEC3);
			GLSL_TYPE(GL_UNSIGNED_INT_VEC4);
			GLSL_TYPE(GL_FLOAT);
			GLSL_TYPE(GL_FLOAT_VEC2);
			GLSL_TYPE(GL_FLOAT_VEC3);
			GLSL_TYPE(GL_FLOAT_VEC4);
			GLSL_TYPE(GL_FLOAT_MAT2);
			GLSL_TYPE(GL_FLOAT_MAT3);
			GLSL_TYPE(GL_FLOAT_MAT4);
// 			GLSL_TYPE(GL_FLOAT_MAT2x3);
// 			GLSL_TYPE(GL_FLOAT_MAT2x4);
// 			GLSL_TYPE(GL_FLOAT_MAT3x2);
// 			GLSL_TYPE(GL_FLOAT_MAT3x4);
// 			GLSL_TYPE(GL_FLOAT_MAT4x2);
// 			GLSL_TYPE(GL_FLOAT_MAT4x3);
// 			GLSL_TYPE(GL_SAMPLER_1D);
 			GLSL_TYPE(GL_SAMPLER_2D);
			GLSL_TYPE(GL_SAMPLER_3D);
			GLSL_TYPE(GL_SAMPLER_CUBE);
// 			GLSL_TYPE(GL_SAMPLER_1D_SHADOW);
			GLSL_TYPE(GL_SAMPLER_2D_SHADOW);
			GLSL_TYPE(GL_IMAGE_1D);
			GLSL_TYPE(GL_IMAGE_2D);
			GLSL_TYPE(GL_IMAGE_3D);
			GLSL_TYPE(GL_IMAGE_CUBE);
		}

#undef GLSL_TYPE

		BX_CHECK(false, "Unknown GLSL type? %x", _type);
		return "UNKNOWN GLSL TYPE!";
	}

	const char* glEnumName(GLenum _enum)
	{
#define GLENUM(_ty) case _ty: return #_ty

		switch (_enum)
		{
			GLENUM(GL_TEXTURE);
			GLENUM(GL_RENDERBUFFER);

			GLENUM(GL_INVALID_ENUM);
			GLENUM(GL_INVALID_VALUE);
			GLENUM(GL_INVALID_OPERATION);
			GLENUM(GL_OUT_OF_MEMORY);

			GLENUM(GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT);
			GLENUM(GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT);
//			GLENUM(GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER);
//			GLENUM(GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER);
			GLENUM(GL_FRAMEBUFFER_UNSUPPORTED);
		}

#undef GLENUM

		BX_WARN(false, "Unknown enum? %x", _enum);
		return "<GLenum?>";
	}

	UniformType::Enum convertGlType(GLenum _type)
	{
		switch (_type)
		{
		case GL_INT:
		case GL_UNSIGNED_INT:
			return UniformType::Uniform1iv;

		case GL_FLOAT:
			return UniformType::Uniform1fv;

		case GL_FLOAT_VEC2:
			return UniformType::Uniform2fv;

		case GL_FLOAT_VEC3:
			return UniformType::Uniform3fv;

		case GL_FLOAT_VEC4:
			return UniformType::Uniform4fv;

		case GL_FLOAT_MAT2:
			break;

		case GL_FLOAT_MAT3:
			return UniformType::Uniform3x3fv;

		case GL_FLOAT_MAT4:
			return UniformType::Uniform4x4fv;

// 		case GL_FLOAT_MAT2x3:
// 		case GL_FLOAT_MAT2x4:
// 		case GL_FLOAT_MAT3x2:
// 		case GL_FLOAT_MAT3x4:
// 		case GL_FLOAT_MAT4x2:
// 		case GL_FLOAT_MAT4x3:
// 			break;

// 		case GL_SAMPLER_1D:
		case GL_SAMPLER_2D:
		case GL_SAMPLER_3D:
		case GL_SAMPLER_CUBE:
// 		case GL_SAMPLER_1D_SHADOW:
 		case GL_SAMPLER_2D_SHADOW:
		case GL_IMAGE_1D:
		case GL_IMAGE_2D:
		case GL_IMAGE_3D:
		case GL_IMAGE_CUBE:
			return UniformType::Uniform1iv;
		};

		BX_CHECK(false, "Unrecognized GL type 0x%04x.", _type);
		return UniformType::End;
	}

	void ProgramGL::create(const ShaderGL& _vsh, const ShaderGL& _fsh)
	{
		m_id = glCreateProgram();
		BX_TRACE("program create: %d: %d, %d", m_id, _vsh.m_id, _fsh.m_id);

		const uint64_t id = (uint64_t(_vsh.m_hash)<<32) | _fsh.m_hash;
		const bool cached = s_renderGL->programFetchFromCache(m_id, id);

		if (!cached)
		{
			GL_CHECK(glAttachShader(m_id, _vsh.m_id) );

			if (0 != _fsh.m_id)
			{
				GL_CHECK(glAttachShader(m_id, _fsh.m_id) );
			}

			GL_CHECK(glLinkProgram(m_id) );

			GLint linked = 0;
			GL_CHECK(glGetProgramiv(m_id, GL_LINK_STATUS, &linked) );

			if (0 == linked)
			{
				char log[1024];
				GL_CHECK(glGetProgramInfoLog(m_id, sizeof(log), NULL, log) );
				BX_TRACE("%d: %s", linked, log);

				GL_CHECK(glDeleteProgram(m_id) );
				return;
			}

			s_renderGL->programCache(m_id, id);
		}

		init();

		if (!cached)
		{
			// Must be after init, otherwise init might fail to lookup shader
			// info (NVIDIA Tegra 3 OpenGL ES 2.0 14.01003).
			GL_CHECK(glDetachShader(m_id, _vsh.m_id) );

			if (0 != _fsh.m_id)
			{
				GL_CHECK(glDetachShader(m_id, _fsh.m_id) );
			}
		}
	}

	void ProgramGL::destroy()
	{
		if (NULL != m_constantBuffer)
		{
			ConstantBuffer::destroy(m_constantBuffer);
			m_constantBuffer = NULL;
		}
		m_numPredefined = 0;

		if (0 != m_id)
		{
			GL_CHECK(glUseProgram(0) );
			GL_CHECK(glDeleteProgram(m_id) );
			m_id = 0;
		}

		m_vcref.invalidate(s_renderGL->m_vaoStateCache);
	}

	void ProgramGL::init()
	{
		GLint activeAttribs  = 0;
		GLint activeUniforms = 0;
		GLint activeBuffers  = 0;

#if BGFX_CONFIG_RENDERER_OPENGL >= 31
		GL_CHECK(glBindFragDataLocation(m_id, 0, "bgfx_FragColor") );
#endif // BGFX_CONFIG_RENDERER_OPENGL >= 31

		if (s_extension[Extension::ARB_program_interface_query].m_supported
		||  BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 31) )
		{
			GL_CHECK(glGetProgramInterfaceiv(m_id, GL_PROGRAM_INPUT,   GL_ACTIVE_RESOURCES, &activeAttribs ) );
			GL_CHECK(glGetProgramInterfaceiv(m_id, GL_UNIFORM,         GL_ACTIVE_RESOURCES, &activeUniforms) );
			GL_CHECK(glGetProgramInterfaceiv(m_id, GL_BUFFER_VARIABLE, GL_ACTIVE_RESOURCES, &activeBuffers ) );
		}
		else
		{
			GL_CHECK(glGetProgramiv(m_id, GL_ACTIVE_ATTRIBUTES, &activeAttribs ) );
			GL_CHECK(glGetProgramiv(m_id, GL_ACTIVE_UNIFORMS,   &activeUniforms) );
		}

		GLint max0, max1;
		GL_CHECK(glGetProgramiv(m_id, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &max0) );
		GL_CHECK(glGetProgramiv(m_id, GL_ACTIVE_UNIFORM_MAX_LENGTH,   &max1) );
		uint32_t maxLength = bx::uint32_max(max0, max1);
		char* name = (char*)alloca(maxLength + 1);

		BX_TRACE("Program %d", m_id);
		BX_TRACE("Attributes (%d):", activeAttribs);
		for (int32_t ii = 0; ii < activeAttribs; ++ii)
		{
			GLint size;
			GLenum type;

			GL_CHECK(glGetActiveAttrib(m_id, ii, maxLength + 1, NULL, &size, &type, name) );

			BX_TRACE("\t%s %s is at location %d"
				, glslTypeName(type)
				, name
				, glGetAttribLocation(m_id, name)
				);
		}

		m_numPredefined = 0;
 		m_numSamplers = 0;

		struct VariableInfo
		{
			GLenum type;
			GLint  loc;
			GLint  num;
		};
		VariableInfo vi;
		GLenum props[] = { GL_TYPE, GL_LOCATION, GL_ARRAY_SIZE };

		const bool piqSupported = s_extension[Extension::ARB_program_interface_query].m_supported;

		BX_TRACE("Uniforms (%d):", activeUniforms);
		for (int32_t ii = 0; ii < activeUniforms; ++ii)
		{
			GLenum gltype;
			GLint num;
			GLint loc;

			if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 31)
			||  piqSupported)
			{
				GL_CHECK(glGetProgramResourceiv(m_id
					, GL_UNIFORM
					, ii
					, BX_COUNTOF(props)
					, props
					, BX_COUNTOF(props)
					, NULL
					, (GLint*)&vi
					) );

				GL_CHECK(glGetProgramResourceName(m_id
					, GL_UNIFORM
					, ii
					, maxLength + 1
					, NULL
					, name
					) );

				gltype = vi.type;
				loc    = vi.loc;
				num    = vi.num;
			}
			else
			{
				GL_CHECK(glGetActiveUniform(m_id, ii, maxLength + 1, NULL, &num, &gltype, name) );
				loc = glGetUniformLocation(m_id, name);
			}

			num = bx::uint32_max(num, 1);

			int offset = 0;
			char* array = strchr(name, '[');
			if (NULL != array)
			{
				BX_TRACE("--- %s", name);
				*array = '\0';
				array++;
				char* end = strchr(array, ']');
				*end = '\0';
				offset = atoi(array);
			}

			switch (gltype)
			{
			case GL_SAMPLER_2D:
			case GL_SAMPLER_3D:
			case GL_SAMPLER_CUBE:
			case GL_SAMPLER_2D_SHADOW:
			case GL_IMAGE_1D:
			case GL_IMAGE_2D:
			case GL_IMAGE_3D:
			case GL_IMAGE_CUBE:
				BX_TRACE("Sampler #%d at location %d.", m_numSamplers, loc);
				m_sampler[m_numSamplers] = loc;
				m_numSamplers++;
				break;

			default:
				break;
			}

			PredefinedUniform::Enum predefined = nameToPredefinedUniformEnum(name);
			if (PredefinedUniform::Count != predefined)
			{
				m_predefined[m_numPredefined].m_loc = loc;
				m_predefined[m_numPredefined].m_type = predefined;
				m_predefined[m_numPredefined].m_count = num;
				m_numPredefined++;
			}
			else
			{
				const UniformInfo* info = s_renderGL->m_uniformReg.find(name);
				if (NULL != info)
				{
					if (NULL == m_constantBuffer)
					{
						m_constantBuffer = ConstantBuffer::create(1024);
					}

					UniformType::Enum type = convertGlType(gltype);
					m_constantBuffer->writeUniformHandle(type, 0, info->m_handle, num);
					m_constantBuffer->write(loc);
					BX_TRACE("store %s %d", name, info->m_handle);
				}
			}

			BX_TRACE("\tuniform %s %s%s is at location %d, size %d, offset %d"
				, glslTypeName(gltype)
				, name
				, PredefinedUniform::Count != predefined ? "*" : ""
				, loc
				, num
				, offset
				);
			BX_UNUSED(offset);
		}

		if (NULL != m_constantBuffer)
		{
			m_constantBuffer->finish();
		}

		if (s_extension[Extension::ARB_program_interface_query].m_supported
		||  BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 31) )
		{
			struct VariableInfo
			{
				GLenum type;
			};
			VariableInfo vi;
			GLenum props[] = { GL_TYPE };

			BX_TRACE("Buffers (%d):", activeBuffers);
			for (int32_t ii = 0; ii < activeBuffers; ++ii)
			{
				GL_CHECK(glGetProgramResourceiv(m_id
					, GL_BUFFER_VARIABLE
					, ii
					, BX_COUNTOF(props)
					, props
					, BX_COUNTOF(props)
					, NULL
					, (GLint*)&vi
					) );

				GL_CHECK(glGetProgramResourceName(m_id
					, GL_BUFFER_VARIABLE
					, ii
					, maxLength + 1
					, NULL
					, name
					) );

				BX_TRACE("\t%s %s at %d"
					, glslTypeName(vi.type)
					, name
					, 0 //vi.loc
					);
			}
		}

		memset(m_attributes, 0xff, sizeof(m_attributes) );
		uint32_t used = 0;
		for (uint32_t ii = 0; ii < Attrib::Count; ++ii)
		{
			GLint loc = glGetAttribLocation(m_id, s_attribName[ii]);
			if (-1 != loc)
			{
				BX_TRACE("attr %s: %d", s_attribName[ii], loc);
				m_attributes[ii] = loc;
				m_used[used++] = ii;
			}
		}
		m_used[used] = Attrib::Count;

		used = 0;
		for (uint32_t ii = 0; ii < BX_COUNTOF(s_instanceDataName); ++ii)
		{
			GLuint loc = glGetAttribLocation(m_id, s_instanceDataName[ii]);
			if (GLuint(-1) != loc )
			{
				BX_TRACE("instance data %s: %d", s_instanceDataName[ii], loc);
				m_instanceData[used++] = loc;
			}
		}
		m_instanceData[used] = 0xffff;
	}

	void ProgramGL::bindAttributes(const VertexDecl& _vertexDecl, uint32_t _baseVertex) const
	{
		for (uint32_t ii = 0; Attrib::Count != m_used[ii]; ++ii)
		{
			Attrib::Enum attr = Attrib::Enum(m_used[ii]);
			GLint loc = m_attributes[attr];

			uint8_t num;
			AttribType::Enum type;
			bool normalized;
			bool asInt;
			_vertexDecl.decode(attr, num, type, normalized, asInt);

			if (-1 != loc)
			{
				if (0xff != _vertexDecl.m_attributes[attr])
				{
					GL_CHECK(glEnableVertexAttribArray(loc) );
					GL_CHECK(glVertexAttribDivisor(loc, 0) );

					uint32_t baseVertex = _baseVertex*_vertexDecl.m_stride + _vertexDecl.m_offset[attr];
					GL_CHECK(glVertexAttribPointer(loc, num, s_attribType[type], normalized, _vertexDecl.m_stride, (void*)(uintptr_t)baseVertex) );
				}
				else
				{
					GL_CHECK(glDisableVertexAttribArray(loc) );
				}
			}
		}
	}

	void ProgramGL::bindInstanceData(uint32_t _stride, uint32_t _baseVertex) const
	{
		uint32_t baseVertex = _baseVertex;
		for (uint32_t ii = 0; 0xffff != m_instanceData[ii]; ++ii)
		{
			GLint loc = m_instanceData[ii];
			GL_CHECK(glEnableVertexAttribArray(loc) );
			GL_CHECK(glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, _stride, (void*)(uintptr_t)baseVertex) );
			GL_CHECK(glVertexAttribDivisor(loc, 1) );
			baseVertex += 16;
		}
	}

	void IndexBufferGL::destroy()
	{
		GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0) );
		GL_CHECK(glDeleteBuffers(1, &m_id) );

		m_vcref.invalidate(s_renderGL->m_vaoStateCache);
	}

	void VertexBufferGL::destroy()
	{
		GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, 0) );
		GL_CHECK(glDeleteBuffers(1, &m_id) );

		m_vcref.invalidate(s_renderGL->m_vaoStateCache);
	}

	static void texImage(GLenum _target, GLint _level, GLint _internalFormat, GLsizei _width, GLsizei _height, GLsizei _depth, GLint _border, GLenum _format, GLenum _type, const GLvoid* _data)
	{
		if (_target == GL_TEXTURE_3D)
		{
			GL_CHECK(glTexImage3D(_target, _level, _internalFormat, _width, _height, _depth, _border, _format, _type, _data) );
		}
		else
		{
			BX_UNUSED(_depth);
			GL_CHECK(glTexImage2D(_target, _level, _internalFormat, _width, _height, _border, _format, _type, _data) );
		}
	}

	static void texSubImage(GLenum _target, GLint _level, GLint _xoffset, GLint _yoffset, GLint _zoffset, GLsizei _width, GLsizei _height, GLsizei _depth, GLenum _format, GLenum _type, const GLvoid* _data)
	{
		if (_target == GL_TEXTURE_3D)
		{
			GL_CHECK(glTexSubImage3D(_target, _level, _xoffset, _yoffset, _zoffset, _width, _height, _depth, _format, _type, _data) );
		}
		else
		{
			BX_UNUSED(_zoffset, _depth);
			GL_CHECK(glTexSubImage2D(_target, _level, _xoffset, _yoffset, _width, _height, _format, _type, _data) );
		}
	}

	static void compressedTexImage(GLenum _target, GLint _level, GLenum _internalformat, GLsizei _width, GLsizei _height, GLsizei _depth, GLint _border, GLsizei _imageSize, const GLvoid* _data)
	{
		if (_target == GL_TEXTURE_3D)
		{
			GL_CHECK(glCompressedTexImage3D(_target, _level, _internalformat, _width, _height, _depth, _border, _imageSize, _data) );
		}
		else
		{
			BX_UNUSED(_depth);
			GL_CHECK(glCompressedTexImage2D(_target, _level, _internalformat, _width, _height, _border, _imageSize, _data) );
		}
	}

	static void compressedTexSubImage(GLenum _target, GLint _level, GLint _xoffset, GLint _yoffset, GLint _zoffset, GLsizei _width, GLsizei _height, GLsizei _depth, GLenum _format, GLsizei _imageSize, const GLvoid* _data)
	{
		if (_target == GL_TEXTURE_3D)
		{
			GL_CHECK(glCompressedTexSubImage3D(_target, _level, _xoffset, _yoffset, _zoffset, _width, _height, _depth, _format, _imageSize, _data) );
		}
		else
		{
			BX_UNUSED(_zoffset, _depth);
			GL_CHECK(glCompressedTexSubImage2D(_target, _level, _xoffset, _yoffset, _width, _height, _format, _imageSize, _data) );
		}
	}

	bool TextureGL::init(GLenum _target, uint32_t _width, uint32_t _height, uint8_t _format, uint8_t _numMips, uint32_t _flags)
	{
		m_target = _target;
		m_numMips = _numMips;
		m_flags = _flags;
		m_currentFlags = UINT32_MAX;
		m_width = _width;
		m_height = _height;
		m_requestedFormat = _format;
		m_textureFormat   = _format;

		const bool bufferOnly = 0 != (m_flags&BGFX_TEXTURE_RT_BUFFER_ONLY);

		if (!bufferOnly)
		{
			GL_CHECK(glGenTextures(1, &m_id) );
			BX_CHECK(0 != m_id, "Failed to generate texture id.");
			GL_CHECK(glBindTexture(_target, m_id) );

			setSamplerState(_flags);

			const TextureFormatInfo& tfi = s_textureFormat[_format];
			m_fmt = tfi.m_fmt;
			m_type = tfi.m_type;

			const bool compressed = isCompressed(TextureFormat::Enum(_format) );
			const bool decompress = !tfi.m_supported && compressed;

			if (decompress)
			{
				m_textureFormat = (uint8_t)TextureFormat::BGRA8;
				const TextureFormatInfo& tfi = s_textureFormat[TextureFormat::BGRA8];
				m_fmt = tfi.m_fmt;
				m_type = tfi.m_type;
			}

			if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL)
			&&  TextureFormat::BGRA8 == m_textureFormat
			&&  GL_RGBA == m_fmt
			&&  s_renderGL->m_textureSwizzleSupport)
			{
				GLint swizzleMask[] = { GL_BLUE, GL_GREEN, GL_RED, GL_ALPHA };
				GL_CHECK(glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask) );
			}
		}

		const bool renderTarget = 0 != (m_flags&BGFX_TEXTURE_RT_MASK);

		if (renderTarget)
		{
			uint32_t msaaQuality = ( (m_flags&BGFX_TEXTURE_RT_MSAA_MASK)>>BGFX_TEXTURE_RT_MSAA_SHIFT);
			msaaQuality = bx::uint32_satsub(msaaQuality, 1);
			msaaQuality = bx::uint32_min(s_renderGL->m_maxMsaa, msaaQuality == 0 ? 0 : 1<<msaaQuality);

			if (0 != msaaQuality
			||  bufferOnly)
			{
				GL_CHECK(glGenRenderbuffers(1, &m_rbo) );
				BX_CHECK(0 != m_rbo, "Failed to generate renderbuffer id.");
				GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, m_rbo) );

				if (0 == msaaQuality)
				{
					GL_CHECK(glRenderbufferStorage(GL_RENDERBUFFER
						, s_textureFormat[m_textureFormat].m_internalFmt
						, _width
						, _height
						) );
				}
				else if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL || BGFX_CONFIG_RENDERER_OPENGLES >= 30) )
				{
					GL_CHECK(glRenderbufferStorageMultisample(GL_RENDERBUFFER
						, msaaQuality
						, s_textureFormat[m_textureFormat].m_internalFmt
						, _width
						, _height
						) );
				}

				GL_CHECK(glBindRenderbuffer(GL_RENDERBUFFER, 0) );

				if (bufferOnly)
				{
					// This is render buffer, there is no sampling, no need
					// to create texture.
					return false;
				}
			}
		}

		return true;
	}

	void TextureGL::create(const Memory* _mem, uint32_t _flags, uint8_t _skip)
	{
		ImageContainer imageContainer;

		if (imageParse(imageContainer, _mem->data, _mem->size) )
		{
			uint8_t numMips = imageContainer.m_numMips;
			const uint32_t startLod = bx::uint32_min(_skip, numMips-1);
			numMips -= uint8_t(startLod);
			const ImageBlockInfo& blockInfo = getBlockInfo(TextureFormat::Enum(imageContainer.m_format) );
			const uint32_t textureWidth  = bx::uint32_max(blockInfo.blockWidth,  imageContainer.m_width >>startLod);
			const uint32_t textureHeight = bx::uint32_max(blockInfo.blockHeight, imageContainer.m_height>>startLod);

			GLenum target = GL_TEXTURE_2D;
			if (imageContainer.m_cubeMap)
			{
				target = GL_TEXTURE_CUBE_MAP;
			}
			else if (imageContainer.m_depth > 1)
			{
				target = GL_TEXTURE_3D;
			}

			if (!init(target
					, textureWidth
					, textureHeight
					, imageContainer.m_format
					, numMips
					, _flags
					) )
			{
				return;
			}

			target = GL_TEXTURE_CUBE_MAP == m_target ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : m_target;

			const GLenum internalFmt = s_textureFormat[m_textureFormat].m_internalFmt;

			const bool swizzle = true
				&& TextureFormat::BGRA8 == m_textureFormat
				&& GL_RGBA == m_fmt
				&& !s_renderGL->m_textureSwizzleSupport
				;
			const bool convert    = m_textureFormat != m_requestedFormat;
			const bool compressed = isCompressed(TextureFormat::Enum(m_textureFormat) );
			uint32_t blockWidth  = 1;
			uint32_t blockHeight = 1;
			
			if (convert && compressed)
			{
				blockWidth  = blockInfo.blockWidth;
				blockHeight = blockInfo.blockHeight;
			}

			BX_TRACE("Texture %3d: %s (requested: %s), %dx%d%s%s."
				, this - s_renderGL->m_textures
				, getName( (TextureFormat::Enum)m_textureFormat)
				, getName( (TextureFormat::Enum)m_requestedFormat)
				, textureWidth
				, textureHeight
				, imageContainer.m_cubeMap ? "x6" : ""
				, 0 != (m_flags&BGFX_TEXTURE_RT_MASK) ? " (render target)" : ""
				);

			BX_WARN(!swizzle && !convert, "Texture %s%s%s from %s to %s."
					, swizzle ? "swizzle" : ""
					, swizzle&&convert ? " and " : ""
					, convert ? "convert" : ""
					, getName( (TextureFormat::Enum)m_requestedFormat)
					, getName( (TextureFormat::Enum)m_textureFormat)
					);

			uint8_t* temp = NULL;
			if (convert || swizzle)
			{
				temp = (uint8_t*)BX_ALLOC(g_allocator, textureWidth*textureHeight*4);
			}

			for (uint8_t side = 0, numSides = imageContainer.m_cubeMap ? 6 : 1; side < numSides; ++side)
			{
				uint32_t width  = textureWidth;
				uint32_t height = textureHeight;
				uint32_t depth  = imageContainer.m_depth;

				for (uint32_t lod = 0, num = numMips; lod < num; ++lod)
				{
					width  = bx::uint32_max(blockWidth,  width);
					height = bx::uint32_max(blockHeight, height);
					depth  = bx::uint32_max(1, depth);

					ImageMip mip;
					if (imageGetRawData(imageContainer, side, lod+startLod, _mem->data, _mem->size, mip) )
					{
						if (compressed)
						{
							compressedTexImage(target+side
								, lod
								, internalFmt
								, width
								, height
								, depth
								, 0
								, mip.m_size
								, mip.m_data
								);
						}
						else
						{
							const uint8_t* data = mip.m_data;

							if (convert)
							{
								imageDecodeToBgra8(temp, mip.m_data, mip.m_width, mip.m_height, mip.m_width*4, mip.m_format);
								data = temp;
							}

							if (swizzle)
							{
								imageSwizzleBgra8(width, height, mip.m_width*4, data, temp);
								data = temp;
							}

							texImage(target+side
								, lod
								, internalFmt
								, width
								, height
								, depth
								, 0
								, m_fmt
								, m_type
								, data
								);
						}
					}
					else
					{
						if (compressed)
						{
							uint32_t size = bx::uint32_max(1, (width  + 3)>>2)
										  * bx::uint32_max(1, (height + 3)>>2)
										  * 4*4*getBitsPerPixel(TextureFormat::Enum(m_textureFormat) )/8
										  ;

							compressedTexImage(target+side
								, lod
								, internalFmt
								, width
								, height
								, depth
								, 0
								, size
								, NULL
								);
						}
						else
						{
							texImage(target+side
								, lod
								, internalFmt
								, width
								, height
								, depth
								, 0
								, m_fmt
								, m_type
								, NULL
								);
						}
					}

					width  >>= 1;
					height >>= 1;
					depth  >>= 1;
				}
			}

			if (NULL != temp)
			{
				BX_FREE(g_allocator, temp);
			}
		}

		GL_CHECK(glBindTexture(m_target, 0) );
	}

	void TextureGL::destroy()
	{
		if (0 != m_id)
		{
			GL_CHECK(glBindTexture(m_target, 0) );
			GL_CHECK(glDeleteTextures(1, &m_id) );
			m_id = 0;
		}

		if (0 != m_rbo)
		{
			GL_CHECK(glDeleteRenderbuffers(1, &m_rbo) );
			m_rbo = 0;
		}
	}

	void TextureGL::update(uint8_t _side, uint8_t _mip, const Rect& _rect, uint16_t _z, uint16_t _depth, uint16_t _pitch, const Memory* _mem)
	{
		BX_UNUSED(_z, _depth);

		const uint32_t bpp = getBitsPerPixel(TextureFormat::Enum(m_textureFormat) );
		const uint32_t rectpitch = _rect.m_width*bpp/8;
		uint32_t srcpitch  = UINT16_MAX == _pitch ? rectpitch : _pitch;

		GL_CHECK(glBindTexture(m_target, m_id) );
		GL_CHECK(glPixelStorei(GL_UNPACK_ALIGNMENT, 1) );

		GLenum target = GL_TEXTURE_CUBE_MAP == m_target ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : m_target;

		const bool swizzle = true
			&& TextureFormat::BGRA8 == m_textureFormat
			&& GL_RGBA == m_fmt
			&& !s_renderGL->m_textureSwizzleSupport
			;
		const bool unpackRowLength = BX_IGNORE_C4127(!!BGFX_CONFIG_RENDERER_OPENGL || s_extension[Extension::EXT_unpack_subimage].m_supported);
		const bool convert         = m_textureFormat != m_requestedFormat;
		const bool compressed      = isCompressed(TextureFormat::Enum(m_textureFormat) );

		const uint32_t width  = _rect.m_width;
		const uint32_t height = _rect.m_height;

		uint8_t* temp = NULL;
		if (convert
		||  swizzle
		||  !unpackRowLength)
		{
			temp = (uint8_t*)BX_ALLOC(g_allocator, rectpitch*height);
		}
		else if (unpackRowLength)
		{
			GL_CHECK(glPixelStorei(GL_UNPACK_ROW_LENGTH, srcpitch*8/bpp) );
		}

		if (compressed)
		{
			const uint8_t* data = _mem->data;

			if (!unpackRowLength)
			{
				imageCopy(width, height, bpp, srcpitch, data, temp);
				data = temp;
			}

			GL_CHECK(compressedTexSubImage(target+_side
				, _mip
				, _rect.m_x
				, _rect.m_y
				, _z
				, _rect.m_width
				, _rect.m_height
				, _depth
				, m_fmt
				, _mem->size
				, data
				) );
		}
		else
		{
			const uint8_t* data = _mem->data;

			if (convert)
			{
				imageDecodeToBgra8(temp, data, width, height, srcpitch, m_requestedFormat);
				data = temp;
				srcpitch = rectpitch;
			}

			if (swizzle)
			{
				imageSwizzleBgra8(width, height, srcpitch, data, temp);
				data = temp;
			}
			else if (!unpackRowLength && !convert)
			{
				imageCopy(width, height, bpp, srcpitch, data, temp);
				data = temp;
			}

			GL_CHECK(texSubImage(target+_side
				, _mip
				, _rect.m_x
				, _rect.m_y
				, _z
				, _rect.m_width
				, _rect.m_height
				, _depth
				, m_fmt
				, m_type
				, data
				) );
		}

		if (NULL != temp)
		{
			BX_FREE(g_allocator, temp);
		}
	}

	void TextureGL::setSamplerState(uint32_t _flags)
	{
		const uint32_t flags = (0 != (BGFX_SAMPLER_DEFAULT_FLAGS & _flags) ? m_flags : _flags) & BGFX_TEXTURE_SAMPLER_BITS_MASK;
		if (flags != m_currentFlags)
		{
			const GLenum target = m_target;
			const uint8_t numMips = m_numMips;

			GL_CHECK(glTexParameteri(target, GL_TEXTURE_WRAP_S, s_textureAddress[(flags&BGFX_TEXTURE_U_MASK)>>BGFX_TEXTURE_U_SHIFT]) );
			GL_CHECK(glTexParameteri(target, GL_TEXTURE_WRAP_T, s_textureAddress[(flags&BGFX_TEXTURE_V_MASK)>>BGFX_TEXTURE_V_SHIFT]) );

			if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL || BGFX_CONFIG_RENDERER_OPENGLES >= 30)
			||  s_extension[Extension::APPLE_texture_max_level].m_supported)
			{
				GL_CHECK(glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, numMips-1) );
			}

			if (target == GL_TEXTURE_3D)
			{
				GL_CHECK(glTexParameteri(target, GL_TEXTURE_WRAP_R, s_textureAddress[(flags&BGFX_TEXTURE_W_MASK)>>BGFX_TEXTURE_W_SHIFT]) );
			}

			const uint32_t mag = (flags&BGFX_TEXTURE_MAG_MASK)>>BGFX_TEXTURE_MAG_SHIFT;
			const uint32_t min = (flags&BGFX_TEXTURE_MIN_MASK)>>BGFX_TEXTURE_MIN_SHIFT;
			const uint32_t mip = (flags&BGFX_TEXTURE_MIP_MASK)>>BGFX_TEXTURE_MIP_SHIFT;
			const GLenum minFilter = s_textureFilterMin[min][1 < numMips ? mip+1 : 0];
			GL_CHECK(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, s_textureFilterMag[mag]) );
			GL_CHECK(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter) );
			if (0 != (flags & (BGFX_TEXTURE_MIN_ANISOTROPIC|BGFX_TEXTURE_MAG_ANISOTROPIC) )
			&&  0.0f < s_renderGL->m_maxAnisotropy)
			{
				GL_CHECK(glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, s_renderGL->m_maxAnisotropy) );
			}

			if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 30)
			||  s_renderGL->m_shadowSamplersSupport)
			{
				const uint32_t cmpFunc = (flags&BGFX_TEXTURE_COMPARE_MASK)>>BGFX_TEXTURE_COMPARE_SHIFT;
				if (0 == cmpFunc)
				{
					GL_CHECK(glTexParameteri(m_target, GL_TEXTURE_COMPARE_MODE, GL_NONE) );
				}
				else
				{
					GL_CHECK(glTexParameteri(m_target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE) );
					GL_CHECK(glTexParameteri(m_target, GL_TEXTURE_COMPARE_FUNC, s_cmpFunc[cmpFunc]) );
				}
			}

			m_currentFlags = flags;
		}
	}

	void TextureGL::commit(uint32_t _stage, uint32_t _flags)
	{
		GL_CHECK(glActiveTexture(GL_TEXTURE0+_stage) );
		GL_CHECK(glBindTexture(m_target, m_id) );

		if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES)
		&&  BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES < 30) )
		{
			// GLES2 doesn't have support for sampler object.
			setSamplerState(_flags);
		}
		else if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL)
			 &&  BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL < 31) )
		{
			// In case that GL 2.1 sampler object is supported via extension.
			if (s_renderGL->m_samplerObjectSupport)
			{
				s_renderGL->setSamplerState(_stage, m_numMips, _flags);
			}
			else
			{
				setSamplerState(_flags);
			}
		}
		else
		{
			// Everything else has sampler object.
			s_renderGL->setSamplerState(_stage, m_numMips, _flags);
		}
	}

	void writeString(bx::WriterI* _writer, const char* _str)
	{
		bx::write(_writer, _str, (int32_t)strlen(_str) );
	}

	void writeStringf(bx::WriterI* _writer, const char* _format, ...)
	{
		char temp[512];

		va_list argList;
		va_start(argList, _format);
		int len = bx::vsnprintf(temp, BX_COUNTOF(temp), _format, argList);
		va_end(argList);

		bx::write(_writer, temp, len);
	}

	void strins(char* _str, const char* _insert)
	{
		size_t len = strlen(_insert);
		memmove(&_str[len], _str, strlen(_str)+1);
		memcpy(_str, _insert, len);
	}

	void ShaderGL::create(Memory* _mem)
	{
		bx::MemoryReader reader(_mem->data, _mem->size);
		m_hash = bx::hashMurmur2A(_mem->data, _mem->size);

		uint32_t magic;
		bx::read(&reader, magic);

		switch (magic)
		{
		case BGFX_CHUNK_MAGIC_CSH: m_type = GL_COMPUTE_SHADER;  break;
		case BGFX_CHUNK_MAGIC_FSH: m_type = GL_FRAGMENT_SHADER;	break;
		case BGFX_CHUNK_MAGIC_VSH: m_type = GL_VERTEX_SHADER;   break;

		default:
			BGFX_FATAL(false, Fatal::InvalidShader, "Unknown shader format %x.", magic);
			break;
		}

		uint32_t iohash;
		bx::read(&reader, iohash);

		uint16_t count;
		bx::read(&reader, count);

		BX_TRACE("%s Shader consts %d"
			, BGFX_CHUNK_MAGIC_FSH == magic ? "Fragment" : BGFX_CHUNK_MAGIC_VSH == magic ? "Vertex" : "Compute"
			, count
			);

		for (uint32_t ii = 0; ii < count; ++ii)
		{
			uint8_t nameSize;
			bx::read(&reader, nameSize);

			char name[256];
			bx::read(&reader, &name, nameSize);
			name[nameSize] = '\0';

			uint8_t type;
			bx::read(&reader, type);

			uint8_t num;
			bx::read(&reader, num);

			uint16_t regIndex;
			bx::read(&reader, regIndex);

			uint16_t regCount;
			bx::read(&reader, regCount);
		}

		uint32_t shaderSize;
		bx::read(&reader, shaderSize);

		m_id = glCreateShader(m_type);

		const char* code = (const char*)reader.getDataPtr();

		if (0 != m_id)
		{
			if (GL_COMPUTE_SHADER != m_type)
			{
				int32_t codeLen = (int32_t)strlen(code);
				int32_t tempLen = codeLen + (4<<10);
				char* temp = (char*)alloca(tempLen);
				bx::StaticMemoryBlockWriter writer(temp, tempLen);

				if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES)
				&&  BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES < 30) )
				{
					writeString(&writer
						, "#define flat\n"
						  "#define smooth\n"
						  "#define noperspective\n"
						);

					bool usesDerivatives = s_extension[Extension::OES_standard_derivatives].m_supported 
						&& bx::findIdentifierMatch(code, s_OES_standard_derivatives)
						;

					bool usesFragData  = !!bx::findIdentifierMatch(code, "gl_FragData");

					bool usesFragDepth = !!bx::findIdentifierMatch(code, "gl_FragDepth");

					bool usesShadowSamplers = !!bx::findIdentifierMatch(code, s_EXT_shadow_samplers);

					bool usesTexture3D = s_extension[Extension::OES_texture_3D].m_supported
						&& bx::findIdentifierMatch(code, s_OES_texture_3D)
						;

					bool usesTextureLod = !!bx::findIdentifierMatch(code, s_EXT_shader_texture_lod);

					bool usesFragmentOrdering = !!bx::findIdentifierMatch(code, "beginFragmentShaderOrdering");

					if (usesDerivatives)
					{
						writeString(&writer, "#extension GL_OES_standard_derivatives : enable\n");
					}

					if (usesFragData)
					{
						BX_WARN(s_extension[Extension::EXT_draw_buffers].m_supported, "EXT_draw_buffers is used but not supported by GLES2 driver.");
						writeString(&writer
							, "#extension GL_EXT_draw_buffers : enable\n"
							);
					}

					bool insertFragDepth = false;
					if (usesFragDepth)
					{
						BX_WARN(s_extension[Extension::EXT_frag_depth].m_supported, "EXT_frag_depth is used but not supported by GLES2 driver.");
						if (s_extension[Extension::EXT_frag_depth].m_supported)
						{
							writeString(&writer
								, "#extension GL_EXT_frag_depth : enable\n"
								  "#define bgfx_FragDepth gl_FragDepthEXT\n"
								);

							char str[128];
							bx::snprintf(str, BX_COUNTOF(str), "%s float gl_FragDepthEXT;\n"
								, s_extension[Extension::OES_fragment_precision_high].m_supported ? "highp" : "mediump"
								);
							writeString(&writer, str);
						}
						else
						{
							insertFragDepth = true;
						}
					}

					if (usesShadowSamplers)
					{
						if (s_renderGL->m_shadowSamplersSupport)
						{
							writeString(&writer
								, "#extension GL_EXT_shadow_samplers : enable\n"
								  "#define shadow2D shadow2DEXT\n"
								  "#define shadow2DProj shadow2DProjEXT\n"
								);
						}
						else
						{
							writeString(&writer
								, "#define sampler2DShadow sampler2D\n"
								  "#define shadow2D(_sampler, _coord) step(_coord.z, texture2D(_sampler, _coord.xy).x)\n"
								  "#define shadow2DProj(_sampler, _coord) step(_coord.z/_coord.w, texture2DProj(_sampler, _coord).x)\n"
								);
						}
					}

					if (usesTexture3D)
					{
						writeString(&writer, "#extension GL_OES_texture_3D : enable\n");
					}

					if (usesTextureLod)
					{
						BX_WARN(s_extension[Extension::EXT_shader_texture_lod].m_supported, "EXT_shader_texture_lod is used but not supported by GLES2 driver.");
						if (s_extension[Extension::EXT_shader_texture_lod].m_supported)
						{
							writeString(&writer
								, "#extension GL_EXT_shader_texture_lod : enable\n"
								  "#define texture2DLod texture2DLodEXT\n"
								  "#define texture2DProjLod texture2DProjLodEXT\n"
								  "#define textureCubeLod textureCubeLodEXT\n"
								);
						}
						else
						{
							writeString(&writer
								, "#define texture2DLod(_sampler, _coord, _level) texture2D(_sampler, _coord)\n"
								  "#define texture2DProjLod(_sampler, _coord, _level) texture2DProj(_sampler, _coord)\n"
								  "#define textureCubeLod(_sampler, _coord, _level) textureCube(_sampler, _coord)\n"
								);
						}
					}

					if (usesFragmentOrdering)
					{
						if (s_extension[Extension::INTEL_fragment_shader_ordering].m_supported)
						{
							writeString(&writer, "#extension GL_INTEL_fragment_shader_ordering : enable\n");
						}
						else
						{
							writeString(&writer, "#define beginFragmentShaderOrdering()\n");
						}
					}

					writeString(&writer, "precision mediump float;\n");

					bx::write(&writer, code, codeLen);
					bx::write(&writer, '\0');

					if (insertFragDepth)
					{
						char* entry = strstr(temp, "void main ()");
						if (NULL != entry)
						{
							char* brace = strstr(entry, "{");
							if (NULL != brace)
							{
								const char* end = bx::strmb(brace, '{', '}');
								if (NULL != end)
								{
									strins(brace+1, "\n  float bgfx_FragDepth = 0.0;\n");
								}
							}
						}
					}

					// Replace all instances of gl_FragDepth with bgfx_FragDepth.
					for (const char* fragDepth = bx::findIdentifierMatch(temp, "gl_FragDepth"); NULL != fragDepth; fragDepth = bx::findIdentifierMatch(fragDepth, "gl_FragDepth") )
					{
						char* insert = const_cast<char*>(fragDepth);
						strins(insert, "bg");
						memcpy(insert + 2, "fx", 2);
					}
				}
				else if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL)
					 &&  BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL <= 21) )
				{
					bool usesTextureLod = s_extension[Extension::ARB_shader_texture_lod].m_supported
						&& bx::findIdentifierMatch(code, s_ARB_shader_texture_lod)
						;

					if (usesTextureLod)
					{
						writeString(&writer, "#version 120\n");

						if (m_type == GL_FRAGMENT_SHADER)
						{
							writeString(&writer, "#extension GL_ARB_shader_texture_lod : enable\n");
						}
					}

					writeString(&writer
							, "#define lowp\n"
							  "#define mediump\n"
							  "#define highp\n"
							  "#define flat\n"
							  "#define smooth\n"
							  "#define noperspective\n"
							);

					bx::write(&writer, code, codeLen);
					bx::write(&writer, '\0');
				}
				else if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL   >= 31)
					 ||  BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 30) )
				{
					if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 30) )
					{
						writeString(&writer
							, "#version 300 es\n"
							  "precision mediump float;\n"
							);
					}
					else
					{
						writeString(&writer, "#version 140\n");
					}

					if (m_type == GL_FRAGMENT_SHADER)
					{
						writeString(&writer, "#define varying in\n");
						writeString(&writer, "#define texture2D texture\n");
						writeString(&writer, "#define texture2DLod textureLod\n");
						writeString(&writer, "#define texture2DProj textureProj\n");

						if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL) )
						{
							writeString(&writer, "#define shadow2D(_sampler, _coord) vec2(textureProj(_sampler, vec4(_coord, 1.0) ) )\n");
							writeString(&writer, "#define shadow2DProj(_sampler, _coord) vec2(textureProj(_sampler, _coord) ) )\n");
						}
						else
						{
							writeString(&writer, "#define shadow2D(_sampler, _coord) (textureProj(_sampler, vec4(_coord, 1.0) ) )\n");
							writeString(&writer, "#define shadow2DProj(_sampler, _coord) (textureProj(_sampler, _coord) ) )\n");
						}

						writeString(&writer, "#define texture3D texture\n");
						writeString(&writer, "#define texture3DLod textureLod\n");
						writeString(&writer, "#define textureCube texture\n");
						writeString(&writer, "#define textureCubeLod textureLod\n");

						uint32_t fragData = 0;

						if (!!bx::findIdentifierMatch(code, "gl_FragData") )
						{
							for (uint32_t ii = 0, num = g_caps.maxFBAttachments; ii < num; ++ii)
							{
								char temp[16];
								bx::snprintf(temp, BX_COUNTOF(temp), "gl_FragData[%d]", ii);
								fragData = bx::uint32_max(fragData, NULL == strstr(code, temp) ? 0 : ii+1);
							}

							BGFX_FATAL(0 != fragData, Fatal::InvalidShader, "Unable to find and patch gl_FragData!");
						}

						if (!!bx::findIdentifierMatch(code, "beginFragmentShaderOrdering") )
						{
							if (s_extension[Extension::INTEL_fragment_shader_ordering].m_supported)
							{
								writeString(&writer, "#extension GL_INTEL_fragment_shader_ordering : enable\n");
							}
							else
							{
								writeString(&writer, "#define beginFragmentShaderOrdering()\n");
							}
						}

						if (0 != fragData)
						{
							writeStringf(&writer, "out vec4 bgfx_FragData[%d];\n", fragData);
							writeString(&writer, "#define gl_FragData bgfx_FragData\n");
						}
						else
						{
							writeString(&writer, "out vec4 bgfx_FragColor;\n");
							writeString(&writer, "#define gl_FragColor bgfx_FragColor\n");
						}
					}
					else
					{
						writeString(&writer, "#define attribute in\n");
						writeString(&writer, "#define varying out\n");
					}

					if (!BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 30) )
					{
						writeString(&writer
								, "#define lowp\n"
								  "#define mediump\n"
								  "#define highp\n"
								);
					}

					bx::write(&writer, code, codeLen);
					bx::write(&writer, '\0');
				}

				code = temp;
			}

			GL_CHECK(glShaderSource(m_id, 1, (const GLchar**)&code, NULL) );
			GL_CHECK(glCompileShader(m_id) );

			GLint compiled = 0;
			GL_CHECK(glGetShaderiv(m_id, GL_COMPILE_STATUS, &compiled) );

			if (0 == compiled)
			{
				BX_TRACE("\n####\n%s\n####", code);

				GLsizei len;
				char log[1024];
				GL_CHECK(glGetShaderInfoLog(m_id, sizeof(log), &len, log) );
				BX_TRACE("Failed to compile shader. %d: %s", compiled, log);

				GL_CHECK(glDeleteShader(m_id) );
				BGFX_FATAL(false, bgfx::Fatal::InvalidShader, "Failed to compile shader.");
			}
			else if (BX_ENABLED(BGFX_CONFIG_DEBUG)
				 &&  s_extension[Extension::ANGLE_translated_shader_source].m_supported
				 &&  NULL != glGetTranslatedShaderSourceANGLE)
			{
				GLsizei len;
				GL_CHECK(glGetShaderiv(m_id, GL_TRANSLATED_SHADER_SOURCE_LENGTH_ANGLE, &len) );

				char* source = (char*)alloca(len);
				GL_CHECK(glGetTranslatedShaderSourceANGLE(m_id, len, &len, source) );

				BX_TRACE("ANGLE source (len: %d):\n%s\n####", len, source);
			}
		}
	}

	void ShaderGL::destroy()
	{
		if (0 != m_id)
		{
			GL_CHECK(glDeleteShader(m_id) );
			m_id = 0;
		}
	}

	static void frameBufferValidate()
	{
		GLenum complete = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		BX_CHECK(GL_FRAMEBUFFER_COMPLETE == complete
			, "glCheckFramebufferStatus failed 0x%08x: %s"
			, complete
			, glEnumName(complete)
		);
		BX_UNUSED(complete);
	}

	void FrameBufferGL::create(uint8_t _num, const TextureHandle* _handles)
	{
		GL_CHECK(glGenFramebuffers(1, &m_fbo[0]) );
		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[0]) );

//		m_denseIdx = UINT16_MAX;
		bool needResolve = false;

		GLenum buffers[BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS];

		uint32_t colorIdx = 0;
		for (uint32_t ii = 0; ii < _num; ++ii)
		{
			TextureHandle handle = _handles[ii];
			if (isValid(handle) )
			{
				const TextureGL& texture = s_renderGL->m_textures[handle.idx];

				if (0 == colorIdx)
				{
					m_width  = texture.m_width;
					m_height = texture.m_height;
				}

				GLenum attachment = GL_COLOR_ATTACHMENT0 + colorIdx;
				if (isDepth( (TextureFormat::Enum)texture.m_textureFormat) )
				{
					attachment = GL_DEPTH_ATTACHMENT;
				}
				else
				{
					buffers[colorIdx] = attachment;
					++colorIdx;
				}

				if (0 != texture.m_rbo)
				{
					GL_CHECK(glFramebufferRenderbuffer(GL_FRAMEBUFFER
						, attachment
						, GL_RENDERBUFFER
						, texture.m_rbo
						) );
				}
				else
				{
					GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER
						, attachment
						, texture.m_target
						, texture.m_id
						, 0
						) );
				}
				
				needResolve |= (0 != texture.m_rbo) && (0 != texture.m_id);
			}
		}

		m_num = colorIdx;

		if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL) )
		{
			if (0 == colorIdx)
			{
				// When only depth is attached disable draw buffer to avoid
				// GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER.
				GL_CHECK(glDrawBuffer(GL_NONE) );
			}
			else
			{
				GL_CHECK(glDrawBuffers(colorIdx, buffers) );
			}

			// Disable read buffer to avoid GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER.
			GL_CHECK(glReadBuffer(GL_NONE) );
		}

		frameBufferValidate();

		if (needResolve)
		{
			GL_CHECK(glGenFramebuffers(1, &m_fbo[1]) );
			GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, m_fbo[1]) );

			for (uint32_t ii = 0, colorIdx = 0; ii < _num; ++ii)
			{
				TextureHandle handle = _handles[ii];
				if (isValid(handle) )
				{
					const TextureGL& texture = s_renderGL->m_textures[handle.idx];

					if (0 != texture.m_id)
					{
						GLenum attachment = GL_COLOR_ATTACHMENT0 + colorIdx;
						if (!isDepth( (TextureFormat::Enum)texture.m_textureFormat) )
						{
							++colorIdx;
							GL_CHECK(glFramebufferTexture2D(GL_FRAMEBUFFER
								, attachment
								, texture.m_target
								, texture.m_id
								, 0
								) );
						}
					}
				}
			}

			frameBufferValidate();
		}

		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, s_renderGL->m_msaaBackBufferFbo) );
	}

	void FrameBufferGL::create(uint16_t _denseIdx, void* _nwh, uint32_t _width, uint32_t _height, TextureFormat::Enum _depthFormat)
	{
		BX_UNUSED(_depthFormat);
		m_swapChain = s_renderGL->m_glctx.createSwapChain(_nwh);
		m_width     = _width;
		m_height    = _height;
		m_denseIdx  = _denseIdx;
	}

	uint16_t FrameBufferGL::destroy()
	{
		if (0 != m_num)
		{
			GL_CHECK(glDeleteFramebuffers(0 == m_fbo[1] ? 1 : 2, m_fbo) );
			memset(m_fbo, 0, sizeof(m_fbo) );
			m_num = 0;
		}

		if (NULL != m_swapChain)
		{
			s_renderGL->m_glctx.destorySwapChain(m_swapChain);
			m_swapChain = NULL;
		}

		uint16_t denseIdx = m_denseIdx;
		m_denseIdx = UINT16_MAX;
		
		return denseIdx;
	}

	void FrameBufferGL::resolve()
	{
		if (0 != m_fbo[1])
		{
			GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo[0]) );
			GL_CHECK(glReadBuffer(GL_COLOR_ATTACHMENT0) );
			GL_CHECK(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo[1]) );
			GL_CHECK(glBlitFramebuffer(0
				, 0
				, m_width
				, m_height
				, 0
				, 0
				, m_width
				, m_height
				, GL_COLOR_BUFFER_BIT
				, GL_LINEAR
				) );
			GL_CHECK(glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo[0]) );
			GL_CHECK(glReadBuffer(GL_NONE) );
			GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, s_renderGL->m_msaaBackBufferFbo) );
		}
	}

	void RendererContextGL::submit(Frame* _render, ClearQuad& _clearQuad, TextVideoMemBlitter& _textVideoMemBlitter)
	{
		if (1 < m_numWindows
		&&  m_vaoSupport)
		{
			m_vaoSupport = false;
			GL_CHECK(glBindVertexArray(0) );
			GL_CHECK(glDeleteVertexArrays(1, &m_vao) );
			m_vao = 0;
			m_vaoStateCache.invalidate();
		}

		m_glctx.makeCurrent(NULL);

		const GLuint defaultVao = m_vao;
		if (0 != defaultVao)
		{
			GL_CHECK(glBindVertexArray(defaultVao) );
		}

		GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, m_backBufferFbo) );

		updateResolution(_render->m_resolution);

		int64_t elapsed = -bx::getHPCounter();
		int64_t captureElapsed = 0;

		if (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL)
		&& (_render->m_debug & (BGFX_DEBUG_IFH|BGFX_DEBUG_STATS) ) )
		{
			m_queries.begin(0, GL_TIME_ELAPSED);
		}

		if (0 < _render->m_iboffset)
		{
			TransientIndexBuffer* ib = _render->m_transientIb;
			m_indexBuffers[ib->handle.idx].update(0, _render->m_iboffset, ib->data);
		}

		if (0 < _render->m_vboffset)
		{
			TransientVertexBuffer* vb = _render->m_transientVb;
			m_vertexBuffers[vb->handle.idx].update(0, _render->m_vboffset, vb->data);
		}

		_render->sort();

		RenderDraw currentState;
		currentState.clear();
		currentState.m_flags = BGFX_STATE_NONE;
		currentState.m_stencil = packStencil(BGFX_STENCIL_NONE, BGFX_STENCIL_NONE);

		Matrix4 viewProj[BGFX_CONFIG_MAX_VIEWS];
		for (uint32_t ii = 0; ii < BGFX_CONFIG_MAX_VIEWS; ++ii)
		{
			bx::float4x4_mul(&viewProj[ii].un.f4x4, &_render->m_view[ii].un.f4x4, &_render->m_proj[ii].un.f4x4);
		}

		Matrix4 invView;
		Matrix4 invProj;
		Matrix4 invViewProj;
		uint8_t invViewCached = 0xff;
		uint8_t invProjCached = 0xff;
		uint8_t invViewProjCached = 0xff;

		uint16_t programIdx = invalidHandle;
		SortKey key;
		uint8_t view = 0xff;
		FrameBufferHandle fbh = BGFX_INVALID_HANDLE;
		int32_t height = _render->m_resolution.m_height;
		float alphaRef = 0.0f;
		uint32_t blendFactor = 0;

		const uint64_t pt = _render->m_debug&BGFX_DEBUG_WIREFRAME ? BGFX_STATE_PT_LINES : 0;
		uint8_t primIndex = uint8_t(pt>>BGFX_STATE_PT_SHIFT);
		PrimInfo prim = s_primInfo[primIndex];

		uint32_t baseVertex = 0;
		GLuint currentVao = 0;
		bool viewHasScissor = false;
		Rect viewScissorRect;
		viewScissorRect.clear();

		const bool blendIndependentSupported = s_extension[Extension::ARB_draw_buffers_blend].m_supported;
		const bool computeSupported = (BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL) && s_extension[Extension::ARB_compute_shader].m_supported)
									|| BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGLES >= 31)
									;

		uint32_t statsNumPrimsSubmitted[BX_COUNTOF(s_primInfo)] = {};
		uint32_t statsNumPrimsRendered[BX_COUNTOF(s_primInfo)] = {};
		uint32_t statsNumInstances[BX_COUNTOF(s_primInfo)] = {};
		uint32_t statsNumIndices = 0;

		if (0 == (_render->m_debug&BGFX_DEBUG_IFH) )
		{
			GL_CHECK(glBindFramebuffer(GL_FRAMEBUFFER, m_msaaBackBufferFbo) );

			for (uint32_t item = 0, numItems = _render->m_num; item < numItems; ++item)
			{
				const bool isCompute   = key.decode(_render->m_sortKeys[item]);
				const bool viewChanged = key.m_view != view;

				const RenderItem& renderItem = _render->m_renderItem[_render->m_sortValues[item] ];

				if (viewChanged)
				{
					GL_CHECK(glInsertEventMarker(0, s_viewName[key.m_view]) );

					view = key.m_view;
					programIdx = invalidHandle;

					if (_render->m_fb[view].idx != fbh.idx)
					{
						fbh = _render->m_fb[view];
						height = setFrameBuffer(fbh, _render->m_resolution.m_height);
					}

					const Rect& rect = _render->m_rect[view];
					const Rect& scissorRect = _render->m_scissor[view];
					viewHasScissor = !scissorRect.isZero();
					viewScissorRect = viewHasScissor ? scissorRect : rect;

					GL_CHECK(glViewport(rect.m_x, height-rect.m_height-rect.m_y, rect.m_width, rect.m_height) );

					Clear& clear = _render->m_clear[view];

					if (BGFX_CLEAR_NONE != clear.m_flags)
					{
						clearQuad(_clearQuad, rect, clear, height, _render->m_clearColor);
					}

					GL_CHECK(glDisable(GL_STENCIL_TEST) );
					GL_CHECK(glEnable(GL_DEPTH_TEST) );
					GL_CHECK(glDepthFunc(GL_LESS) );
					GL_CHECK(glEnable(GL_CULL_FACE) );
					GL_CHECK(glDisable(GL_BLEND) );
				}

				if (isCompute)
				{
					if (computeSupported)
					{
						const RenderCompute& compute = renderItem.compute;

						ProgramGL& program = m_program[key.m_program];
 						GL_CHECK(glUseProgram(program.m_id) );

						GLbitfield barrier = 0;
						for (uint32_t ii = 0; ii < BGFX_MAX_COMPUTE_BINDINGS; ++ii)
						{
							const ComputeBinding& bind = compute.m_bind[ii];
							if (invalidHandle != bind.m_idx)
							{
								switch (bind.m_type)
								{
								case ComputeBinding::Image:
									{
										const TextureGL& texture = m_textures[bind.m_idx];
										GL_CHECK(glBindImageTexture(ii, texture.m_id, bind.m_mip, GL_FALSE, 0, s_access[bind.m_access], s_imageFormat[bind.m_format]) );
										barrier |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
									}
									break;

								case ComputeBinding::Buffer:
									{
// 										const VertexBufferGL& vertexBuffer = m_vertexBuffers[bind.m_idx];
// 										GL_CHECK(glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ii, vertexBuffer.m_id) ); 
// 										barrier |= GL_SHADER_STORAGE_BARRIER_BIT;
									}
									break;
								}
							}
						}

						if (0 != barrier)
						{
							bool constantsChanged = compute.m_constBegin < compute.m_constEnd;
							rendererUpdateUniforms(this, _render->m_constantBuffer, compute.m_constBegin, compute.m_constEnd);

							if (constantsChanged
							&&  NULL != program.m_constantBuffer)
							{
								commit(*program.m_constantBuffer);
							}

							GL_CHECK(glDispatchCompute(compute.m_numX, compute.m_numY, compute.m_numZ) );
							GL_CHECK(glMemoryBarrier(barrier) );
						}
					}

					continue;
				}

				const RenderDraw& draw = renderItem.draw;

				const uint64_t newFlags = draw.m_flags;
				uint64_t changedFlags = currentState.m_flags ^ draw.m_flags;
				currentState.m_flags = newFlags;

				const uint64_t newStencil = draw.m_stencil;
				uint64_t changedStencil = currentState.m_stencil ^ draw.m_stencil;
				currentState.m_stencil = newStencil;

				if (viewChanged)
				{
					currentState.clear();
					currentState.m_scissor = !draw.m_scissor;
					changedFlags = BGFX_STATE_MASK;
					changedStencil = packStencil(BGFX_STENCIL_MASK, BGFX_STENCIL_MASK);
					currentState.m_flags = newFlags;
					currentState.m_stencil = newStencil;
				}

				uint16_t scissor = draw.m_scissor;
				if (currentState.m_scissor != scissor)
				{
					currentState.m_scissor = scissor;

					if (UINT16_MAX == scissor)
					{
						if (viewHasScissor)
						{
							GL_CHECK(glEnable(GL_SCISSOR_TEST) );
							GL_CHECK(glScissor(viewScissorRect.m_x, height-viewScissorRect.m_height-viewScissorRect.m_y, viewScissorRect.m_width, viewScissorRect.m_height) );
						}
						else
						{
							GL_CHECK(glDisable(GL_SCISSOR_TEST) );
						}
					}
					else
					{
						Rect scissorRect;
						scissorRect.intersect(viewScissorRect, _render->m_rectCache.m_cache[scissor]);
						GL_CHECK(glEnable(GL_SCISSOR_TEST) );
						GL_CHECK(glScissor(scissorRect.m_x, height-scissorRect.m_height-scissorRect.m_y, scissorRect.m_width, scissorRect.m_height) );
					}
				}

				if (0 != changedStencil)
				{
					if (0 != newStencil)
					{
						GL_CHECK(glEnable(GL_STENCIL_TEST) );

						uint32_t bstencil = unpackStencil(1, newStencil);
						uint32_t frontAndBack = bstencil != BGFX_STENCIL_NONE && bstencil != unpackStencil(0, newStencil);

// 						uint32_t bchanged = unpackStencil(1, changedStencil);
// 						if (BGFX_STENCIL_FUNC_RMASK_MASK & bchanged)
// 						{
// 							uint32_t wmask = (bstencil&BGFX_STENCIL_FUNC_RMASK_MASK)>>BGFX_STENCIL_FUNC_RMASK_SHIFT;
// 							GL_CHECK(glStencilMask(wmask) );
// 						}

						for (uint32_t ii = 0, num = frontAndBack+1; ii < num; ++ii)
						{
							uint32_t stencil = unpackStencil(ii, newStencil);
							uint32_t changed = unpackStencil(ii, changedStencil);
							GLenum face = s_stencilFace[frontAndBack+ii];

							if ( (BGFX_STENCIL_TEST_MASK|BGFX_STENCIL_FUNC_REF_MASK|BGFX_STENCIL_FUNC_RMASK_MASK) & changed)
							{
								GLint ref = (stencil&BGFX_STENCIL_FUNC_REF_MASK)>>BGFX_STENCIL_FUNC_REF_SHIFT;
								GLint mask = (stencil&BGFX_STENCIL_FUNC_RMASK_MASK)>>BGFX_STENCIL_FUNC_RMASK_SHIFT;
								uint32_t func = (stencil&BGFX_STENCIL_TEST_MASK)>>BGFX_STENCIL_TEST_SHIFT;
								GL_CHECK(glStencilFuncSeparate(face, s_cmpFunc[func], ref, mask));
							}

							if ( (BGFX_STENCIL_OP_FAIL_S_MASK|BGFX_STENCIL_OP_FAIL_Z_MASK|BGFX_STENCIL_OP_PASS_Z_MASK) & changed)
							{
								uint32_t sfail = (stencil&BGFX_STENCIL_OP_FAIL_S_MASK)>>BGFX_STENCIL_OP_FAIL_S_SHIFT;
								uint32_t zfail = (stencil&BGFX_STENCIL_OP_FAIL_Z_MASK)>>BGFX_STENCIL_OP_FAIL_Z_SHIFT;
								uint32_t zpass = (stencil&BGFX_STENCIL_OP_PASS_Z_MASK)>>BGFX_STENCIL_OP_PASS_Z_SHIFT;
								GL_CHECK(glStencilOpSeparate(face, s_stencilOp[sfail], s_stencilOp[zfail], s_stencilOp[zpass]) );
							}
						}
					}
					else
					{
						GL_CHECK(glDisable(GL_STENCIL_TEST) );
					}
				}

				if ( (0
					 | BGFX_STATE_CULL_MASK
					 | BGFX_STATE_DEPTH_WRITE
					 | BGFX_STATE_DEPTH_TEST_MASK
					 | BGFX_STATE_RGB_WRITE
					 | BGFX_STATE_ALPHA_WRITE
					 | BGFX_STATE_BLEND_MASK
					 | BGFX_STATE_BLEND_EQUATION_MASK
					 | BGFX_STATE_ALPHA_REF_MASK
					 | BGFX_STATE_PT_MASK
					 | BGFX_STATE_POINT_SIZE_MASK
					 | BGFX_STATE_MSAA
					 ) & changedFlags)
				{
					if (BGFX_STATE_CULL_MASK & changedFlags)
					{
						if (BGFX_STATE_CULL_CW & newFlags)
						{
							GL_CHECK(glEnable(GL_CULL_FACE) );
							GL_CHECK(glCullFace(GL_BACK) );
						}
						else if (BGFX_STATE_CULL_CCW & newFlags)
						{
							GL_CHECK(glEnable(GL_CULL_FACE) );
							GL_CHECK(glCullFace(GL_FRONT) );
						}
						else
						{
							GL_CHECK(glDisable(GL_CULL_FACE) );
						}
					}

					if (BGFX_STATE_DEPTH_WRITE & changedFlags)
					{
						GL_CHECK(glDepthMask(!!(BGFX_STATE_DEPTH_WRITE & newFlags) ) );
					}

					if (BGFX_STATE_DEPTH_TEST_MASK & changedFlags)
					{
						uint32_t func = (newFlags&BGFX_STATE_DEPTH_TEST_MASK)>>BGFX_STATE_DEPTH_TEST_SHIFT;

						if (0 != func)
						{
							GL_CHECK(glEnable(GL_DEPTH_TEST) );
							GL_CHECK(glDepthFunc(s_cmpFunc[func]) );
						}
						else
						{
							GL_CHECK(glDisable(GL_DEPTH_TEST) );
						}
					}

					if (BGFX_STATE_ALPHA_REF_MASK & changedFlags)
					{
						uint32_t ref = (newFlags&BGFX_STATE_ALPHA_REF_MASK)>>BGFX_STATE_ALPHA_REF_SHIFT;
						alphaRef = ref/255.0f;
					}

#if BGFX_CONFIG_RENDERER_OPENGL
					if ( (BGFX_STATE_PT_POINTS|BGFX_STATE_POINT_SIZE_MASK) & changedFlags)
					{
						float pointSize = (float)(bx::uint32_max(1, (newFlags&BGFX_STATE_POINT_SIZE_MASK)>>BGFX_STATE_POINT_SIZE_SHIFT) );
						GL_CHECK(glPointSize(pointSize) );
					}

					if (BGFX_STATE_MSAA & changedFlags)
					{
						if (BGFX_STATE_MSAA & newFlags)
						{
							GL_CHECK(glEnable(GL_MULTISAMPLE) );
						}
						else
						{
							GL_CHECK(glDisable(GL_MULTISAMPLE) );
						}
					}
#endif // BGFX_CONFIG_RENDERER_OPENGL

					if ( (BGFX_STATE_ALPHA_WRITE|BGFX_STATE_RGB_WRITE) & changedFlags)
					{
						GLboolean alpha = !!(newFlags&BGFX_STATE_ALPHA_WRITE);
						GLboolean rgb = !!(newFlags&BGFX_STATE_RGB_WRITE);
						GL_CHECK(glColorMask(rgb, rgb, rgb, alpha) );
					}

					if ( (BGFX_STATE_BLEND_MASK|BGFX_STATE_BLEND_EQUATION_MASK|BGFX_STATE_BLEND_INDEPENDENT) & changedFlags
					||  blendFactor != draw.m_rgba)
					{
						if ( (BGFX_STATE_BLEND_MASK|BGFX_STATE_BLEND_EQUATION_MASK|BGFX_STATE_BLEND_INDEPENDENT) & newFlags
						||  blendFactor != draw.m_rgba)
						{
							const bool enabled = !!(BGFX_STATE_BLEND_MASK & newFlags);
							const bool independent = !!(BGFX_STATE_BLEND_INDEPENDENT & newFlags)
								&& blendIndependentSupported
								;

							const uint32_t blend    = uint32_t( (newFlags&BGFX_STATE_BLEND_MASK)>>BGFX_STATE_BLEND_SHIFT);
							const uint32_t equation = uint32_t( (newFlags&BGFX_STATE_BLEND_EQUATION_MASK)>>BGFX_STATE_BLEND_EQUATION_SHIFT);

							const uint32_t srcRGB  = (blend    )&0xf;
							const uint32_t dstRGB  = (blend>> 4)&0xf;
							const uint32_t srcA    = (blend>> 8)&0xf;
							const uint32_t dstA    = (blend>>12)&0xf;

							const uint32_t equRGB = (equation   )&0x7;
							const uint32_t equA   = (equation>>3)&0x7;

							const uint32_t numRt = getNumRt();

							if (!BX_ENABLED(BGFX_CONFIG_RENDERER_OPENGL)
							||  1 >= numRt
							||  !independent)
							{
								if (enabled)
								{
									GL_CHECK(glEnable(GL_BLEND) );
									GL_CHECK(glBlendFuncSeparate(s_blendFactor[srcRGB].m_src
										, s_blendFactor[dstRGB].m_dst
										, s_blendFactor[srcA].m_src
										, s_blendFactor[dstA].m_dst
										) );
									GL_CHECK(glBlendEquationSeparate(s_blendEquation[equRGB], s_blendEquation[equA]) );

									if ( (s_blendFactor[srcRGB].m_factor || s_blendFactor[dstRGB].m_factor)
									&&  blendFactor != draw.m_rgba)
									{
										const uint32_t rgba = draw.m_rgba;
										GLclampf rr = ( (rgba>>24)     )/255.0f;
										GLclampf gg = ( (rgba>>16)&0xff)/255.0f;
										GLclampf bb = ( (rgba>> 8)&0xff)/255.0f;
										GLclampf aa = ( (rgba    )&0xff)/255.0f;

										GL_CHECK(glBlendColor(rr, gg, bb, aa) );
									}
								}
								else
								{
									GL_CHECK(glDisable(GL_BLEND) );
								}
							}
							else
							{
								if (enabled)
								{
									GL_CHECK(glEnablei(GL_BLEND, 0) );
									GL_CHECK(glBlendFuncSeparatei(0
										, s_blendFactor[srcRGB].m_src
										, s_blendFactor[dstRGB].m_dst
										, s_blendFactor[srcA].m_src
										, s_blendFactor[dstA].m_dst
										) );
									GL_CHECK(glBlendEquationSeparatei(0
										, s_blendEquation[equRGB]
										, s_blendEquation[equA]
										) );
								}
								else
								{
									GL_CHECK(glDisablei(GL_BLEND, 0) );
								}

								for (uint32_t ii = 1, rgba = draw.m_rgba; ii < numRt; ++ii, rgba >>= 11)
								{
									if (0 != (rgba&0x7ff) )
									{
										const uint32_t src      = (rgba   )&0xf;
										const uint32_t dst      = (rgba>>4)&0xf;
										const uint32_t equation = (rgba>>8)&0x7;
										GL_CHECK(glEnablei(GL_BLEND, ii) );
										GL_CHECK(glBlendFunci(ii, s_blendFactor[src].m_src, s_blendFactor[dst].m_dst) );
										GL_CHECK(glBlendEquationi(ii, s_blendEquation[equation]) );
									}
									else
									{
										GL_CHECK(glDisablei(GL_BLEND, ii) );
									}
								}
							}
						}
						else
						{
							GL_CHECK(glDisable(GL_BLEND) );
						}

						blendFactor = draw.m_rgba;
					}

					const uint64_t pt = _render->m_debug&BGFX_DEBUG_WIREFRAME ? BGFX_STATE_PT_LINES : newFlags&BGFX_STATE_PT_MASK;
					primIndex = uint8_t(pt>>BGFX_STATE_PT_SHIFT);
					prim = s_primInfo[primIndex];
				}

				bool programChanged = false;
				bool constantsChanged = draw.m_constBegin < draw.m_constEnd;
				bool bindAttribs = false;
				rendererUpdateUniforms(this, _render->m_constantBuffer, draw.m_constBegin, draw.m_constEnd);

				if (key.m_program != programIdx)
				{
					programIdx = key.m_program;
					GLuint id = invalidHandle == programIdx ? 0 : m_program[programIdx].m_id;
					GL_CHECK(glUseProgram(id) );
					programChanged =
						constantsChanged =
						bindAttribs = true;
				}

				if (invalidHandle != programIdx)
				{
					ProgramGL& program = m_program[programIdx];

					if (constantsChanged
					&&  NULL != program.m_constantBuffer)
					{
						commit(*program.m_constantBuffer);
					}

					for (uint32_t ii = 0, num = program.m_numPredefined; ii < num; ++ii)
					{
						PredefinedUniform& predefined = program.m_predefined[ii];
						switch (predefined.m_type)
						{
						case PredefinedUniform::ViewRect:
							{
								float rect[4];
								rect[0] = _render->m_rect[view].m_x;
								rect[1] = _render->m_rect[view].m_y;
								rect[2] = _render->m_rect[view].m_width;
								rect[3] = _render->m_rect[view].m_height;

								GL_CHECK(glUniform4fv(predefined.m_loc
									, 1
									, &rect[0]
								) );
							}
							break;

						case PredefinedUniform::ViewTexel:
							{
								float rect[4];
								rect[0] = 1.0f/float(_render->m_rect[view].m_width);
								rect[1] = 1.0f/float(_render->m_rect[view].m_height);

								GL_CHECK(glUniform4fv(predefined.m_loc
									, 1
									, &rect[0]
								) );
							}
							break;

						case PredefinedUniform::View:
							{
								GL_CHECK(glUniformMatrix4fv(predefined.m_loc
									, 1
									, GL_FALSE
									, _render->m_view[view].un.val
									) );
							}
							break;

						case PredefinedUniform::InvView:
							{
								if (view != invViewCached)
								{
									invViewCached = view;
									bx::float4x4_inverse(&invView.un.f4x4, &_render->m_view[view].un.f4x4);
								}

								GL_CHECK(glUniformMatrix4fv(predefined.m_loc
									, 1
									, GL_FALSE
									, invView.un.val
									) );
							}
							break;

						case PredefinedUniform::Proj:
							{
								GL_CHECK(glUniformMatrix4fv(predefined.m_loc
									, 1
									, GL_FALSE
									, _render->m_proj[view].un.val
									) );
							}
							break;

						case PredefinedUniform::InvProj:
							{
								if (view != invProjCached)
								{
									invProjCached = view;
									bx::float4x4_inverse(&invProj.un.f4x4, &_render->m_proj[view].un.f4x4);
								}

								GL_CHECK(glUniformMatrix4fv(predefined.m_loc
									, 1
									, GL_FALSE
									, invProj.un.val
									) );
							}
							break;

						case PredefinedUniform::ViewProj:
							{
								GL_CHECK(glUniformMatrix4fv(predefined.m_loc
									, 1
									, GL_FALSE
									, viewProj[view].un.val
									) );
							}
							break;

						case PredefinedUniform::InvViewProj:
							{
								if (view != invViewProjCached)
								{
									invViewProjCached = view;
									bx::float4x4_inverse(&invViewProj.un.f4x4, &viewProj[view].un.f4x4);
								}

								GL_CHECK(glUniformMatrix4fv(predefined.m_loc
									, 1
									, GL_FALSE
									, invViewProj.un.val
									) );
							}
							break;

						case PredefinedUniform::Model:
							{
								const Matrix4& model = _render->m_matrixCache.m_cache[draw.m_matrix];
								GL_CHECK(glUniformMatrix4fv(predefined.m_loc
									, bx::uint32_min(predefined.m_count, draw.m_num)
									, GL_FALSE
									, model.un.val
									) );
							}
							break;

						case PredefinedUniform::ModelView:
							{
								Matrix4 modelView;
								const Matrix4& model = _render->m_matrixCache.m_cache[draw.m_matrix];
								bx::float4x4_mul(&modelView.un.f4x4, &model.un.f4x4, &_render->m_view[view].un.f4x4);

								GL_CHECK(glUniformMatrix4fv(predefined.m_loc
									, 1
									, GL_FALSE
									, modelView.un.val
									) );
							}
							break;

						case PredefinedUniform::ModelViewProj:
							{
								Matrix4 modelViewProj;
								const Matrix4& model = _render->m_matrixCache.m_cache[draw.m_matrix];
								bx::float4x4_mul(&modelViewProj.un.f4x4, &model.un.f4x4, &viewProj[view].un.f4x4);

								GL_CHECK(glUniformMatrix4fv(predefined.m_loc
									, 1
									, GL_FALSE
									, modelViewProj.un.val
									) );
							}
							break;

						case PredefinedUniform::AlphaRef:
							{
								GL_CHECK(glUniform1f(predefined.m_loc, alphaRef) );
							}
							break;

						case PredefinedUniform::Count:
							break;
						}
					}

					{
						for (uint32_t stage = 0; stage < BGFX_CONFIG_MAX_TEXTURE_SAMPLERS; ++stage)
						{
							const Sampler& sampler = draw.m_sampler[stage];
							Sampler& current = currentState.m_sampler[stage];
							if (current.m_idx != sampler.m_idx
							||  current.m_flags != sampler.m_flags
							||  programChanged)
							{
								if (invalidHandle != sampler.m_idx)
								{
									TextureGL& texture = m_textures[sampler.m_idx];
									texture.commit(stage, sampler.m_flags);
								}
							}

							current = sampler;
						}
					}

					if (0 != defaultVao
					&&  0 == draw.m_startVertex
					&&  0 == draw.m_instanceDataOffset)
					{
						if (programChanged
						||  currentState.m_vertexBuffer.idx != draw.m_vertexBuffer.idx
						||  currentState.m_indexBuffer.idx != draw.m_indexBuffer.idx
						||  currentState.m_instanceDataBuffer.idx != draw.m_instanceDataBuffer.idx
						||  currentState.m_instanceDataOffset != draw.m_instanceDataOffset
						||  currentState.m_instanceDataStride != draw.m_instanceDataStride)
						{
							bx::HashMurmur2A murmur;
							murmur.begin();
							murmur.add(draw.m_vertexBuffer.idx);
							murmur.add(draw.m_indexBuffer.idx);
							murmur.add(draw.m_instanceDataBuffer.idx);
							murmur.add(draw.m_instanceDataOffset);
							murmur.add(draw.m_instanceDataStride);
							murmur.add(programIdx);
							uint32_t hash = murmur.end();

							currentState.m_vertexBuffer = draw.m_vertexBuffer;
							currentState.m_indexBuffer = draw.m_indexBuffer;
							currentState.m_instanceDataOffset = draw.m_instanceDataOffset;
							currentState.m_instanceDataStride = draw.m_instanceDataStride;
							baseVertex = draw.m_startVertex;

							GLuint id = m_vaoStateCache.find(hash);
							if (UINT32_MAX != id)
							{
								currentVao = id;
								GL_CHECK(glBindVertexArray(id) );
							}
							else
							{
								id = m_vaoStateCache.add(hash);
								currentVao = id;
								GL_CHECK(glBindVertexArray(id) );

								ProgramGL& program = m_program[programIdx];
								program.add(hash);

								if (isValid(draw.m_vertexBuffer) )
								{
									VertexBufferGL& vb = m_vertexBuffers[draw.m_vertexBuffer.idx];
									vb.add(hash);
									GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, vb.m_id) );

									uint16_t decl = !isValid(vb.m_decl) ? draw.m_vertexDecl.idx : vb.m_decl.idx;
									program.bindAttributes(m_vertexDecls[decl], draw.m_startVertex);

									if (isValid(draw.m_instanceDataBuffer) )
									{
										VertexBufferGL& instanceVb = m_vertexBuffers[draw.m_instanceDataBuffer.idx];
										instanceVb.add(hash);
										GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, instanceVb.m_id) );
										program.bindInstanceData(draw.m_instanceDataStride, draw.m_instanceDataOffset);
									}
								}
								else
								{
									GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, 0) );
								}

								if (isValid(draw.m_indexBuffer) )
								{
									IndexBufferGL& ib = m_indexBuffers[draw.m_indexBuffer.idx];
									ib.add(hash);
									GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.m_id) );
								}
								else
								{
									GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0) );
								}
							}
						}
					}
					else
					{
						if (0 != defaultVao
						&&  0 != currentVao)
						{
							GL_CHECK(glBindVertexArray(defaultVao) );
							currentState.m_vertexBuffer.idx = invalidHandle;
							currentState.m_indexBuffer.idx = invalidHandle;
							bindAttribs = true;
							currentVao = 0;
						}

						if (programChanged
						||  currentState.m_vertexBuffer.idx != draw.m_vertexBuffer.idx
						||  currentState.m_instanceDataBuffer.idx != draw.m_instanceDataBuffer.idx
						||  currentState.m_instanceDataOffset != draw.m_instanceDataOffset
						||  currentState.m_instanceDataStride != draw.m_instanceDataStride)
						{
							currentState.m_vertexBuffer = draw.m_vertexBuffer;
							currentState.m_instanceDataBuffer.idx = draw.m_instanceDataBuffer.idx;
							currentState.m_instanceDataOffset = draw.m_instanceDataOffset;
							currentState.m_instanceDataStride = draw.m_instanceDataStride;

							uint16_t handle = draw.m_vertexBuffer.idx;
							if (invalidHandle != handle)
							{
								VertexBufferGL& vb = m_vertexBuffers[handle];
								GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, vb.m_id) );
								bindAttribs = true;
							}
							else
							{
								GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, 0) );
							}
						}

						if (currentState.m_indexBuffer.idx != draw.m_indexBuffer.idx)
						{
							currentState.m_indexBuffer = draw.m_indexBuffer;

							uint16_t handle = draw.m_indexBuffer.idx;
							if (invalidHandle != handle)
							{
								IndexBufferGL& ib = m_indexBuffers[handle];
								GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.m_id) );
							}
							else
							{
								GL_CHECK(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0) );
							}
						}

						if (isValid(currentState.m_vertexBuffer) )
						{
							if (baseVertex != draw.m_startVertex
							||  bindAttribs)
							{
								baseVertex = draw.m_startVertex;
								const VertexBufferGL& vb = m_vertexBuffers[draw.m_vertexBuffer.idx];
								uint16_t decl = !isValid(vb.m_decl) ? draw.m_vertexDecl.idx : vb.m_decl.idx;
								const ProgramGL& program = m_program[programIdx];
								program.bindAttributes(m_vertexDecls[decl], draw.m_startVertex);

								if (isValid(draw.m_instanceDataBuffer) )
								{
									GL_CHECK(glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffers[draw.m_instanceDataBuffer.idx].m_id) );
									program.bindInstanceData(draw.m_instanceDataStride, draw.m_instanceDataOffset);
								}
							}
						}
					}

					if (isValid(currentState.m_vertexBuffer) )
					{
						uint32_t numVertices = draw.m_numVertices;
						if (UINT32_MAX == numVertices)
						{
							const VertexBufferGL& vb = m_vertexBuffers[currentState.m_vertexBuffer.idx];
							uint16_t decl = !isValid(vb.m_decl) ? draw.m_vertexDecl.idx : vb.m_decl.idx;
							const VertexDecl& vertexDecl = m_vertexDecls[decl];
							numVertices = vb.m_size/vertexDecl.m_stride;
						}

						uint32_t numIndices = 0;
						uint32_t numPrimsSubmitted = 0;
						uint32_t numInstances = 0;
						uint32_t numPrimsRendered = 0;

						if (isValid(draw.m_indexBuffer) )
						{
							if (UINT32_MAX == draw.m_numIndices)
							{
								numIndices = m_indexBuffers[draw.m_indexBuffer.idx].m_size/2;
								numPrimsSubmitted = numIndices/prim.m_div - prim.m_sub;
								numInstances = draw.m_numInstances;
								numPrimsRendered = numPrimsSubmitted*draw.m_numInstances;

								GL_CHECK(glDrawElementsInstanced(prim.m_type
									, numIndices
									, GL_UNSIGNED_SHORT
									, (void*)0
									, draw.m_numInstances
									) );
							}
							else if (prim.m_min <= draw.m_numIndices)
							{
								numIndices = draw.m_numIndices;
								numPrimsSubmitted = numIndices/prim.m_div - prim.m_sub;
								numInstances = draw.m_numInstances;
								numPrimsRendered = numPrimsSubmitted*draw.m_numInstances;

								GL_CHECK(glDrawElementsInstanced(prim.m_type
									, numIndices
									, GL_UNSIGNED_SHORT
									, (void*)(uintptr_t)(draw.m_startIndex*2)
									, draw.m_numInstances
									) );
							}
						}
						else
						{
							numPrimsSubmitted = numVertices/prim.m_div - prim.m_sub;
							numInstances = draw.m_numInstances;
							numPrimsRendered = numPrimsSubmitted*draw.m_numInstances;

							GL_CHECK(glDrawArraysInstanced(prim.m_type
								, 0
								, numVertices
								, draw.m_numInstances
								) );
						}

						statsNumPrimsSubmitted[primIndex] += numPrimsSubmitted;
						statsNumPrimsRendered[primIndex]  += numPrimsRendered;
						statsNumInstances[primIndex]      += numInstances;
						statsNumIndices += numIndices;
					}
				}
			}

			blitMsaaFbo();

			if (0 < _render->m_num)
			{
				captureElapsed = -bx::getHPCounter();
				capture();
				captureElapsed += bx::getHPCounter();
			}
		}

		m_glctx.makeCurrent(NULL);
		int64_t now = bx::getHPCounter();
		elapsed += now;

		static int64_t last = now;
		int64_t frameTime = now - last;
		last = now;

		static int64_t min = frameTime;
		static int64_t max = frameTime;
		min = min > frameTime ? frameTime : min;
		max = max < frameTime ? frameTime : max;

		if (_render->m_debug & (BGFX_DEBUG_IFH|BGFX_DEBUG_STATS) )
		{
			double elapsedGpuMs = 0.0;
#if BGFX_CONFIG_RENDERER_OPENGL
			m_queries.end(GL_TIME_ELAPSED);
			uint64_t elapsedGl = m_queries.getResult(0);
			elapsedGpuMs = double(elapsedGl)/1e6;
#endif // BGFX_CONFIG_RENDERER_OPENGL

			TextVideoMem& tvm = m_textVideoMem;

			static int64_t next = now;

			if (now >= next)
			{
				next = now + bx::getHPFrequency();
				double freq = double(bx::getHPFrequency() );
				double toMs = 1000.0/freq;

				tvm.clear();
				uint16_t pos = 0;
				tvm.printf(0, pos++, BGFX_CONFIG_DEBUG ? 0x89 : 0x8f, " %s / " BX_COMPILER_NAME " / " BX_CPU_NAME " / " BX_ARCH_NAME " / " BX_PLATFORM_NAME " "
					, getRendererName()
					);
				tvm.printf(0, pos++, 0x0f, "      Vendor: %s", m_vendor);
				tvm.printf(0, pos++, 0x0f, "    Renderer: %s", m_renderer);
				tvm.printf(0, pos++, 0x0f, "     Version: %s", m_version);
				tvm.printf(0, pos++, 0x0f, "GLSL version: %s", m_glslVersion);

				pos = 10;
				tvm.printf(10, pos++, 0x8e, "      Frame CPU: %7.3f, % 7.3f \x1f, % 7.3f \x1e [ms] / % 6.2f FPS "
					, double(frameTime)*toMs
					, double(min)*toMs
					, double(max)*toMs
					, freq/frameTime
					);

				const uint32_t msaa = (m_resolution.m_flags&BGFX_RESET_MSAA_MASK)>>BGFX_RESET_MSAA_SHIFT;
				tvm.printf(10, pos++, 0x8e, "    Reset flags: [%c] vsync, [%c] MSAAx%d "
					, !!(m_resolution.m_flags&BGFX_RESET_VSYNC) ? '\xfe' : ' '
					, 0 != msaa ? '\xfe' : ' '
					, 1<<msaa
					);

				double elapsedCpuMs = double(elapsed)*toMs;
				tvm.printf(10, pos++, 0x8e, " Draw calls: %4d / CPU %3.4f [ms] %c GPU %3.4f [ms]"
					, _render->m_num
					, elapsedCpuMs
					, elapsedCpuMs > elapsedGpuMs ? '>' : '<'
					, elapsedGpuMs
					);
				for (uint32_t ii = 0; ii < BX_COUNTOF(s_primInfo); ++ii)
				{
					tvm.printf(10, pos++, 0x8e, "   %8s: %7d (#inst: %5d), submitted: %7d"
						, s_primName[ii]
						, statsNumPrimsRendered[ii]
						, statsNumInstances[ii]
						, statsNumPrimsSubmitted[ii]
						);
				}

				tvm.printf(10, pos++, 0x8e, "    Indices: %7d", statsNumIndices);
				tvm.printf(10, pos++, 0x8e, "   DVB size: %7d", _render->m_vboffset);
				tvm.printf(10, pos++, 0x8e, "   DIB size: %7d", _render->m_iboffset);

				pos++;
				tvm.printf(10, pos++, 0x8e, " State cache:     ");
				tvm.printf(10, pos++, 0x8e, " VAO    | Sampler ");
				tvm.printf(10, pos++, 0x8e, " %6d | %6d  "
					, m_vaoStateCache.getCount()
					, m_samplerStateCache.getCount()
					);
				pos++;

				double captureMs = double(captureElapsed)*toMs;
				tvm.printf(10, pos++, 0x8e, "    Capture: %3.4f [ms]", captureMs);

#if BGFX_CONFIG_RENDERER_OPENGL
				if (s_extension[Extension::ATI_meminfo].m_supported)
				{
					GLint vboFree[4];
					GL_CHECK(glGetIntegerv(GL_VBO_FREE_MEMORY_ATI, vboFree) );

					GLint texFree[4];
					GL_CHECK(glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, texFree) );

					GLint rbfFree[4];
					GL_CHECK(glGetIntegerv(GL_RENDERBUFFER_FREE_MEMORY_ATI, rbfFree) );

					pos++;
					tvm.printf(10, pos++, 0x8c, " -------------|    free|  free b|     aux|  aux fb");

					char tmp0[16];
					char tmp1[16];
					char tmp2[16];
					char tmp3[16];

					bx::prettify(tmp0, BX_COUNTOF(tmp0), vboFree[0]);
					bx::prettify(tmp1, BX_COUNTOF(tmp1), vboFree[1]);
					bx::prettify(tmp2, BX_COUNTOF(tmp2), vboFree[2]);
					bx::prettify(tmp3, BX_COUNTOF(tmp3), vboFree[3]);
					tvm.printf(10, pos++, 0x8e, "           VBO: %10s, %10s, %10s, %10s", tmp0, tmp1, tmp2, tmp3);

					bx::prettify(tmp0, BX_COUNTOF(tmp0), texFree[0]);
					bx::prettify(tmp1, BX_COUNTOF(tmp1), texFree[1]);
					bx::prettify(tmp2, BX_COUNTOF(tmp2), texFree[2]);
					bx::prettify(tmp3, BX_COUNTOF(tmp3), texFree[3]);
					tvm.printf(10, pos++, 0x8e, "       Texture: %10s, %10s, %10s, %10s", tmp0, tmp1, tmp2, tmp3);

					bx::prettify(tmp0, BX_COUNTOF(tmp0), rbfFree[0]);
					bx::prettify(tmp1, BX_COUNTOF(tmp1), rbfFree[1]);
					bx::prettify(tmp2, BX_COUNTOF(tmp2), rbfFree[2]);
					bx::prettify(tmp3, BX_COUNTOF(tmp3), rbfFree[3]);
					tvm.printf(10, pos++, 0x8e, " Render Buffer: %10s, %10s, %10s, %10s", tmp0, tmp1, tmp2, tmp3);
				}
				else if (s_extension[Extension::NVX_gpu_memory_info].m_supported)
				{
					GLint dedicated;
					GL_CHECK(glGetIntegerv(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &dedicated) );

					GLint totalAvail;
					GL_CHECK(glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalAvail) );
					GLint currAvail;
					GL_CHECK(glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &currAvail) );

					GLint evictedCount;
					GL_CHECK(glGetIntegerv(GL_GPU_MEMORY_INFO_EVICTION_COUNT_NVX, &evictedCount) );

					GLint evictedMemory;
					GL_CHECK(glGetIntegerv(GL_GPU_MEMORY_INFO_EVICTED_MEMORY_NVX, &evictedMemory) );

					pos += 2;

					char tmp0[16];
					char tmp1[16];

					bx::prettify(tmp0, BX_COUNTOF(tmp0), dedicated);
					tvm.printf(10, pos++, 0x8e, " Dedicated: %10s", tmp0);

					bx::prettify(tmp0, BX_COUNTOF(tmp0), currAvail);
					bx::prettify(tmp1, BX_COUNTOF(tmp1), totalAvail);
					tvm.printf(10, pos++, 0x8e, " Available: %10s / %10s", tmp0, tmp1);

					bx::prettify(tmp0, BX_COUNTOF(tmp0), evictedCount);
					bx::prettify(tmp1, BX_COUNTOF(tmp1), evictedMemory);
					tvm.printf(10, pos++, 0x8e, "  Eviction: %10s / %10s", tmp0, tmp1);
				}
#endif // BGFX_CONFIG_RENDERER_OPENGL

				uint8_t attr[2] = { 0x89, 0x8a };
				uint8_t attrIndex = _render->m_waitSubmit < _render->m_waitRender;

				pos++;
				tvm.printf(10, pos++, attr[attrIndex&1], "Submit wait: %3.4f [ms]", double(_render->m_waitSubmit)*toMs);
				tvm.printf(10, pos++, attr[(attrIndex+1)&1], "Render wait: %3.4f [ms]", double(_render->m_waitRender)*toMs);

				min = frameTime;
				max = frameTime;
			}

			blit(this, _textVideoMemBlitter, tvm);
		}
		else if (_render->m_debug & BGFX_DEBUG_TEXT)
		{
			blit(this, _textVideoMemBlitter, _render->m_textVideoMem);
		}

		GL_CHECK(glFrameTerminatorGREMEDY() );
	}
} // namespace bgfx

#else

namespace bgfx
{
	RendererContextI* rendererCreateGL()
	{
		return NULL;
	}

	void rendererDestroyGL()
	{
	}
} // namespace bgfx

#endif // (BGFX_CONFIG_RENDERER_OPENGLES || BGFX_CONFIG_RENDERER_OPENGL)
/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include "bgfx_p.h"

#if BGFX_CONFIG_RENDERER_NULL

namespace bgfx
{
	struct RendererContextNULL : public RendererContextI
	{
		RendererContextNULL()
		{
		}

		~RendererContextNULL()
		{
		}

		RendererType::Enum getRendererType() const BX_OVERRIDE
		{
			return RendererType::Null;
		}

		const char* getRendererName() const BX_OVERRIDE
		{
			return BGFX_RENDERER_NULL_NAME;
		}

		void flip() BX_OVERRIDE
		{
		}

		void createIndexBuffer(IndexBufferHandle /*_handle*/, Memory* /*_mem*/) BX_OVERRIDE
		{
		}

		void destroyIndexBuffer(IndexBufferHandle /*_handle*/) BX_OVERRIDE
		{
		}

		void createVertexDecl(VertexDeclHandle /*_handle*/, const VertexDecl& /*_decl*/) BX_OVERRIDE
		{
		}

		void destroyVertexDecl(VertexDeclHandle /*_handle*/) BX_OVERRIDE
		{
		}

		void createVertexBuffer(VertexBufferHandle /*_handle*/, Memory* /*_mem*/, VertexDeclHandle /*_declHandle*/) BX_OVERRIDE
		{
		}

		void destroyVertexBuffer(VertexBufferHandle /*_handle*/) BX_OVERRIDE
		{
		}

		void createDynamicIndexBuffer(IndexBufferHandle /*_handle*/, uint32_t /*_size*/) BX_OVERRIDE
		{
		}

		void updateDynamicIndexBuffer(IndexBufferHandle /*_handle*/, uint32_t /*_offset*/, uint32_t /*_size*/, Memory* /*_mem*/) BX_OVERRIDE
		{
		}

		void destroyDynamicIndexBuffer(IndexBufferHandle /*_handle*/) BX_OVERRIDE
		{
		}

		void createDynamicVertexBuffer(VertexBufferHandle /*_handle*/, uint32_t /*_size*/) BX_OVERRIDE
		{
		}

		void updateDynamicVertexBuffer(VertexBufferHandle /*_handle*/, uint32_t /*_offset*/, uint32_t /*_size*/, Memory* /*_mem*/) BX_OVERRIDE
		{
		}

		void destroyDynamicVertexBuffer(VertexBufferHandle /*_handle*/) BX_OVERRIDE
		{
		}

		void createShader(ShaderHandle /*_handle*/, Memory* /*_mem*/) BX_OVERRIDE
		{
		}

		void destroyShader(ShaderHandle /*_handle*/) BX_OVERRIDE
		{
		}

		void createProgram(ProgramHandle /*_handle*/, ShaderHandle /*_vsh*/, ShaderHandle /*_fsh*/) BX_OVERRIDE
		{
		}

		void destroyProgram(ProgramHandle /*_handle*/) BX_OVERRIDE
		{
		}

		void createTexture(TextureHandle /*_handle*/, Memory* /*_mem*/, uint32_t /*_flags*/, uint8_t /*_skip*/) BX_OVERRIDE
		{
		}

		void updateTextureBegin(TextureHandle /*_handle*/, uint8_t /*_side*/, uint8_t /*_mip*/) BX_OVERRIDE
		{
		}

		void updateTexture(TextureHandle /*_handle*/, uint8_t /*_side*/, uint8_t /*_mip*/, const Rect& /*_rect*/, uint16_t /*_z*/, uint16_t /*_depth*/, uint16_t /*_pitch*/, const Memory* /*_mem*/) BX_OVERRIDE
		{
		}

		void updateTextureEnd() BX_OVERRIDE
		{
		}

		void destroyTexture(TextureHandle /*_handle*/) BX_OVERRIDE
		{
		}

		void createFrameBuffer(FrameBufferHandle /*_handle*/, uint8_t /*_num*/, const TextureHandle* /*_textureHandles*/) BX_OVERRIDE
		{
		}

		void createFrameBuffer(FrameBufferHandle /*_handle*/, void* /*_nwh*/, uint32_t /*_width*/, uint32_t /*_height*/, TextureFormat::Enum /*_depthFormat*/) BX_OVERRIDE
		{
		}

		void destroyFrameBuffer(FrameBufferHandle /*_handle*/) BX_OVERRIDE
		{
		}

		void createUniform(UniformHandle /*_handle*/, UniformType::Enum /*_type*/, uint16_t /*_num*/, const char* /*_name*/) BX_OVERRIDE
		{
		}

		void destroyUniform(UniformHandle /*_handle*/) BX_OVERRIDE
		{
		}

		void saveScreenShot(const char* /*_filePath*/) BX_OVERRIDE
		{
		}

		void updateViewName(uint8_t /*_id*/, const char* /*_name*/) BX_OVERRIDE
		{
		}

		void updateUniform(uint16_t /*_loc*/, const void* /*_data*/, uint32_t /*_size*/) BX_OVERRIDE
		{
		}

		void setMarker(const char* /*_marker*/, uint32_t /*_size*/) BX_OVERRIDE
		{
		}

		void submit(Frame* /*_render*/, ClearQuad& /*_clearQuad*/, TextVideoMemBlitter& /*_textVideoMemBlitter*/) BX_OVERRIDE
		{
		}

		void blitSetup(TextVideoMemBlitter& /*_blitter*/) BX_OVERRIDE
		{
		}

		void blitRender(TextVideoMemBlitter& /*_blitter*/, uint32_t /*_numIndices*/) BX_OVERRIDE
		{
		}
	};

	static RendererContextNULL* s_renderNULL;

	RendererContextI* rendererCreateNULL()
	{
		s_renderNULL = BX_NEW(g_allocator, RendererContextNULL);
		return s_renderNULL;
	}

	void rendererDestroyNULL()
	{
		BX_DELETE(g_allocator, s_renderNULL);
		s_renderNULL = NULL;
	}
} // namespace bgfx

#else

namespace bgfx
{
	RendererContextI* rendererCreateNULL()
	{
		return NULL;
	}

	void rendererDestroyNULL()
	{
	}
} // namespace bgfx

#endif // BGFX_CONFIG_RENDERER_NULL
/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include <string.h>
#include <bx/debug.h>
#include <bx/hash.h>
#include <bx/uint32_t.h>
#include <bx/string.h>
#include <bx/readerwriter.h>

#include "config.h"
#include "vertexdecl.h"

namespace bgfx
{
	static const uint8_t s_attribTypeSizeDx9[AttribType::Count][4] =
	{
		{  4,  4,  4,  4 },
		{  4,  4,  8,  8 },
		{  4,  4,  8,  8 },
		{  4,  8, 12, 16 },
	};

	static const uint8_t s_attribTypeSizeDx1x[AttribType::Count][4] =
	{
		{  1,  2,  4,  4 },
		{  2,  4,  8,  8 },
		{  2,  4,  8,  8 },
		{  4,  8, 12, 16 },
	};

	static const uint8_t s_attribTypeSizeGl[AttribType::Count][4] =
	{
		{  1,  2,  4,  4 },
		{  2,  4,  6,  8 },
		{  2,  4,  6,  8 },
		{  4,  8, 12, 16 },
	};

	static const uint8_t (*s_attribTypeSize[])[AttribType::Count][4] =
	{
#if BGFX_CONFIG_RENDERER_DIRECT3D9
		&s_attribTypeSizeDx9,
#elif BGFX_CONFIG_RENDERER_DIRECT3D11 || BGFX_CONFIG_RENDERER_DIRECT3D12
		&s_attribTypeSizeDx1x,
#elif BGFX_CONFIG_RENDERER_OPENGL || BGFX_CONFIG_RENDERER_OPENGLES
		&s_attribTypeSizeGl,
#else
		&s_attribTypeSizeDx9,
#endif // BGFX_CONFIG_RENDERER_
		&s_attribTypeSizeDx9,  // Direct3D9
		&s_attribTypeSizeDx1x, // Direct3D11
		&s_attribTypeSizeDx1x, // Direct3D12
		&s_attribTypeSizeGl,   // OpenGLES
		&s_attribTypeSizeGl,   // OpenGL
	};
	BX_STATIC_ASSERT(BX_COUNTOF(s_attribTypeSize) == bgfx::RendererType::Count);

	void initAttribTypeSizeTable(RendererType::Enum _type)
	{
		s_attribTypeSize[0] = s_attribTypeSize[_type];
	}

	void dbgPrintfVargs(const char* _format, va_list _argList)
	{
		char temp[8192];
		char* out = temp;
		int32_t len = bx::vsnprintf(out, sizeof(temp), _format, _argList);
		if ( (int32_t)sizeof(temp) < len)
		{
			out = (char*)alloca(len+1);
			len = bx::vsnprintf(out, len, _format, _argList);
		}
		out[len] = '\0';
		bx::debugOutput(out);
	}

	void dbgPrintf(const char* _format, ...)
	{
		va_list argList;
		va_start(argList, _format);
		dbgPrintfVargs(_format, argList);
		va_end(argList);
	}

	VertexDecl::VertexDecl()
	{
		// BK - struct need to have ctor to qualify as non-POD data.
		// Need this to catch programming errors when serializing struct.
	}

	VertexDecl& VertexDecl::begin(RendererType::Enum _renderer)
	{
		m_hash = _renderer; // use hash to store renderer type while building VertexDecl.
		m_stride = 0;
		memset(m_attributes, 0xff, sizeof(m_attributes) );
		memset(m_offset, 0, sizeof(m_offset) );

		return *this;
	}

	void VertexDecl::end()
	{
		bx::HashMurmur2A murmur;
		murmur.begin();
		murmur.add(m_attributes, sizeof(m_attributes) );
		murmur.add(m_offset, sizeof(m_offset) );
		m_hash = murmur.end();
	}

	VertexDecl& VertexDecl::add(Attrib::Enum _attrib, uint8_t _num, AttribType::Enum _type, bool _normalized, bool _asInt)
	{
		const uint8_t encodedNorm = (_normalized&1)<<6;
		const uint8_t encodedType = (_type&3)<<3;
		const uint8_t encodedNum  = (_num-1)&3;
		const uint8_t encodeAsInt = (_asInt&(!!"\x1\x1\x0\x0"[_type]) )<<7;
		m_attributes[_attrib] = encodedNorm|encodedType|encodedNum|encodeAsInt;

		m_offset[_attrib] = m_stride;
		m_stride += (*s_attribTypeSize[m_hash])[_type][_num-1];

		return *this;
	}

	VertexDecl& VertexDecl::skip(uint8_t _num)
	{
		m_stride += _num;

		return *this;
	}

	void VertexDecl::decode(Attrib::Enum _attrib, uint8_t& _num, AttribType::Enum& _type, bool& _normalized, bool& _asInt) const
	{
		uint8_t val = m_attributes[_attrib];
		_num        = (val&3)+1;
		_type       = AttribType::Enum((val>>3)&3);
		_normalized = !!(val&(1<<6) );
		_asInt      = !!(val&(1<<7) );
	}

	static const char* s_attrName[] = 
	{
		"Attrib::Position",
		"Attrib::Normal",
		"Attrib::Tangent",
		"Attrib::Bitangent",
		"Attrib::Color0",
		"Attrib::Color1",
		"Attrib::Indices",
		"Attrib::Weights",
		"Attrib::TexCoord0",
		"Attrib::TexCoord1",
		"Attrib::TexCoord2",
		"Attrib::TexCoord3",
		"Attrib::TexCoord4",
		"Attrib::TexCoord5",
		"Attrib::TexCoord6",
		"Attrib::TexCoord7",
	};
	BX_STATIC_ASSERT(BX_COUNTOF(s_attrName) == Attrib::Count);

	const char* getAttribName(Attrib::Enum _attr)
	{
		return s_attrName[_attr];
	}

	void dump(const VertexDecl& _decl)
	{
		if (BX_ENABLED(BGFX_CONFIG_DEBUG) )
		{
			dbgPrintf("vertexdecl %08x (%08x), stride %d\n"
				, _decl.m_hash
				, bx::hashMurmur2A(_decl.m_attributes)
				, _decl.m_stride
				);

			for (uint32_t attr = 0; attr < Attrib::Count; ++attr)
			{
				if (0xff != _decl.m_attributes[attr])
				{
					uint8_t num;
					AttribType::Enum type;
					bool normalized;
					bool asInt;
					_decl.decode(Attrib::Enum(attr), num, type, normalized, asInt);

					dbgPrintf("\tattr %d - %s, num %d, type %d, norm %d, asint %d, offset %d\n"
						, attr
						, getAttribName(Attrib::Enum(attr) )
						, num
						, type
						, normalized
						, asInt
						, _decl.m_offset[attr]
					);
				}
			}
		}
	}

	struct AttribToId
	{
		Attrib::Enum attr;
		uint16_t id;
	};

	static AttribToId s_attribToId[] =
	{
		// NOTICE:
		// Attrib must be in order how it appears in Attrib::Enum! id is
		// unique and should not be changed if new Attribs are added.
		{ Attrib::Position,  0x0001 },
		{ Attrib::Normal,    0x0002 },
		{ Attrib::Tangent,   0x0003 },
		{ Attrib::Bitangent, 0x0004 },
		{ Attrib::Color0,    0x0005 },
		{ Attrib::Color1,    0x0006 },
		{ Attrib::Indices,   0x000e },
		{ Attrib::Weight,    0x000f },
		{ Attrib::TexCoord0, 0x0010 },
		{ Attrib::TexCoord1, 0x0011 },
		{ Attrib::TexCoord2, 0x0012 },
		{ Attrib::TexCoord3, 0x0013 },
		{ Attrib::TexCoord4, 0x0014 },
		{ Attrib::TexCoord5, 0x0015 },
		{ Attrib::TexCoord6, 0x0016 },
		{ Attrib::TexCoord7, 0x0017 },
	};
	BX_STATIC_ASSERT(BX_COUNTOF(s_attribToId) == Attrib::Count);

	Attrib::Enum idToAttrib(uint16_t id)
	{
		for (uint32_t ii = 0; ii < BX_COUNTOF(s_attribToId); ++ii)
		{
			if (s_attribToId[ii].id == id)
			{
				return s_attribToId[ii].attr;
			}
		}

		return Attrib::Count;
	}

	uint16_t attribToId(Attrib::Enum _attr)
	{
		return s_attribToId[_attr].id;
	}

	struct AttribTypeToId
	{
		AttribType::Enum type;
		uint16_t id;
	};

	static AttribTypeToId s_attribTypeToId[] =
	{
		// NOTICE:
		// AttribType must be in order how it appears in AttribType::Enum!
		// id is unique and should not be changed if new AttribTypes are
		// added.
		{ AttribType::Uint8, 0x0001 },
		{ AttribType::Int16, 0x0002 },
		{ AttribType::Half,  0x0003 },
		{ AttribType::Float, 0x0004 },
	};
	BX_STATIC_ASSERT(BX_COUNTOF(s_attribTypeToId) == AttribType::Count);

	AttribType::Enum idToAttribType(uint16_t id)
	{
		for (uint32_t ii = 0; ii < BX_COUNTOF(s_attribTypeToId); ++ii)
		{
			if (s_attribTypeToId[ii].id == id)
			{
				return s_attribTypeToId[ii].type;
			}
		}

		return AttribType::Count;
	}

	uint16_t attribTypeToId(AttribType::Enum _attr)
	{
		return s_attribTypeToId[_attr].id;
	}

	int32_t write(bx::WriterI* _writer, const VertexDecl& _decl)
	{
		int32_t total = 0;
		uint8_t numAttrs = 0;

		for (uint32_t attr = 0; attr < Attrib::Count; ++attr)
		{
			numAttrs += 0xff == _decl.m_attributes[attr] ? 0 : 1;
		}

		total += bx::write(_writer, numAttrs);
		total += bx::write(_writer, _decl.m_stride);

		for (uint32_t attr = 0; attr < Attrib::Count; ++attr)
		{
			if (0xff != _decl.m_attributes[attr])
			{
				uint8_t num;
				AttribType::Enum type;
				bool normalized;
				bool asInt;
				_decl.decode(Attrib::Enum(attr), num, type, normalized, asInt);
				total += bx::write(_writer, _decl.m_offset[attr]);
				total += bx::write(_writer, s_attribToId[attr].id);
				total += bx::write(_writer, num);
				total += bx::write(_writer, s_attribTypeToId[type].id);
				total += bx::write(_writer, normalized);
				total += bx::write(_writer, asInt);
			}
		}

		return total;
	}

	int32_t read(bx::ReaderI* _reader, VertexDecl& _decl)
	{
		int32_t total = 0;

		uint8_t numAttrs;
		total += bx::read(_reader, numAttrs);

		uint16_t stride;
		total += bx::read(_reader, stride);

		_decl.begin();

		for (uint32_t ii = 0; ii < numAttrs; ++ii)
		{
			uint16_t offset;
			total += bx::read(_reader, offset);

			uint16_t attribId = 0;
			total += bx::read(_reader, attribId);

			uint8_t num;
			total += bx::read(_reader, num);

			uint16_t attribTypeId;
			total += bx::read(_reader, attribTypeId);

			bool normalized;
			total += bx::read(_reader, normalized);

			bool asInt;
			total += bx::read(_reader, asInt);

			Attrib::Enum     attr = idToAttrib(attribId);
			AttribType::Enum type = idToAttribType(attribTypeId);
			if (Attrib::Count != attr
			&&  AttribType::Count != type)
			{
				_decl.add(attr, num, type, normalized, asInt);
				_decl.m_offset[attr] = offset;
			}
		}

		_decl.end();
		_decl.m_stride = stride;

		return total;
	}

	void vertexPack(const float _input[4], bool _inputNormalized, Attrib::Enum _attr, const VertexDecl& _decl, void* _data, uint32_t _index)
	{
		if (!_decl.has(_attr) )
		{
			return;
		}

		uint32_t stride = _decl.getStride();
		uint8_t* data = (uint8_t*)_data + _index*stride + _decl.getOffset(_attr);

		uint8_t num;
		AttribType::Enum type;
		bool normalized;
		bool asInt;
		_decl.decode(_attr, num, type, normalized, asInt);

		switch (type)
		{
		default:
		case AttribType::Uint8:
			{
				uint8_t* packed = (uint8_t*)data;
				if (_inputNormalized)
				{
					if (asInt)
					{
						switch (num)
						{
						default: *packed++ = uint8_t(*_input++ * 127.0f + 128.0f);
						case 3:  *packed++ = uint8_t(*_input++ * 127.0f + 128.0f);
						case 2:  *packed++ = uint8_t(*_input++ * 127.0f + 128.0f);
						case 1:  *packed++ = uint8_t(*_input++ * 127.0f + 128.0f);
						}
					}
					else
					{
						switch (num)
						{
						default: *packed++ = uint8_t(*_input++ * 255.0f);
						case 3:  *packed++ = uint8_t(*_input++ * 255.0f);
						case 2:  *packed++ = uint8_t(*_input++ * 255.0f);
						case 1:  *packed++ = uint8_t(*_input++ * 255.0f);
						}
					}
				}
				else
				{
					switch (num)
					{
					default: *packed++ = uint8_t(*_input++);
					case 3:  *packed++ = uint8_t(*_input++);
					case 2:  *packed++ = uint8_t(*_input++);
					case 1:  *packed++ = uint8_t(*_input++);
					}
				}
			}
			break;

		case AttribType::Int16:
			{
				int16_t* packed = (int16_t*)data;
				if (_inputNormalized)
				{
					if (asInt)
					{
						switch (num)
						{
						default: *packed++ = int16_t(*_input++ * 32767.0f);
						case 3:  *packed++ = int16_t(*_input++ * 32767.0f);
						case 2:  *packed++ = int16_t(*_input++ * 32767.0f);
						case 1:  *packed++ = int16_t(*_input++ * 32767.0f);
						}
					}
					else
					{
						switch (num)
						{
						default: *packed++ = int16_t(*_input++ * 65535.0f - 32768.0f);
						case 3:  *packed++ = int16_t(*_input++ * 65535.0f - 32768.0f);
						case 2:  *packed++ = int16_t(*_input++ * 65535.0f - 32768.0f);
						case 1:  *packed++ = int16_t(*_input++ * 65535.0f - 32768.0f);
						}
					}
				}
				else
				{
					switch (num)
					{
					default: *packed++ = int16_t(*_input++);
					case 3:  *packed++ = int16_t(*_input++);
					case 2:  *packed++ = int16_t(*_input++);
					case 1:  *packed++ = int16_t(*_input++);
					}
				}
			}
			break;

		case AttribType::Half:
			{
				uint16_t* packed = (uint16_t*)data;
				switch (num)
				{
				default: *packed++ = bx::halfFromFloat(*_input++);
				case 3:  *packed++ = bx::halfFromFloat(*_input++);
				case 2:  *packed++ = bx::halfFromFloat(*_input++);
				case 1:  *packed++ = bx::halfFromFloat(*_input++);
				}
			}
			break;

		case AttribType::Float:
			memcpy(data, _input, num*sizeof(float) );
			break;
		}
	}

	void vertexUnpack(float _output[4], Attrib::Enum _attr, const VertexDecl& _decl, const void* _data, uint32_t _index)
	{
		if (!_decl.has(_attr) )
		{
			memset(_output, 0, 4*sizeof(float) );
			return;
		}

		uint32_t stride = _decl.getStride();
		uint8_t* data = (uint8_t*)_data + _index*stride + _decl.getOffset(_attr);

		uint8_t num;
		AttribType::Enum type;
		bool normalized;
		bool asInt;
		_decl.decode(_attr, num, type, normalized, asInt);

		switch (type)
		{
		default:
		case AttribType::Uint8:
			{
				uint8_t* packed = (uint8_t*)data;
				if (asInt)
				{
					switch (num)
					{
					default: *_output++ = (float(*packed++) - 128.0f)*1.0f/127.0f;
					case 3:  *_output++ = (float(*packed++) - 128.0f)*1.0f/127.0f;
					case 2:  *_output++ = (float(*packed++) - 128.0f)*1.0f/127.0f;
					case 1:  *_output++ = (float(*packed++) - 128.0f)*1.0f/127.0f;
					}
				}
				else
				{
					switch (num)
					{
					default: *_output++ = float(*packed++)*1.0f/255.0f;
					case 3:  *_output++ = float(*packed++)*1.0f/255.0f;
					case 2:  *_output++ = float(*packed++)*1.0f/255.0f;
					case 1:  *_output++ = float(*packed++)*1.0f/255.0f;
					}
				}
			}
			break;

		case AttribType::Int16:
			{
				int16_t* packed = (int16_t*)data;
				if (asInt)
				{
					switch (num)
					{
					default: *_output++ = float(*packed++)*1.0f/32767.0f;
					case 3:  *_output++ = float(*packed++)*1.0f/32767.0f;
					case 2:  *_output++ = float(*packed++)*1.0f/32767.0f;
					case 1:  *_output++ = float(*packed++)*1.0f/32767.0f;
					}
				}
				else
				{
					switch (num)
					{
					default: *_output++ = (float(*packed++) + 32768.0f)*1.0f/65535.0f;
					case 3:  *_output++ = (float(*packed++) + 32768.0f)*1.0f/65535.0f;
					case 2:  *_output++ = (float(*packed++) + 32768.0f)*1.0f/65535.0f;
					case 1:  *_output++ = (float(*packed++) + 32768.0f)*1.0f/65535.0f;
					}
				}
			}
			break;

		case AttribType::Half:
			{
				uint16_t* packed = (uint16_t*)data;
				switch (num)
				{
				default: *_output++ = bx::halfToFloat(*packed++);
				case 3:  *_output++ = bx::halfToFloat(*packed++);
				case 2:  *_output++ = bx::halfToFloat(*packed++);
				case 1:  *_output++ = bx::halfToFloat(*packed++);
				}
			}
			break;

		case AttribType::Float:
			memcpy(_output, data, num*sizeof(float) );
			_output += num;
			break;
		}

		switch (num)
		{
		case 1: *_output++ = 0.0f;
		case 2: *_output++ = 0.0f;
		case 3: *_output++ = 0.0f;
		default: break;
		}
	}

	void vertexConvert(const VertexDecl& _destDecl, void* _destData, const VertexDecl& _srcDecl, const void* _srcData, uint32_t _num)
	{
		if (_destDecl.m_hash == _srcDecl.m_hash)
		{
			memcpy(_destData, _srcData, _srcDecl.getSize(_num) );
			return;
		}

		struct ConvertOp
		{
			enum Enum
			{
				Set,
				Copy,
				Convert,
			};

			Attrib::Enum attr;
			Enum op;
			uint32_t src;
			uint32_t dest;
			uint32_t size;
		};

		ConvertOp convertOp[Attrib::Count];
		uint32_t numOps = 0;

		for (uint32_t ii = 0; ii < Attrib::Count; ++ii)
		{
			Attrib::Enum attr = (Attrib::Enum)ii;

			if (_destDecl.has(attr) )
			{
				ConvertOp& cop = convertOp[numOps];
				cop.attr = attr;
				cop.dest = _destDecl.getOffset(attr);

				uint8_t num;
				AttribType::Enum type;
				bool normalized;
				bool asInt;
				_destDecl.decode(attr, num, type, normalized, asInt);
				cop.size = (*s_attribTypeSize[0])[type][num-1];

				if (_srcDecl.has(attr) )
				{
					cop.src = _srcDecl.getOffset(attr);
					cop.op = _destDecl.m_attributes[attr] == _srcDecl.m_attributes[attr] ? ConvertOp::Copy : ConvertOp::Convert;
				}
				else
				{
					cop.op = ConvertOp::Set;
				}

				++numOps;
			}
		}

		if (0 < numOps)
		{
			const uint8_t* src = (const uint8_t*)_srcData;
			uint32_t srcStride = _srcDecl.getStride();

			uint8_t* dest = (uint8_t*)_destData;
			uint32_t destStride = _destDecl.getStride();

			float unpacked[4];

			for (uint32_t ii = 0; ii < _num; ++ii)
			{
				for (uint32_t jj = 0; jj < numOps; ++jj)
				{
					const ConvertOp& cop = convertOp[jj];

					switch (cop.op)
					{
					case ConvertOp::Set:
						memset(dest + cop.dest, 0, cop.size);
						break;

					case ConvertOp::Copy:
						memcpy(dest + cop.dest, src + cop.src, cop.size);
						break;

					case ConvertOp::Convert:
						vertexUnpack(unpacked, cop.attr, _srcDecl, src);
						vertexPack(unpacked, true, cop.attr, _destDecl, dest);
						break;
					}
				}

				src += srcStride;
				dest += destStride;
			}
		}
	}

	inline float sqLength(const float _a[3], const float _b[3])
	{
		const float xx = _a[0] - _b[0];
		const float yy = _a[1] - _b[1];
		const float zz = _a[2] - _b[2];
		return xx*xx + yy*yy + zz*zz;
	}

	uint16_t weldVerticesRef(uint16_t* _output, const VertexDecl& _decl, const void* _data, uint16_t _num, float _epsilon)
	{
		// Brute force slow vertex welding...
		const float epsilonSq = _epsilon*_epsilon;

		uint32_t numVertices = 0;
		memset(_output, 0xff, _num*sizeof(uint16_t) );

		for (uint32_t ii = 0; ii < _num; ++ii)
		{
			if (UINT16_MAX != _output[ii])
			{
				continue;
			}

			_output[ii] = (uint16_t)ii;
			++numVertices;

			float pos[4];
			vertexUnpack(pos, Attrib::Position, _decl, _data, ii);

			for (uint32_t jj = 0; jj < _num; ++jj)
			{
				if (UINT16_MAX != _output[jj])
				{
					continue;
				}

				float test[4];
				vertexUnpack(test, Attrib::Position, _decl, _data, jj);

				if (sqLength(test, pos) < epsilonSq)
				{
					_output[jj] = (uint16_t)ii;
				}
			}
		}

		return (uint16_t)numVertices;
	}

	uint16_t weldVertices(uint16_t* _output, const VertexDecl& _decl, const void* _data, uint16_t _num, float _epsilon)
	{
		const uint32_t hashSize = bx::uint32_nextpow2(_num);
		const uint32_t hashMask = hashSize-1;
		const float epsilonSq = _epsilon*_epsilon;

		uint32_t numVertices = 0;

		const uint32_t size = sizeof(uint16_t)*(hashSize + _num);
		uint16_t* hashTable = (uint16_t*)alloca(size);
		memset(hashTable, 0xff, size);

		uint16_t* next = hashTable + hashSize;

		for (uint32_t ii = 0; ii < _num; ++ii)
		{
			float pos[4];
			vertexUnpack(pos, Attrib::Position, _decl, _data, ii);
			uint32_t hashValue = bx::hashMurmur2A(pos, 3*sizeof(float) ) & hashMask;

			uint16_t offset = hashTable[hashValue];
			for (; UINT16_MAX != offset; offset = next[offset])
			{
				float test[4];
				vertexUnpack(test, Attrib::Position, _decl, _data, _output[offset]);

				if (sqLength(test, pos) < epsilonSq)
				{
					_output[ii] = _output[offset];
					break;
				}
			}

			if (UINT16_MAX == offset)
			{
				_output[ii] = (uint16_t)ii;
				next[ii] = hashTable[hashValue];
				hashTable[hashValue] = (uint16_t)ii;
				numVertices++;
			}
		}

		return (uint16_t)numVertices;
	}
} // namespace bgfx
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
