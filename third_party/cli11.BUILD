load("@rules_cc//cc:cc_library.bzl", "cc_library")

# CLI11 is distributed as a single-header library plus a split-headers
# layout. We use the single-header form: include/CLI/CLI.hpp pulls
# everything in.
cc_library(
    name = "cli11",
    hdrs = glob(["include/CLI/**/*.hpp"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)
