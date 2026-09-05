#!/bin/bash
# Install all system dependencies for building Remin
# Run with: sudo ./scripts/install_deps.sh

set -euo pipefail

# Detect distribution
if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO=$ID
    VERSION=$VERSION_ID
else
    echo "Cannot detect distribution"
    exit 1
fi

echo "Detected: $PRETTY_NAME"

install_ubuntu() {
    local version=$1
    echo "Installing dependencies for Ubuntu $version..."

    apt-get update

    # Build tools
    apt-get install -y \
        build-essential \
        cmake \
        ninja-build \
    pkg-config \
        git \
        ccache

    # GTK4 / libadwaita / GTKSourceView / VTE
    apt-get install -y \
        libgtkmm-4.0-dev \
        libvte-2.91-gtk4-dev \
        libadwaita-1-dev \
        libgtksourceview-5-dev \
        libsqlite3-dev \
        libmd4c-dev

    # Optional: for running GUI apps in headless CI
    apt-get install -y xvfb
}

install_debian() {
    local version=$1
    echo "Installing dependencies for Debian $version..."

    apt-get update

    apt-get install -y \
        build-essential \
        cmake \
        ninja-build \
        pkg-config \
        git \
        ccache \
        libgtkmm-4.0-dev \
        libvte-2.91-gtk4-dev \
        libadwaita-1-dev \
        libgtksourceview-5-dev \
        libsqlite3-dev \
        libmd4c-dev
}

install_fedora() {
    local version=$1
    echo "Installing dependencies for Fedora $version..."

    dnf install -y \
        gcc-c++ \
        cmake \
        ninja-build \
        pkgconfig \
        git \
        ccache \
        gtkmm40-devel \
        vte291-gtk4-devel \
        libadwaita-devel \
        gtksourceview5-devel \
        sqlite-devel \
        md4c-devel
}

install_arch() {
    echo "Installing dependencies for Arch Linux..."

    pacman -Sy --needed --noconfirm \
        base-devel \
        cmake \
        ninja \
        pkgconf \
        git \
        ccache \
        gtkmm4 \
        vte291-gtk4 \
        libadwaita \
        gtksourceview5 \
        sqlite \
        md4c
}

case $DISTRO in
    ubuntu)
        install_ubuntu "$VERSION"
        ;;
    debian)
        install_debian "$VERSION"
        ;;
    fedora)
        install_fedora "$VERSION"
        ;;
    arch|manjaro|endeavouros)
        install_arch
        ;;
    *)
        echo "Unsupported distribution: $DISTRO"
        echo "Please install dependencies manually:"
        echo "  - C++ compiler (GCC 11+ or Clang 13+)"
        echo "  - CMake 3.20+, Ninja, pkg-config"
        echo "  - GTK4/gtkmm-4.0, VTE-2.91-GTK4, libadwaita-1, GTKSourceView-5"
        echo "  - SQLite3, md4c"
        exit 1
        ;;
esac

echo "Dependencies installed successfully!"
echo "You can now build with: cmake -B build && cmake --build build"