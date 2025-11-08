#!/bin/bash
# Build script for ohmy-miner-etc

set -e

# Configuration
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${BUILD_DIR:-build}"
CUDA_ARCH="${CUDA_ARCH:-60;61;70;75;80;86;89}"
JOBS="${JOBS:-$(nproc)}"

echo "==================================="
echo "Building OhMy Miner ETC"
echo "==================================="
echo "Build Type: $BUILD_TYPE"
echo "Build Directory: $BUILD_DIR"
echo "CUDA Architectures: $CUDA_ARCH"
echo "Parallel Jobs: $JOBS"
echo "==================================="

# Check CUDA
if ! command -v nvcc &> /dev/null; then
    echo "Error: CUDA compiler (nvcc) not found!"
    echo "Please install CUDA Toolkit 11.0 or later"
    exit 1
fi

echo "CUDA Version:"
nvcc --version

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo ""
echo "Configuring with CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_CUDA_ARCHITECTURES="$CUDA_ARCH" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build
echo ""
echo "Building..."
make -j"$JOBS"

# Run tests
echo ""
echo "Running tests..."
ctest --output-on-failure

echo ""
echo "==================================="
echo "Build completed successfully!"
echo "Executable: $BUILD_DIR/ohmy-miner-etc"
echo "==================================="
