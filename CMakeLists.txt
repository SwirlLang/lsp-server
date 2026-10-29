cmake_minimum_required(VERSION 3.30)

project(SwirlLSP)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES "src/*.cpp")
message(STATUS "Found sources: ${SOURCES}")
add_executable(swirl_lsp ${SOURCES})
include_directories(include)