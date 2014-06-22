package main

import (
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"runtime"

	glfw "github.com/go-gl/glfw3"
	"github.com/go-gl/mathgl/mgl32"
	"github.com/james4k/go-bgfx"
	"github.com/james4k/go-bgfx/examples/assets"
	"github.com/james4k/go-bgfx/window/bgfx_glfw"
)

type PosColorVertex struct {
	X, Y, Z float32
	ABGR    uint32
}

var vertices = []PosColorVertex{
	{-1.0, 1.0, 1.0, 0xff000000},
	{1.0, 1.0, 1.0, 0xff0000ff},
	{-1.0, -1.0, 1.0, 0xff00ff00},
	{1.0, -1.0, 1.0, 0xff00ffff},
	{-1.0, 1.0, -1.0, 0xffff0000},
	{1.0, 1.0, -1.0, 0xffff00ff},
	{-1.0, -1.0, -1.0, 0xffffff00},
	{1.0, -1.0, -1.0, 0xffffffff},
}

var indices = []uint16{
	0, 1, 2, // 0
	1, 3, 2,
	4, 6, 5, // 2
	5, 6, 7,
	0, 2, 4, // 4
	4, 2, 6,
	1, 5, 3, // 6
	5, 7, 3,
	0, 4, 1, // 8
	4, 5, 1,
	2, 3, 6, // 10
	6, 3, 7,
}

func main() {
	runtime.LockOSThread()
	var (
		width  = 1280
		height = 720
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

	bgfx.Reset(width, height, 0)
	bgfx.SetDebug(bgfx.DebugText)
	bgfx.SetViewClear(
		0,
		bgfx.ClearColor|bgfx.ClearDepth,
		0x303030ff,
		1.0,
		0,
	)

	var vd bgfx.VertexDecl
	vd.Begin()
	vd.Add(bgfx.AttribPosition, 3, bgfx.AttribTypeFloat, false, false)
	vd.Add(bgfx.AttribColor0, 4, bgfx.AttribTypeUint8, true, false)
	vd.End()
	vb := bgfx.CreateVertexBuffer(vertices, vd)
	defer bgfx.DestroyVertexBuffer(vb)
	ib := bgfx.CreateIndexBuffer(indices)
	defer bgfx.DestroyIndexBuffer(ib)
	prog, err := loadProgram("vs_cubes", "fs_cubes")
	if err != nil {
		log.Fatalln(err)
	}
	defer bgfx.DestroyProgram(prog)

	var (
		last, avgdt, totaldt float32
		nframes              int
		dim                  = 12
	)
	for !window.ShouldClose() {
		now := float32(glfw.GetTime())
		dt := now - last
		last = now

		if totaldt >= 1.0 {
			avgdt = totaldt / float32(nframes)
			if avgdt < 1.0/65 {
				dim += 2
			} else if avgdt > 1.0/57 && dim > 2 {
				dim -= 1
			}
			totaldt = 0
			nframes = 0
		}
		totaldt += dt
		nframes++

		width, height = window.GetSize()
		var (
			eye = mgl32.Vec3{0, 0, -35.0}
			at  = mgl32.Vec3{0, 0, 0}
			up  = mgl32.Vec3{0, 1, 0}
		)
		view := [16]float32(mgl32.LookAtV(eye, at, up))
		proj := [16]float32(mgl32.Perspective(
			mgl32.DegToRad(60.0),
			float32(width)/float32(height),
			0.1, 100.0,
		))
		bgfx.SetViewTransform(0, view, proj)
		bgfx.SetViewRect(0, 0, 0, width, height)
		bgfx.DebugTextClear()
		bgfx.DebugTextPrintf(0, 1, 0x4f, title)
		bgfx.DebugTextPrintf(0, 2, 0x6f, "Description: Draw stress, maximizing number of draw calls.")
		bgfx.DebugTextPrintf(0, 3, 0x0f, "Frame: % 7.3f[ms]", dt*1000.0)
		bgfx.DebugTextPrintf(0, 5, 0x0f, "Dim: %d", dim)
		bgfx.DebugTextPrintf(0, 6, 0x0f, "AvgFrame: % 7.3f[ms]", avgdt*1000.0)
		bgfx.Submit(0)

		const step = 0.6
		pos := [3]float32{
			-step * float32(dim) / 2.0,
			-step * float32(dim) / 2.0,
			-15,
		}
		for z := 0; z < dim; z++ {
			for y := 0; y < dim; y++ {
				for x := 0; x < dim; x++ {
					mtx := mgl32.HomogRotate3DX(now + float32(x)*0.21)
					mtx = mtx.Mul4(mgl32.HomogRotate3DY(now + float32(y)*0.37))
					mtx = mtx.Mul4(mgl32.HomogRotate3DY(now + float32(z)*0.13))
					mtx = mtx.Mul4(mgl32.Scale3D(0.25, 0.25, 0.25))
					mtx[12] = pos[0] + float32(x)*step
					mtx[13] = pos[1] + float32(y)*step
					mtx[14] = pos[2] + float32(z)*step

					bgfx.SetTransform([16]float32(mtx))
					bgfx.SetProgram(prog)
					bgfx.SetVertexBuffer(vb)
					bgfx.SetIndexBuffer(ib)
					bgfx.SetState(bgfx.StateDefault)
					bgfx.Submit(0)
				}
			}
		}

		bgfx.Frame()
		glfw.PollEvents()
	}
}

func loadProgram(vsh, fsh string) (bgfx.Program, error) {
	v, err := loadShader(vsh)
	if err != nil {
		return bgfx.Program{}, err
	}
	f, err := loadShader(fsh)
	if err != nil {
		return bgfx.Program{}, err
	}
	return bgfx.CreateProgram(v, f, true), nil
}

func loadShader(name string) (bgfx.Shader, error) {
	f, err := assets.Open(filepath.Join("shaders/glsl", name+".bin"))
	if err != nil {
		return bgfx.Shader{}, err
	}
	defer f.Close()
	data, err := ioutil.ReadAll(f)
	if err != nil {
		return bgfx.Shader{}, err
	}
	return bgfx.CreateShader(data), nil
}
