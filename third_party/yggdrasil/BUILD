load("@rules_cc//cc:cc_library.bzl", "cc_library")

# Core yggdrasil — logging, signal handling, TSC clock, Aeron utils.
# Excludes secrets_client (needs AWS SDK) and ws_connect (needs Boost.Beast).
# Services that need those should add them as separate targets later.
cc_library(
    name = "yggdrasil",
    srcs = [
        "src/logging.cpp",
        "src/util/tsc_clock.cpp",
    ],
    hdrs = glob(["include/yggdrasil/**/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [
        "@quill",
        "@aeron//:aeron_client",
        "@fmt",
        "@tomlplusplus//:tomlplusplus",
    ],
    linkopts = ["-lpthread"],
)
