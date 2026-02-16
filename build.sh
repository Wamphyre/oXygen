#!/bin/bash

# Build script for oXygen on macOS
# Automates CMake configuration and Release build

echo "🛠️  Configuring oXygen for macOS..."

# Check if cmake is installed
if ! command -v cmake &> /dev/null; then
    echo "❌ CMake could not be found. Please install CMake."
    exit 1
fi

# Clean previous builds
echo "🧹 Cleaning previous builds..."
rm -rf build
rm -rf releases
mkdir -p releases

# Ensure build directory exists
mkdir -p build

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles"

if [ $? -ne 0 ]; then
    echo "❌ Configuration failed."
    exit 1
fi

echo "🚀 Building oXygen (Release)..."

# Build with maximum parallelism
cmake --build build --config Release -j$(sysctl -n hw.ncpu)

if [ $? -ne 0 ]; then
    echo "❌ Build failed."
    exit 1
fi

echo "✅ Build successful!"
echo "📂 Artifacts:"
ls -d releases/*.vst3 2>/dev/null || echo "  (Release VST3 not found)"
echo "Done."
