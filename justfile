# Justfile - Common development tasks for Remin
# Install: cargo install just
# Usage: just <command>

set shell := ["bash", "-cu"]

# Variables
PROJECT_ROOT := `pwd`
BUILD_DIR := "build"
BIN_DIR := "build/src/app"
CLI_BIN := "build/src/cli"

# Default recipe
default: build

# =============================================================================
# BUILD COMMANDS
# =============================================================================

# Configure and build
build:
	cmake -B {{BUILD_DIR}} -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DREMIN_BUILD_GUI=ON -DREMIN_BUILD_CLI=ON -DREMIN_BUILD_TESTS=ON -G Ninja
	cmake --build {{BUILD_DIR}} --parallel

# Fast build (no tests)
fast:
	cmake --build {{BUILD_DIR}} --parallel

# Clean build
clean:
	rm -rf {{BUILD_DIR}}

# Reconfigure
reconfig:
	rm -rf {{BUILD_DIR}}
	cmake -B {{BUILD_DIR}} -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DREMIN_BUILD_GUI=ON -DREMIN_BUILD_CLI=ON -DREMIN_BUILD_TESTS=ON -G Ninja

# =============================================================================
# RUN COMMANDS
# =============================================================================

# Run GUI application
run: build
	./build/src/app/remin gui

# Run CLI
cli: build
	./build/src/cli/remin-cli

# Run with ASan
asan: 
	ASAN_OPTIONS=detect_leaks=0 ./build-asan/src/app/remin gui

# =============================================================================
# TEST COMMANDS
# =============================================================================

# Run all tests
test: build
	cd {{BUILD_DIR}} && ctest --output-on-failure

# Run tests with verbose output
test-verbose: build
	cd {{BUILD_DIR}} && ctest --output-on-failure -V

# Run specific test
test-%: build
	cd {{BUILD_DIR}} && ctest --output-on-failure -R {{*}} 

# =============================================================================
# DEVELOPMENT COMMANDS
# =============================================================================

# Format code with clang-format
fmt:
	find src -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i

# Check formatting
fmt-check:
	find src -name "*.cpp" -o -name "*.hpp" | xargs clang-format --dry-run --Werror

# Static analysis with cppcheck
analyze:
	cppcheck --enable=all --std=c++20 --suppress=missingIncludeSystem --inline-suppr --quiet src/

# Run cppcheck with XML output for CI
analyze-xml:
	cppcheck --enable=all --std=c++20 --xml --xml-version=2 src/ 2> cppcheck-report.xml

# =============================================================================
# DOCKER COMMANDS
# =============================================================================

# Build Docker image
docker-build:
	docker build -t remin:latest .

# Run in Docker
docker-run:
	docker run --rm -it --net=host -e DISPLAY remin:latest gui

# Build and run in Docker
docker-all: docker-build docker-run

# =============================================================================
# UTILITY COMMANDS
# =============================================================================

# Install system dependencies (Ubuntu/Debian)
deps:
	sudo ./scripts/install_deps.sh

# Setup development environment
setup:
	./scripts/setup_dev.sh

# Run clang-tidy
tidy:
	run-clang-tidy -p build -j$(nproc) -quiet

# Generate compile_commands.json for LSP
compile-commands:
	cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Update compile_commands.json
update-compile-commands: compile-commands

# =============================================================================
# RELEASE COMMANDS
# =============================================================================

# Build release
release:
	cmake -B build-release -DCMAKE_BUILD_TYPE=Release -G Ninja
	cmake --build build-release --parallel
	cmake --build build-release --target test

# Create release package
package: release
	cpack -B build-release --config CPackConfig.cmake

# =============================================================================
# HELP
# =============================================================================

help:
	@echo "Remin Development Commands:"
	@echo ""
	@echo "Build:"
	@echo "  build          - Configure and build (with tests)"
	@echo "  fast           - Fast build (no tests)"
	@echo "  clean          - Clean build directory"
	@echo "  reconfig       - Reconfigure and rebuild"
	@echo ""
	@echo "Run:"
	@echo "  run            - Build and run GUI"
	@echo "  cli            - Build and run CLI"
	@echo "  asan           - Run with AddressSanitizer"
	@echo ""
	@echo "Test:"
	@echo "  test           - Run all tests"
	@echo "  test-verbose   - Run tests with verbose output"
	@echo "  test-<name>    - Run specific test (e.g., test-workspace)"
	@echo ""
	@echo "Development:"
	@echo "  fmt            - Format code with clang-format"
	@echo "  fmt-check      - Check formatting"
	@echo "  analyze        - Run cppcheck static analysis"
	@echo "  tidy           - Run clang-tidy"
	@echo ""
	@echo "Docker:"
	@echo "  docker-build   - Build Docker image"
	@echo "  docker-run     - Run in Docker"
	@echo ""
	@echo "Utilities:"
	@echo "  deps           - Install system dependencies (requires sudo)"
	@echo "  setup          - Setup development environment"
	@echo "  tidy           - Run clang-tidy"
	@echo "  compile-commands - Generate compile_commands.json for LSP"