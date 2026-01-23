#!/bin/bash
# Install dependencies for Meshtastic Client on Ubuntu/Debian

set -e

echo "Installing Qt6 and build dependencies..."

sudo apt-get update

# Core build tools
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    protobuf-compiler \
    libprotobuf-dev

# Qt6 base packages
sudo apt-get install -y \
    qt6-base-dev \
    qt6-declarative-dev \
    qt6-serialport-dev \
    libqt6opengl6-dev \
    qml6-module-qtquick \
    qml6-module-qtquick-controls \
    qml6-module-qtquick-layouts \
    qml6-module-qtquick-window

# Qt6 Positioning - try multiple package names
sudo apt-get install -y qt6-positioning-dev 2>/dev/null || \
sudo apt-get install -y libqt6positioning6-dev 2>/dev/null || \
echo "Warning: Qt6 Positioning dev package not found"

sudo apt-get install -y qml6-module-qtpositioning 2>/dev/null || \
echo "Warning: Qt6 Positioning QML module not found"

# Qt6 Location - this module has limited availability
# Try various package names that different Ubuntu versions use
echo ""
echo "Attempting to install Qt6 Location module..."

if sudo apt-get install -y qt6-location-dev qml6-module-qtlocation 2>/dev/null; then
    echo "Qt6 Location installed from standard repos"
elif sudo apt-get install -y libqt6location6-dev 2>/dev/null; then
    echo "Qt6 Location dev installed (alternative package name)"
else
    echo ""
    echo "========================================"
    echo "Qt6 Location module not in repositories"
    echo "========================================"
    echo ""
    echo "Options:"
    echo "1. Ubuntu 24.04+: The packages should be available"
    echo "2. Ubuntu 22.04: Try the Qt6 PPA:"
    echo "   sudo add-apt-repository ppa:okirby/qt6-backports"
    echo "   sudo apt-get update"
    echo "   sudo apt-get install qt6-location-dev"
    echo ""
    echo "3. Use system Qt from qt.io installer"
    echo "4. Build without map (modify CMakeLists.txt)"
    echo ""
fi

echo ""
echo "========================================"
echo "Installation complete!"
echo "========================================"
echo ""
echo "To build the project:"
echo "  mkdir build && cd build"
echo "  cmake .."
echo "  make -j\$(nproc)"
echo ""
echo "If Qt6 Location is missing, you can still build"
echo "but the map feature won't work. See options above."
