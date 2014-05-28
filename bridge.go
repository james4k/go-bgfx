package bgfx

// #cgo CPPFLAGS: -I include
// #cgo darwin CPPFLAGS: -I include/compat/osx
// #cgo darwin LDFLAGS: -framework Cocoa -framework OpenGL -lGLEW
// #cgo linux LDFLAGS: -lGLEW -lGL
// #cgo windows LDFLAGS: -lglew32 -lopengl32
// #include "bridge.h"
import "C"
