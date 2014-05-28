#!/usr/bin/env bash
set -e

# get sources
git submodule update --init

# TODO checkout to specific revisions

# remove old generated files
rm -rf include/*
rm -f z*.cpp z*.m z*.h

# copy includes
cp -r lib/bgfx/include/* include/
cp -r lib/bgfx/3rdparty/khronos/* include/
cp -r lib/bx/include/* include/

function prep {
	sed 's/#[ 	]*include "/#include "z/g' $1 > $2
	sed -i '' 's/#\s*include "zbgfx.h"/#include <bgfx.h>/g' $2
	sed -i '' 's/#\s*include "zbgfxplatform.h"/#include <bgfxplatform.h>/g' $2
}

# copy source files with z prefix for easy management, and fix includes
# to use z prefix
cd lib/bgfx/src
for file in `ls *.cpp *.h`; do
	prep $file ../../../z$file
done
cd -

# copy some additional source files that make us buildable by the go
# tool
cd src
for file in `ls *`; do
	prep $file ../z$file
done
