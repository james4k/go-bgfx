package main

import (
	"log"
	"os"
	"path/filepath"
	"runtime"

	glfw "github.com/go-gl/glfw3"
	"github.com/james4k/go-bgfx"
	"github.com/james4k/go-bgfx/window/bgfx_glfw"
)

func main() {
	runtime.LockOSThread()
	var (
		width  = 512
		height = 512
		title  = filepath.Base(os.Args[0])
	)
	glfw.SetErrorCallback(func(err glfw.ErrorCode, desc string) {
		log.Printf("glfw: %s\n", desc)
	})
	if !glfw.Init() {
		os.Exit(1)
	}
	defer glfw.Terminate()
	// for now, fized size window. bgfx currently breaks glfw events
	// because it overrides the NSWindow's content view
	glfw.WindowHint(glfw.Resizable, 0)
	window, err := glfw.CreateWindow(width, height, title, nil, nil)
	if err != nil {
		log.Fatalln(err)
	}
	bgfx_glfw.SetWindow(window)
	bgfx.Init()
	defer bgfx.Shutdown()
	bgfx.Reset(width, height, bgfx.ResetVSync)
	bgfx.SetDebug(bgfx.DebugText)
	bgfx.SetViewClear(
		0,
		bgfx.ClearColor|bgfx.ClearDepth,
		0x303030ff,
		1.0,
		0,
	)
	for !window.ShouldClose() {
		width, height = window.GetSize()
		bgfx.SetViewRect(0, 0, 0, width, height)
		bgfx.Submit(0)
		bgfx.DebugTextClear()
		bgfx.DebugTextPrintf(0, 1, 0x4f, title)
		bgfx.DebugTextPrintf(0, 2, 0x6f, "Description: Initialization and debug text.")
		bgfx.Frame()
		glfw.PollEvents()
	}
}
