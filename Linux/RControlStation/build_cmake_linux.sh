#!/bin/bash

# CMake build script for Linux
# Usage: ./build_cmake_linux.sh [debug|release] [clean]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build/cmake_linux"
BUILD_TYPE="Release"
CLEAN=false
PARALLEL_JOBS=$(nproc)
SKIP_DEPS=false

# Parse arguments
for arg in "$@"; do
    case "$arg" in
        debug|Debug)
            BUILD_TYPE="Debug"
            ;;
        release|Release)
            BUILD_TYPE="Release"
            ;;
        clean)
            CLEAN=true
            ;;
        skip-deps|--skip-deps)
            SKIP_DEPS=true
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [debug|release] [clean] [skip-deps]"
            exit 1
            ;;
    esac
done

echo "Building RControlStation for Linux"
echo "  Build Type: $BUILD_TYPE"
echo "  Build Directory: $BUILD_DIR"

# Check dependencies
check_dependencies() {
    local missing=()
    command -v cmake >/dev/null 2>&1 || missing+=("CMake")
    command -v qmake6 >/dev/null 2>&1 || missing+=("Qt6 development tools (qmake6)")
    # Check for Qt6 libraries via ldconfig (system-wide)
    ldconfig -p | grep -q libQt6Core.so || missing+=("Qt6 development libraries")
    pkg-config --exists sdl2 >/dev/null 2>&1 || missing+=("SDL2 (libsdl2-dev)")
    pkg-config --exists gdal >/dev/null 2>&1 || ldconfig -p | grep -q libgdal.so || missing+=("GDAL (libgdal-dev)")

    if [ ${#missing[@]} -gt 0 ]; then
        echo "ERROR: Missing dependencies:"
        for dep in "${missing[@]}"; do
            echo "  - $dep"
        done
        echo ""
        
        # Map missing dependencies to apt packages
        declare -A apt_packages=(
            ["CMake"]="cmake"
            ["Qt6 development tools (qmake6)"]="qt6-base-dev-tools"
            ["Qt6 development libraries"]="qt6-base-dev"
            ["SDL2 (libsdl2-dev)"]="libsdl2-dev"
            ["GDAL (libgdal-dev)"]="libgdal-dev"
        )
        
        # Build apt install command
        local apt_install_cmd="sudo apt update && sudo apt install -y"
        for dep in "${missing[@]}"; do
            if [ -n "${apt_packages[$dep]}" ]; then
                apt_install_cmd+=" ${apt_packages[$dep]}"
            fi
        done
        
        # Ask user if they want to install automatically
        read -p "Attempt to install missing dependencies automatically? [Y/n] " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]] || [[ -z $REPLY ]]; then
            echo "Installing missing dependencies..."
            eval $apt_install_cmd
            if [ $? -ne 0 ]; then
                echo "Failed to install dependencies. Please install them manually."
                exit 1
            fi
            # Re-check dependencies after installation
            check_dependencies
        else
            echo "Please install the missing dependencies manually and rerun this script."
            exit 1
        fi
    fi
}

# Check dependencies unless skipped
if [ "$SKIP_DEPS" = false ]; then
    check_dependencies
fi

# Clean if requested
if [ "$CLEAN" = true ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    exit 0
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Configure with CMake
cd "$BUILD_DIR"
echo "Configuring with CMake..."

# Use ccache if available for faster recompilation
if command -v ccache >/dev/null 2>&1; then
    echo "Using ccache for faster builds"
    export CC="ccache gcc"
    export CXX="ccache g++"
fi

cmake \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/cmake/Toolchains/Linux-GCC.cmake" \
    -DHAS_OPENGL=OFF \
    -DHAS_ASSIMP=OFF \
    -DHAS_SIM_SCEN=OFF \
    "$SCRIPT_DIR"

# Build
echo "Building..."
cmake --build . --config "$BUILD_TYPE" --parallel $PARALLEL_JOBS

# Build summary
echo ""
echo "=== Build Summary ==="
echo "  Build Type: $BUILD_TYPE"
echo "  Executable: ${BUILD_DIR}/bin/RControlStation"
echo "  Build Log:  ${BUILD_DIR}/CMakeFiles/CMakeOutput.log"
