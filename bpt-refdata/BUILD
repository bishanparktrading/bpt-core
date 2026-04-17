load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

# NOTE: `mapping/instrument_mapping_s3_fetcher.cpp` and `main.cpp`/`app/`
# are NOT included — they depend on aws-sdk-cpp, which has no BCR module
# and is costly to build via rules_foreign_cc. Tests don't touch S3, so
# this test-only lib is sufficient. The app binary stays on CMake for now.
cc_library(
    name = "refdata_lib",
    srcs = [
        "src/mapping/instrument_mapping_loader.cpp",
        "src/registry/instrument_registry.cpp",
        "src/resolver/instrument_resolver.cpp",
        "src/messaging/refdata_control_subscriber.cpp",
        "src/messaging/refdata_snapshot_publisher.cpp",
        "src/messaging/refdata_delta_publisher.cpp",
        "src/messaging/subscription_manager.cpp",
        "src/messaging/fee_schedule_publisher.cpp",
        "src/messaging/refdata_status_publisher.cpp",
        "src/config/loader.cpp",
        "src/http/rest_client.cpp",
        "src/adapter/binance/binance_parser.cpp",
        "src/adapter/binance/binance_refdata_adapter.cpp",
        "src/adapter/okx/okx_parser.cpp",
        "src/adapter/okx/okx_refdata_adapter.cpp",
        "src/adapter/hyperliquid/hyperliquid_parser.cpp",
        "src/adapter/hyperliquid/hyperliquid_refdata_adapter.cpp",
        "src/adapter/deribit/deribit_refdata_adapter.cpp",
        "src/metrics/metrics.cpp",
    ],
    hdrs = glob(["include/refdata/**/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [
        "//messages",
        "//third_party/yggdrasil",
        "@boost.asio",
        "@boost.beast",
        "@boost.system",
        "@fmt",
        "@nlohmann_json//:nlohmann_json",
        "@openssl",
        "@prometheus_cpp//:core",
        "@prometheus_cpp//:pull",
        "@tomlplusplus//:tomlplusplus",
    ],
)

cc_test(
    name = "refdata_tests",
    srcs = glob(["tests/*.cpp"]),
    deps = [
        ":refdata_lib",
        "@googletest//:gtest_main",
    ],
)
