#!/bin/bash
# Install dependencies for Meshtastic Client on Fedora

set -e

echo "Installing Qt6 and build dependencies..."

sudo dnf install -y \
    gcc-c++ \
    cmake \
    ninja-build \
    qt6-qtbase-devel \
    qt6-qtdeclarative-devel \
    qt6-qtpositioning-devel \
    qt6-qtserialport-devel \
    qt6-qtlocation-devel \
    protobuf-devel \
    protobuf-compiler

echo ""
echo "Dependencies installed successfully!"
echo ""
echo "To build the project, run:"
echo "  cd meshtastic-client"
echo "  mkdir build && cd build"
echo "  cmake .."
echo "  make -j\$(nproc)"
