load("@rules_python//python:py_test.bzl", "py_test")

# Lives at python/ level so the test imports bpt_canon as a package.
py_test(
    name = "test_reader",
    srcs = ["tests/test_reader.py"],
    deps = ["//bpt-canon/python/bpt_canon"],
)
