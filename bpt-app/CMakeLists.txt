cmake_minimum_required(VERSION 3.20)
project(bpt-app VERSION 0.1.0 LANGUAGES CXX)

# bpt-app — shared application skeleton. Mostly headers (bpt::app::run template);
# one .cpp for the TOML base-settings loader.

add_library(bpt-app STATIC
    src/base_settings.cpp
)

target_include_directories(bpt-app PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(bpt-app PUBLIC
    bpt-common
    tomlplusplus::tomlplusplus
)

target_compile_features(bpt-app PUBLIC cxx_std_20)
