# Surtr Library
add_library(surtr_lib
    config/config.cpp
    pricing/black_scholes.cpp
    pricing/iv_solver.cpp
    pricing/forward_curve.cpp
    surface/surface_builder.cpp
    messaging/vol_surface_publisher.cpp
    messaging/status_publisher.cpp
    md/md_subscriber.cpp
    refdata/refdata_subscriber.cpp
)

target_link_libraries(surtr_lib PUBLIC
    yggdrasil
    fmt::fmt
    tomlplusplus::tomlplusplus
    aeron_client
    Threads::Threads
    bifrost::protocol
)

target_include_directories(surtr_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${aeron_SOURCE_DIR}/aeron-client/src/main/cpp
)

# Main Executable
add_executable(surtr_app main.cpp app/surtr_app.cpp)
target_link_libraries(surtr_app PRIVATE surtr_lib)
set_target_properties(surtr_app PROPERTIES OUTPUT_NAME surtr)
