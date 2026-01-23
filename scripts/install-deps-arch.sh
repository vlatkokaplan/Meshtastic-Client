#!/bin/bash
# Install dependencies for Meshtastic Client on Arch Linux

set -e

echo "Installing Qt6 and build dependencies..."

sudo pacman -S --needed \
    base-devel \
    cmake \
    ninja \
    qt6-base \
    qt6-declarative \
    qt6-positioning \
    qt6-serialport \
    qt6-location \
    protobuf

echo ""
echo "Dependencies installed successfully!"
echo ""
echo "To build the project, run:"
echo "  cd meshtastic-client"
echo "  mkdir build && cd build"
echo "  cmake .."
echo "  make -j\$(nproc)"
