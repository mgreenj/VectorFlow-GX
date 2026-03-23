#!/bin/bash

PROJECT_DIR="/home/rdgpu/VectorFlow-GX"

rm -rf $PROJECT_DIR/build

cd $PROJECT_DIR

meson setup build

ninja -C $PROJECT_DIR/build -j$(nproc)

if [[ -f builds/vectorflow-gx ]]; then
    echo "Success!"
elif 
    echo "Failed"
fi

