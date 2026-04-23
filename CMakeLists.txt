cmake_minimum_required(VERSION 3.25)

project(UROAM VERSION 1.0.0 LANGUAGES C Rust)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(FeatureSummary)
include(GNUInstallDirs)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(UROAM_BUILD_TESTS "Build tests" ON)
option(UROAM_BUILD_RUST "Build Rust components" ON)
option(UROAM_BUILD_KERNEL "Build kernel module" ON)

set(UROAM_VERSION_MAJOR 1)
set(UROAM_VERSION_MINOR 0)
set(UROAM_VERSION_PATCH 0)
set(UROAM_VERSION "${UROAM_VERSION_MAJOR}.${UROAM_VERSION_MINOR}.${UROAM_VERSION_PATCH}")

# Find required packages
find_package(Python3 3.10 QUIET)
find_package(Threads REQUIRED)

# Check for Rust
if(UROAM_BUILD_RUST)
    find_program(CARGO cargo)
    find_program(RUSTC rustc)
    if(CARGO AND RUSTC)
        set(HAVE_RUST TRUE)
    else()
        message(WARNING "Rust/Cargo not found, skipping Rust components")
        set(HAVE_RUST FALSE)
    endif()
endif()

# AAL (Architecture Abstraction Layer)
add_subdirectory(aal)

# C Shared Library
add_subdirectory(src/shared)

# Kernel module
if(UROAM_BUILD_KERNEL)
    find_path(KERNEL_SOURCE_DIR
        NAMES include/linux/module.h include/generated/utsrelease.h
        PATHS /lib/modules/${CMAKE_SYSTEM_VERSION}/build /usr/src/linux-headers-${CMAKE_SYSTEM_VERSION}
        NO_DEFAULT_PATH
    )
    if(KERNEL_SOURCE_DIR)
        add_subdirectory(kmod)
    else()
        message(WARNING "Kernel headers not found, skipping kernel module")
    endif()
endif()

# eBPF
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/ebpf)
    add_subdirectory(ebpf)
endif()

# Rust components
if(HAVE_RUST AND UROAM_BUILD_RUST)
    # CMake doesn't have native Rust support, use custom command
    add_custom_target(uroamd
        COMMAND ${CARGO} build --release
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Building uroamd with Cargo"
        VERBOSE
    )
    add_custom_target(ramctl
        COMMAND ${CARGO} build --release
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Building ramctl with Cargo"
        VERBOSE
    )
endif()

# Tests
if(UROAM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation
install(FILES include/uroam.h include/uroam_aal.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Packaging
set(CPACK_PACKAGE_NAME "uroam")
set(CPACK_PACKAGE_VERSION "${UROAM_VERSION}")
set(CPACK_PACKAGE_CONTACT "UROAM Team <team@uroam.org>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Unified RAM Optimization and Management Framework")
include(CPack)

feature_summary(WHAT ALL)
