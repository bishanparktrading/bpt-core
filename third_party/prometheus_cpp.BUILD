load("@rules_cc//cc:cc_library.bzl", "cc_library")

# We skip prometheus-cpp's upstream Bazel machinery and compile core+pull into
# our own targets. Zlib support is off — scrape responses are uncompressed,
# which is fine for a localhost Prometheus.

# Stand-in export-macro headers (upstream generates these via CMake).
genrule(
    name = "gen_core_export",
    outs = ["core/include/prometheus/detail/core_export.h"],
    srcs = ["@//third_party/prometheus_export_headers:core_export.h"],
    cmd = "cp $< $@",
)

genrule(
    name = "gen_pull_export",
    outs = ["pull/include/prometheus/detail/pull_export.h"],
    srcs = ["@//third_party/prometheus_export_headers:pull_export.h"],
    cmd = "cp $< $@",
)

cc_library(
    name = "core",
    srcs = glob(["core/src/**/*.cc", "core/src/**/*.h"]),
    hdrs = glob(["core/include/**/*.h"]) + [":gen_core_export"],
    strip_include_prefix = "core/include",
    visibility = ["//visibility:public"],
)

cc_library(
    name = "util",
    hdrs = glob(["util/include/**/*.h"]),
    strip_include_prefix = "util/include",
    visibility = ["//visibility:public"],
)

cc_library(
    name = "pull",
    srcs = glob(["pull/src/**/*.cc", "pull/src/**/*.h"]),
    hdrs = glob(["pull/include/**/*.h"]) + [":gen_pull_export"],
    strip_include_prefix = "pull/include",
    visibility = ["//visibility:public"],
    deps = [
        ":core",
        ":util",
        "@civetweb//:civetserver",
    ],
)
