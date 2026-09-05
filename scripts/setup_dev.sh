#!/bin/bash
# Development setup script for Remin
# Usage: ./scripts/setup_dev.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "=== Remin Development Setup ==="
echo "Project root: $PROJECT_ROOT"

# Check for required tools
check_tool() {
    if ! command -v "$1" &> /dev/null; then
        echo "ERROR: $1 not found. Please install it first."
        return 1
    fi
    echo "✓ $1 found"
}

echo "Checking required tools..."
check_tool cmake || exit 1
check_tool ninja || exit 1
check_tool pkg-config || exit 1
check_tool git || exit 1

# Check for C++ compiler
if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
    echo "ERROR: No C++ compiler found (g++ or clang++)"
    exit 1
fi
echo "✓ C++ compiler found"

# Check pkg-config dependencies
check_pkg() {
    if pkg-config --exists "$1"; then
        echo "✓ $1 found"
    else
        echo "WARNING: $1 not found via pkg-config"
        return 1
    fi
}

echo "Checking pkg-config dependencies..."
check_pkg gtkmm-4.0 || echo "  → Install libgtkmm-4.0-dev"
check_pkg vte-2.91-gtk4 || echo "  → Install libvte-2.91-gtk4-dev"
check_pkg libadwaita-1 || echo "  → Install libadwaita-1-dev"
check_pkg gtksourceview-5 || echo "  → Install libgtksourceview-5-dev"
check_pkg sqlite3 || echo "  → Install libsqlite3-dev"
check_pkg md4c || echo "  → Install libmd4c-dev (optional)"

# Configure build
BUILD_DIR="${PROJECT_ROOT}/build"
echo "Configuring build in $BUILD_DIR..."

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DREMIN_BUILD_GUI=ON \
    -DREMIN_BUILD_CLI=ON \
    -DREMIN_BUILD_TESTS=ON \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -G Ninja

echo "Build configured successfully!"
echo ""
echo "To build:    cmake --build build"
echo "To test:     cmake --build build --target test"
echo "To run GUI:  ./build/src/app/remin gui"
echo "To run CLI:  ./build/src/cli/remin-cli --help"