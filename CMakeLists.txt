cmake_minimum_required(VERSION 3.30)

project(SwirlLSP VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# core library
add_library(swirl_lsp_core STATIC
    src/LSPServer.cpp
)
target_include_directories(swirl_lsp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# executable
add_executable(swirl_lsp src/main.cpp)
target_link_libraries(swirl_lsp PRIVATE swirl_lsp_core)

# install
include(GNUInstallDirs)
install(TARGETS swirl_lsp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})