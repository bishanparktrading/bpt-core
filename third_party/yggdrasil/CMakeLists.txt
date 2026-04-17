cmake_minimum_required(VERSION 3.20)
project(yggdrasil VERSION 0.1.0 LANGUAGES CXX)

# yggdrasil is mostly header-only. A few translation units are compiled into a
# static library to keep heavy headers (Quill internals) out of every TU that
# only needs the corresponding yggdrasil headers.

add_library(yggdrasil STATIC
    src/logging.cpp
    src/util/tsc_clock.cpp
    src/secrets/secrets_client.cpp
)

target_include_directories(yggdrasil PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(yggdrasil PUBLIC
    quill::quill
)

target_compile_features(yggdrasil PUBLIC cxx_std_20)
