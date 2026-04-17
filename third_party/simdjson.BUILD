load("@rules_cc//cc:cc_library.bzl", "cc_library")

# simdjson ships an amalgamated single-TU build (`singleheader/simdjson.h` + `.cpp`).
# Using it avoids having to replicate the CMake config for CPU-dispatched kernels.
cc_library(
    name = "simdjson",
    srcs = ["singleheader/simdjson.cpp"],
    hdrs = ["singleheader/simdjson.h"],
    includes = ["singleheader"],
    visibility = ["//visibility:public"],
)
