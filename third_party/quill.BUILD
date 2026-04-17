load("@rules_cc//cc:cc_library.bzl", "cc_library")

# Quill v8 is header-only.
cc_library(
    name = "quill",
    hdrs = glob(["include/**/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    defines = [
        "QUILL_FMT_EXTERNAL",
    ],
    deps = [
        "@fmt",
    ],
)
