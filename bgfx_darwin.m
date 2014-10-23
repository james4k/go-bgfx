/*
 * Copyright 2011-2014 Branimir Karadzic. All rights reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#include <Cocoa/Cocoa.h>

struct GlCtx_s
{
	void* view;
	void* context;
} typedef GlCtx;

void bgfx_GlContext_create(GlCtx* ctx, void* nsctx, uint32_t _width, uint32_t _height)
{
	//BX_UNUSED(_width, _height);

	/*
	NSWindow* nsWindow = (NSWindow*)nswnd;

	NSOpenGLPixelFormatAttribute profile =
#if BGFX_CONFIG_RENDERER_OPENGL >= 31
		NSOpenGLProfileVersion3_2Core
#else
		NSOpenGLProfileVersionLegacy
#endif // BGFX_CONFIG_RENDERER_OPENGL >= 31
		;

	NSOpenGLPixelFormatAttribute pixelFormatAttributes[] = {
		NSOpenGLPFAOpenGLProfile, profile,
		NSOpenGLPFAColorSize,     24,
		NSOpenGLPFAAlphaSize,     8,
		NSOpenGLPFADepthSize,     24,
		NSOpenGLPFAStencilSize,   8,
		NSOpenGLPFADoubleBuffer,  true,
		NSOpenGLPFAAccelerated,   true,
		NSOpenGLPFANoRecovery,    true,
		0,                        0,
	};

	NSOpenGLPixelFormat* pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:pixelFormatAttributes];
	//BGFX_FATAL(NULL != pixelFormat, Fatal::UnableToInitialize, "Failed to initialize pixel format.");

	NSRect glViewRect = [[nsWindow contentView] bounds];
	NSOpenGLView* glView = [[NSOpenGLView alloc] initWithFrame:glViewRect pixelFormat:pixelFormat];
	
	[pixelFormat release];
	[nsWindow setContentView:glView];
	*/
	
	//NSOpenGLContext* glContext = [glView openGLContext];
	//BGFX_FATAL(NULL != glContext, Fatal::UnableToInitialize, "Failed to initialize GL context.");
	NSOpenGLContext* glContext = (NSOpenGLContext*)nsctx;

	[glContext makeCurrentContext];
	GLint interval = 0;
	[glContext setValues:&interval forParameter:NSOpenGLCPSwapInterval];
	
	//ctx->view    = glView;
	ctx->context = glContext;
}

void bgfx_GlContext_destroy(GlCtx* ctx)
{
	//NSOpenGLView* glView = (NSOpenGLView*)ctx->view;
	//ctx->view = 0;
	ctx->context = 0;
	//[glView release];
}

void bgfx_GlContext_resize(GlCtx* ctx, uint32_t _width, uint32_t _height, bool _vsync)
{
	GLint interval = _vsync ? 1 : 0;
	NSOpenGLContext* glContext = (NSOpenGLContext*)ctx->context;
	[glContext setValues:&interval forParameter:NSOpenGLCPSwapInterval];
}

void bgfx_GlContext_swap(GlCtx *ctx)
{
	NSOpenGLContext* glContext = (NSOpenGLContext*)ctx->context;
	[glContext makeCurrentContext];
	[glContext flushBuffer];
}

