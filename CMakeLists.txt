cmake_minimum_required(VERSION 3.25)

find_package(cmake-bare REQUIRED PATHS ${CMAKE_SOURCE_DIR}/node_modules/cmake-bare)
find_package(cmake-fetch REQUIRED PATHS ${CMAKE_SOURCE_DIR}/node_modules/cmake-fetch)

project(bare_ltx2 C CXX)

include(cmake-fetch)

fetch_package(
  "github:64johnlee/qvac-ext-stable-diffusion.cpp#ltx2-video-generation"
  SOURCE_DIR sd_source_dir
)

set(SD_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SD_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(SD_BUILD_SERVER     OFF CACHE BOOL "" FORCE)

add_subdirectory(${sd_source_dir} deps/stable-diffusion.cpp EXCLUDE_FROM_ALL)

add_bare_module(bare_ltx2)

target_sources(bare_ltx2 PRIVATE binding.c)

target_include_directories(bare_ltx2 PRIVATE
  ${sd_source_dir}/include
)

target_link_libraries(bare_ltx2 PRIVATE stable-diffusion)

if (MSVC)
  target_compile_options(bare_ltx2 PRIVATE /W0)
else()
  target_compile_options(bare_ltx2 PRIVATE -w)
endif()
