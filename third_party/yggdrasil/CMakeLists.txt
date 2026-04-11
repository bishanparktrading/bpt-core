cmake_minimum_required(VERSION 3.20)
project(yggdrasil VERSION 0.1.0 LANGUAGES CXX)

# yggdrasil is mostly header-only, but three translation units are compiled into
# a static library to avoid pulling heavy headers (AWS SDK, Quill internals)
# into every TU that includes the corresponding yggdrasil headers.

find_package(aws-cpp-sdk-core           CONFIG REQUIRED)
find_package(aws-cpp-sdk-secretsmanager CONFIG REQUIRED)

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
    aws-cpp-sdk-core
    aws-cpp-sdk-secretsmanager
)

target_compile_features(yggdrasil PUBLIC cxx_std_20)
