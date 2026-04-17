load("@rules_cc//cc:cc_library.bzl", "cc_library")

# fast_float is header-only.
cc_library(
    name = "fast_float",
    hdrs = glob(["include/fast_float/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)
