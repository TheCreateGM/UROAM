cmake_minimum_required(VERSION 3.25)

add_library(aal STATIC
    aal.c
)

target_include_directories(aal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_options(aal PRIVATE
    -Wall -Wextra -Wpedantic
    -fPIC
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(aal PRIVATE
        -fno-builtin
    )
endif()

install(TARGETS aal
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../include/uroam_aal.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)