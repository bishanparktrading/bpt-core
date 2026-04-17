load("@rules_cc//cc:cc_library.bzl", "cc_library")

# Minimal civetweb v1.16 — no SSL (metrics scrape on localhost), no CGI, no files.
# Adapted from prometheus-cpp's shipped civetweb.BUILD.
COPTS = [
    "-DUSE_IPV6",
    "-DNDEBUG",
    "-DNO_CGI",
    "-DNO_CACHING",
    "-DNO_FILES",
    "-DNO_SSL",
    "-UDEBUG",
]

cc_library(
    name = "civetweb",
    srcs = ["src/civetweb.c"],
    hdrs = ["include/civetweb.h"],
    copts = COPTS,
    includes = ["include"],
    linkopts = ["-lpthread", "-lrt"],
    textual_hdrs = [
        "src/handle_form.inl",
        "src/match.inl",
        "src/md5.inl",
        "src/response.inl",
        "src/sort.inl",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "civetserver",
    srcs = ["src/CivetServer.cpp"],
    hdrs = ["include/CivetServer.h"],
    copts = COPTS,
    includes = ["include"],
    linkopts = ["-lpthread", "-lrt"],
    visibility = ["//visibility:public"],
    deps = [":civetweb"],
)
