cmake_minimum_required(VERSION 3.16)

# Universal RAM Optimization Layer - Top-level CMake configuration
project(uroam VERSION 0.1.0 LANGUAGES C CXX)

# Project metadata
set(PROJECT_NAME "uroam")
set(PROJECT_DESCRIPTION "Universal RAM Optimization Layer for AI Workloads on Linux")
set(PROJECT_AUTHOR "Universal RAM Optimization Project")
set(PROJECT_LICENSE "GPL-2.0-only")

# Set C/C++ standards
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_options(-Wshadow)
    add_compile_options(-Wconversion)
    add_compile_options(-Wunused)
    add_compile_options(-Wno-unused-parameter)

    # Kernel module specific flags
    add_compile_options($<$<COMPILING_KERNEL_MODULE:-"${KERNEL_CFLAGS}">>)
endif()

# Enable debugging in debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0)
    add_definitions(-DDEBUG=1)
else()
    add_compile_options(-O2)
endif()

# Enable optimization in release builds
set(CMAKE_INSTALL_PREFIX "/usr/local")

# Options
option(BUILD_KERNEL_MODULE "Build kernel module" ON)
option(BUILD_USERpace_LIBRARY "Build user-space library" ON)
option(BUILD_PYTHON_BINDINGS "Build Python bindings" ON)
option(BUILD_TESTS "Build tests" OFF)
option(ENABLE_EBPF "Enable eBPF features" OFF)

# Dependencies
find_package(Threads REQUIRED)

# Kernel module source files
if(BUILD_KERNEL_MODULE)
    add_subdirectory(kernel)
endif()

# User-space library
if(BUILD_USERpace_LIBRARY)
    add_subdirectory(lib)
endif()

# Python bindings
if(BUILD_PYTHON_BINDINGS)
    find_package(Python3 COMPONENTS Development REQUIRED)
    add_subdirectory(integrations)
endif()

# CLI tools
add_subdirectory(cli)

# Dashboard
add_subdirectory(dashboard)

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation targets
install(DIRECTORY include/ DESTINATION include/uroam)
install(DIRECTORY etc/ DESTINATION etc/uroam)

# CPack for package generation
include(InstallRequiredSystemLibraries)
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR ${PROJECT_AUTHOR})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "DEB;RPM;TGZ")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER ${PROJECT_AUTHOR})
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.31)")

include(CPack)
