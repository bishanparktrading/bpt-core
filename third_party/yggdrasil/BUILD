load("@rules_cc//cc:cc_library.bzl", "cc_library")

# Core yggdrasil — logging, signal handling, TSC clock, Aeron utils, secrets.
# Excludes ws_connect (needs Boost.Beast — keep per-service if they need it).
# Secrets client now reads from $CREDENTIALS_DIRECTORY (systemd-creds) — no
# more aws-sdk-cpp dependency.
cc_library(
    name = "yggdrasil",
    srcs = [
        "src/logging.cpp",
        "src/secrets/secrets_client.cpp",
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
