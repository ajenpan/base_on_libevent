#!/bin/sh

BUILD_DIR="build"

if [ -d $BUILD_DIR ]; then
    rm -rf $BUILD_DIR
fi

mkdir -p $BUILD_DIR && cd $BUILD_DIR
echo "build-dir:$(pwd)"

if [ $VCPKG_ROOT ]; then
    cmake .. -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake && cmake --build . -j $(nproc)
else
    cmake .. && cmake --build . -j $(nproc)
fi
