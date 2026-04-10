add_library(jormungandr_lib
    app/jormungandr_app.cpp
    clock/clock_master.cpp
    config/loader.cpp
    data/data_loader.cpp
    exchange/binance_md_server.cpp
    exchange/binance_order_server.cpp
    exchange/okx_md_server.cpp
    exchange/okx_order_server.cpp
    matching/matching_engine.cpp
    messaging/backtest_ack_subscriber.cpp
    messaging/backtest_control_publisher.cpp
    results/results_collector.cpp
)

target_include_directories(jormungandr_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${aeron_SOURCE_DIR}/aeron-client/src/main/cpp
)

target_link_libraries(jormungandr_lib PUBLIC
    aeron_client
    yggdrasil
    bifrost::protocol
    fmt::fmt
    spdlog::spdlog
    tomlplusplus::tomlplusplus
    Boost::system
    Boost::json
    prometheus-cpp::pull
    Arrow::arrow_shared
    Parquet::parquet_shared
    Threads::Threads
)

add_executable(jormungandr main.cpp)

target_link_libraries(jormungandr PRIVATE jormungandr_lib)
