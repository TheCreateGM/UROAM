# Kernel module build configuration

# Kernel module name
set(MODULE_NAME uroam)

# Source files
set(MODULE_SOURCES
    main.c
    hooks.c
    pressure.c
    tracking.c
    numa.c
    ipc.c
)

# Generate module version info
string(TIMESTAMP NOW "%Y-%m-%d %H:%M:%S")
add_definitions(-DMODULE_VERSION="0.1.0")
add_definitions(-DMODULE_BUILD_DATE="${NOW}")

# Find kernel source
find_path(KERNEL_SOURCE_DIR
    NAMES
        include/linux/module.h
        include/generated/utsrelease.h
    PATHS
        /lib/modules/${KERNEL_VERSION}/build
        /usr/src/linux-headers-${KERNEL_VERSION}
        /usr/src/linux
        /usr/lib/modules/${KERNEL_VERSION}/build
)

if(NOT KERNEL_SOURCE_DIR)
    message(FATAL_ERROR "Linux kernel source not found. Please install kernel headers.")
endif()

# Kernel module specific compile flags
set(KERNEL_CFLAGS -D__KERNEL__ -DMODULE -I${KERNEL_SOURCE_DIR}/include)

# Include directories
include_directories(
    ${KERNEL_SOURCE_DIR}/include
    ${KERNEL_SOURCE_DIR}/arch/x86/include
    ${KERNEL_SOURCE_DIR}/include/linux
    ${KERNEL_SOURCE_DIR}/include/uapi/linux
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Create the kernel module
add_library(${MODULE_NAME} MODULE EXCLUDE_FROM_ALL ${MODULE_SOURCES})

# Set target properties
target_include_directories(${MODULE_NAME}
    PRIVATE
        ${KERNEL_SOURCE_DIR}/include
        ${KERNEL_SOURCE_DIR}/arch/x86/include
        ${KERNEL_SOURCE_DIR}/include/linux
        ${KERNEL_SOURCE_DIR}/include/uapi/linux
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Disable compiler-specific flags that conflict with kernel builds
set_target_properties(${MODULE_NAME} PROPERTIES
    SUFFIX ".ko"
    C_STANDARD 99  # Kernel uses C99
    C_STANDARD_REQUIRED OFF
    POSITION_INDEPENDENT_CODE OFF
)

# Set module-specific flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU")
    target_compile_options(${MODULE_NAME}
        PRIVATE
            -Wno-pointer-sign
            -Wno-format-security
            -Wno-int-conversion
            -Wno-implicit-fallthrough
            -Wno-shift-negative-value
            -Wno-incompatible-pointer-types
    )
endif()

# Kernel version check (optional)
# Try to get kernel version
find_program(UNAME uname)
if(UNAME)
    execute_process(
        COMMAND ${UNAME} -r
        OUTPUT_VARIABLE KERNEL_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    execute_process(
        COMMAND uname -r
        OUTPUT_VARIABLE KERNEL_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

message(STATUS "Building kernel module for: ${KERNEL_VERSION}")
message(STATUS "Kernel source: ${KERNEL_SOURCE_DIR}")

# Copy module to installation directory
install(TARGETS ${MODULE_NAME}
    LIBRARY DESTINATION lib/modules/${KERNEL_VERSION}/kernel/drivers/misc
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

# Also install to project-local directory for development
install(TARGETS ${MODULE_NAME}
    LIBRARY DESTINATION bin
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)
