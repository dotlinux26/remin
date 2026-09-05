# Remin - Reproducible Build Environment
# Multi-stage build for reproducible builds

# =============================================================================
# BASE BUILD STAGE
# =============================================================================
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    ca-certificates \
    # GTK4 / libadwaita / GTKSourceView dependencies
    libgtkmm-4.0-dev \
    libvte-2.91-gtk4-dev \
    libadwaita-1-dev \
    libgtksourceview-5-dev \
    libsqlite3-dev \
    # Optional: md4c for markdown preview
    libmd4c-dev \
    # Cleanup
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Set up build directory
WORKDIR /build

# Copy source code
COPY . .

# Configure and build
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DREMIN_BUILD_GUI=ON -DREMIN_BUILD_CLI=ON -DREMIN_BUILD_TESTS=ON \
    && cmake --build build --parallel $(nproc) \
    && cmake --build build --target test

# =============================================================================
# RUNTIME STAGE (minimal)
# =============================================================================
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Install runtime dependencies only
RUN apt-get update && apt-get install -y --no-install-recommends \
    libgtkmm-4.0-1v5 \
    libvte-2.91-gtk4-0 \
    libadwaita-1-0 \
    libgtksourceview-5-0 \
    libsqlite3-0 \
    libmd4c0 \
    libglib2.0-0 \
    libgtk-4-1 \
    libpango-1.0-0 \
    libcairo2 \
    libgdk-pixbuf-2.0-0 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Copy built binaries
COPY --from=builder /build/build/src/app/remin /usr/local/bin/remin
COPY --from=builder /build/build/src/cli/remin-cli /usr/local/bin/remin-cli

# Create non-root user
RUN useradd -m -s /bin/bash remin
USER remin
WORKDIR /home/remin

ENTRYPOINT ["remin"]
CMD ["gui"]