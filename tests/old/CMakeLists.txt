cmake_minimum_required(VERSION 3.10)
project(uroam VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Compiler flags
if(CMAKE_COMPILER_IS_GNUCC)
    add_compile_options(-Wall -Wextra -O2)
elseif(MSVC)
    add_compile_options(/W4)
endif()

# Find required packages
find_package(PkgConfig REQUIRED)

# Kernel module (optional)
option(BUILD_KERNEL_MODULE "Build kernel module" ON)

if(BUILD_KERNEL_MODULE)
    add_subdirectory(kernel)
endif()

# User-space daemon (Python script)
configure_file(src/user/uroam_daemon.py ${CMAKE_BINARY_DIR}/bin/uroam_daemon COPYONLY)
file(INSTALL src/user/uroam_daemon.py DESTINATION ${CMAKE_BINARY_DIR}/bin/
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# CLI tool (Python script)
configure_file(src/user/uroam_cli.py ${CMAKE_BINARY_DIR}/bin/uroam_cli COPYONLY)
file(INSTALL src/user/uroam_cli.py DESTINATION ${CMAKE_BINARY_DIR}/bin/
     FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Install targets
install(FILES src/user/uroam_daemon.py
    DESTINATION bin
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    RENAME uroam_daemon
)

install(FILES src/user/uroam_cli.py
    DESTINATION bin
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    RENAME uroam_cli
)

# Tests
enable_testing()
add_test(NAME test_classification COMMAND ${CMAKE_COMMAND} -E echo "Tests would run here")
