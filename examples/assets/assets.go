package assets

import (
	"os"
	"path/filepath"
)

var dirs = filepath.SplitList(os.Getenv("GOPATH"))

func init() {
	const assets = "src/github.com/james4k/go-bgfx/examples/assets"
	for i := range dirs {
		dirs[i] = filepath.Join(dirs[i], assets)
	}
}

func Open(name string) (f *os.File, err error) {
	for _, dir := range dirs {
		f, err = os.Open(filepath.Join(dir, name))
		if err != nil {
			continue
		}
		return
	}
	return
}
