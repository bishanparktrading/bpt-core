load("@rules_cc//cc:cc_library.bzl", "cc_library")

# Header-only JSON library — single file at `single_include/nlohmann/json.hpp`.
cc_library(
    name = "nlohmann_json",
    hdrs = ["single_include/nlohmann/json.hpp"],
    includes = ["single_include"],
    visibility = ["//visibility:public"],
)
