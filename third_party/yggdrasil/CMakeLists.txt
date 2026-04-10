cmake_minimum_required(VERSION 3.20)
project(yggdrasil VERSION 0.1.0 LANGUAGES CXX)

# Header-only interface library — no compilation required.
# Consumers must have spdlog and Aeron already linked in their own CMakeLists.
add_library(yggdrasil INTERFACE)

target_include_directories(yggdrasil INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(yggdrasil INTERFACE cxx_std_20)
