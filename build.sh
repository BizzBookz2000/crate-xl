#!/bin/bash
# Linux build (development only). Needs: build-essential cmake git libasound2-dev
# libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev libfreetype-dev libgl1-mesa-dev
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build --parallel
echo "Built: build/Crate_artefacts/Release/Crate"
