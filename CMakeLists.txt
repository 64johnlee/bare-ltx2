cmake_minimum_required(VERSION 3.25)

find_package(cmake-bare REQUIRED PATHS ${CMAKE_SOURCE_DIR}/node_modules/cmake-bare)
find_package(cmake-fetch REQUIRED PATHS ${CMAKE_SOURCE_DIR}/node_modules/cmake-fetch)

project(bare_ltx2 C CXX)

include(cmake-fetch)

# These must be set before fetch_package so FetchContent_MakeAvailable
# picks them up before configuring the subdirectory.
set(SD_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SD_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(SD_BUILD_SERVER     OFF CACHE BOOL "" FORCE)

# fetch_package calls FetchContent_MakeAvailable internally which already
# calls add_subdirectory — do NOT call add_subdirectory again.
fetch_package(
  "github:64johnlee/qvac-ext-stable-diffusion.cpp#ltx2-video-generation"
  SOURCE_DIR sd_source_dir
)

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
