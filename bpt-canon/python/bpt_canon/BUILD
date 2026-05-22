load("@rules_python//python:py_library.bzl", "py_library")

# Pure-Python canon reader. SBE offsets are hand-decoded in reader.py;
# bump the format constants if MdMarketData/MdTrade layouts change.
py_library(
    name = "bpt_canon",
    srcs = glob(["*.py"]),
    imports = [".."],
    visibility = ["//visibility:public"],
    # pandas is lazy-imported from system Python (no @pypi// in MODULE.bazel).
)
