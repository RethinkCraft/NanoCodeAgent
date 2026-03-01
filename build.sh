#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Default build directory
BUILD_DIR="build"

# Function to print usage
print_usage() {
    echo "Usage: $0 [command]"
    echo ""
    echo "Commands:"
    echo "  debug    Build the project in Debug mode (default)"
    echo "  release  Build the project in Release mode"
    echo "  test     Run the test suite (builds in Debug mode first)"
    echo "  clean    Remove the build directory"
    echo "  help     Show this help message"
}

# Function to build the project
build_project() {
    local build_type=$1
    echo "Building in $build_type mode..."
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Configure with CMake
    cmake -DCMAKE_BUILD_TYPE="$build_type" ..
    
    # Build with all available cores
    make -j"$(nproc)"
    
    cd ..
    echo "Build completed successfully."
}

# Function to run tests
run_tests() {
    echo "Running tests..."
    
    # Ensure we have a debug build before testing
    build_project "Debug"
    
    cd "$BUILD_DIR"
    ctest --output-on-failure
    cd ..
    
    echo "All tests passed."
}

# Function to clean the build directory
clean_project() {
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    echo "Clean completed."
}

# Main logic
COMMAND=${1:-debug}

case "$COMMAND" in
    debug)
        build_project "Debug"
        ;;
    release)
        build_project "Release"
        ;;
    test)
        run_tests
        ;;
    clean)
        clean_project
        ;;
    help|--help|-h)
        print_usage
        ;;
    *)
        echo "Error: Unknown command '$COMMAND'"
        print_usage
        exit 1
        ;;
esac
