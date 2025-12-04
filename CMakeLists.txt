cmake_minimum_required(VERSION 3.16)
project(dwg_inspect LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# LibreDWG via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBREDWG REQUIRED libredwg)

add_executable(dwg_inspect
    src/main.cpp
    src/dwg_inspector.cpp
)

target_include_directories(dwg_inspect PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LIBREDWG_INCLUDE_DIRS}
)

target_link_libraries(dwg_inspect PRIVATE
    ${LIBREDWG_LIBRARIES}
)

# Don't propagate libredwg's internal CFLAGS to us; they can mess
# with dwg.h typedefs. We only need includes + -ldwg.

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dwg_inspect PRIVATE -Wall -Wextra -Wpedantic)
endif()
