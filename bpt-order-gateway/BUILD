load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

# Core order-gateway components needed by tests — no network adapters.
cc_library(
    name = "order_gateway_core",
    srcs = [
        "src/config/loader.cpp",
        "src/order/order_state_manager.cpp",
        "src/risk/risk_checker.cpp",
        "src/adapter/hyperliquid/hyperliquid_action_codec.cpp",
        "src/adapter/hyperliquid/hyperliquid_exec_parser.cpp",
        "src/adapter/hyperliquid/hyperliquid_signer.cpp",
    ],
    hdrs = glob(["include/order_gateway/**/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [
        "//messages",
        "//third_party/yggdrasil",
        "@boost.json",
        "@fmt",
        "@openssl",
        "@secp256k1//:secp256k1",
        "@tomlplusplus//:tomlplusplus",
    ],
)

cc_test(
    name = "order_gateway_unit_tests",
    srcs = glob(["tests/unit/*.cpp"]),
    deps = [
        ":order_gateway_core",
        "@googletest//:gtest_main",
    ],
)
