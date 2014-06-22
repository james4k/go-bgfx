package main

import (
	"log"
	"math"
	"os"
	"path/filepath"
	"runtime"

	glfw "github.com/go-gl/glfw3"
	"github.com/go-gl/mathgl/mgl32"
	"github.com/james4k/go-bgfx"
	"github.com/james4k/go-bgfx/window/bgfx_glfw"
)

type PosNormalColorVertex struct {
	Position [3]float32
	Normal   [3]float32
	ABGR     uint32
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

	bgfx.Reset(width, height, bgfx.ResetVSync)
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
	vd.Add(bgfx.AttribNormal, 3, bgfx.AttribTypeFloat, false, false)
	vd.Add(bgfx.AttribColor0, 4, bgfx.AttribTypeUint8, true, false)
	vd.End()
	vsh := bgfx.CreateShader(vs_metaballs_glsl)
	fsh := bgfx.CreateShader(fs_metaballs_glsl)
	prog := bgfx.CreateProgram(vsh, fsh, true)
	if err != nil {
		log.Fatalln(err)
	}
	defer bgfx.DestroyProgram(prog)

	const dim = 32
	const ypitch = dim
	const zpitch = dim * dim
	const invdim = 1.0 / (dim - 1)
	var grid [dim * dim * dim]cell

	var last float32
	nowOffset := glfw.GetTime()
	for !window.ShouldClose() {
		now := float32(glfw.GetTime() - nowOffset)
		dt := now - last
		last = now
		width, height = window.GetSize()
		var (
			eye = mgl32.Vec3{0, 0, -50.0}
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
		bgfx.Submit(0)

		// 32k vertices
		const maxVertices = (32 << 10)
		var vertices []PosNormalColorVertex
		tvb := bgfx.AllocTransientVertexBuffer(&vertices, maxVertices, vd)

		const numSpheres = 16
		var spheres [numSpheres][4]float32
		for i := 0; i < numSpheres; i++ {
			x := float64(i)
			t := float64(now)
			spheres[i][0] = float32(math.Sin(t*(x*0.21)+x*0.37) * (dim*0.5 - 8.0))
			spheres[i][1] = float32(math.Sin(t*(x*0.37)+x*0.67) * (dim*0.5 - 8.0))
			spheres[i][2] = float32(math.Cos(t*(x*0.11)+x*0.13) * (dim*0.5 - 8.0))
			spheres[i][3] = 1.0 / (2.0 + float32(math.Sin(t*(x*0.13))*0.5+0.5)*2.0)
		}

		profUpdate := glfw.GetTime()
		for z := 0; z < dim; z++ {
			fz := float32(z)
			for y := 0; y < dim; y++ {
				fy := float32(y)
				offset := (z*dim + y) * dim
				for x := 0; x < dim; x++ {
					var (
						fx      = float32(x)
						dist    float32
						prod    float32 = 1.0
						xoffset         = offset + x
					)
					for i := 0; i < numSpheres; i++ {
						pos := &spheres[i]
						dx := pos[0] - (-dim*0.5 + fx)
						dy := pos[1] - (-dim*0.5 + fy)
						dz := pos[2] - (-dim*0.5 + fz)
						invr := pos[3]
						dot := dx*dx + dy*dy + dz*dz
						dot *= invr * invr
						dist *= dot
						dist += prod
						prod *= dot
					}
					grid[xoffset].val = dist/prod - 1.0
				}
			}
		}
		profUpdate = glfw.GetTime() - profUpdate

		profNormal := glfw.GetTime()
		for z := 1; z < dim-1; z++ {
			for y := 1; y < dim-1; y++ {
				offset := (z*dim + y) * dim
				for x := 1; x < dim-1; x++ {
					xoffset := offset + x
					grid[xoffset].normal = mgl32.Vec3{
						grid[xoffset-1].val - grid[xoffset+1].val,
						grid[xoffset-ypitch].val - grid[xoffset+ypitch].val,
						grid[xoffset-zpitch].val - grid[xoffset+zpitch].val,
					}.Normalize()
				}
			}
		}
		profNormal = glfw.GetTime() - profNormal

		profTriangulate := glfw.GetTime()
		numVertices := 0
		for z := 0; z < dim-1 && numVertices+12 < maxVertices; z++ {
			var (
				rgb [6]float32
				pos [3]float32
				val [8]*cell
			)
			rgb[2] = float32(z) * invdim
			rgb[5] = float32(z+1) * invdim
			for y := 0; y < dim-1 && numVertices+12 < maxVertices; y++ {
				offset := (z*dim + y) * dim
				rgb[1] = float32(y) * invdim
				rgb[4] = float32(y+1) * invdim
				for x := 0; x < dim-1 && numVertices+12 < maxVertices; x++ {
					xoffset := offset + x
					rgb[0] = float32(x) * invdim
					rgb[3] = float32(x+1) * invdim
					pos[0] = -dim*0.5 + float32(x)
					pos[1] = -dim*0.5 + float32(y)
					pos[2] = -dim*0.5 + float32(z)
					val[0] = &grid[xoffset+zpitch+ypitch]
					val[1] = &grid[xoffset+zpitch+ypitch+1]
					val[2] = &grid[xoffset+ypitch+1]
					val[3] = &grid[xoffset+ypitch]
					val[4] = &grid[xoffset+zpitch]
					val[5] = &grid[xoffset+zpitch+1]
					val[6] = &grid[xoffset+1]
					val[7] = &grid[xoffset]
					num := triangulate(vertices[numVertices:], rgb[:], pos[:], val[:], 0.5)
					numVertices += num
				}
			}
		}
		profTriangulate = glfw.GetTime() - profTriangulate

		bgfx.DebugTextClear()
		bgfx.DebugTextPrintf(0, 1, 0x4f, title)
		bgfx.DebugTextPrintf(0, 2, 0x6f, "Description: Rendering with transient buffers and embedded shaders.")
		bgfx.DebugTextPrintf(0, 4, 0x0f, "    Vertices: %d (%.2f%%)", numVertices, float32(numVertices*100)/maxVertices)
		bgfx.DebugTextPrintf(0, 5, 0x0f, "      Update: % 7.3f[ms]", profUpdate*1000.0)
		bgfx.DebugTextPrintf(0, 6, 0x0f, "Calc normals: % 7.3f[ms]", profNormal*1000.0)
		bgfx.DebugTextPrintf(0, 7, 0x0f, " Triangulate: % 7.3f[ms]", profTriangulate*1000.0)
		bgfx.DebugTextPrintf(0, 8, 0x0f, "       Frame: % 7.3f[ms]", dt*1000.0)

		bgfx.DebugTextPrintf(0, 10, 0x1f, "BUG: looks like an error with the normals/colors")

		mtx := mgl32.HomogRotate3DX(now * 0.67)
		mtx = mtx.Mul4(mgl32.HomogRotate3DY(now))
		bgfx.SetTransform([16]float32(mtx))
		bgfx.SetProgram(prog)
		bgfx.SetTransientVertexBuffer(tvb, 0, numVertices)
		bgfx.Submit(0)

		bgfx.Frame()
		glfw.PollEvents()
	}
}
