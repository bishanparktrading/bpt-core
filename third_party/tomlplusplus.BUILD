load("@rules_cc//cc:cc_library.bzl", "cc_library")

# tomlplusplus v3.4.0 is header-only. We use the full header tree
# (`include/toml++/toml.hpp`) so our existing `#include <toml++/toml.hpp>`
# paths keep working unchanged.
cc_library(
    name = "tomlplusplus",
    hdrs = glob(["include/toml++/**/*.h", "include/toml++/**/*.hpp", "include/toml++/**/*.inl"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)
