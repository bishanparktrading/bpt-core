load("@rules_cc//cc:cc_library.bzl", "cc_library")

cc_library(
    name = "aeron_client",
    srcs = glob(["aeron-client/src/main/cpp/**/*.cpp"]),
    hdrs = glob([
        "aeron-client/src/main/cpp/**/*.h",
    ]),
    includes = ["aeron-client/src/main/cpp"],
    visibility = ["//visibility:public"],
    linkopts = ["-lpthread"],
    defines = [
        "AERON_COMPILER_MSVC=0",
        'AERON_VERSION_MAJOR=1',
        'AERON_VERSION_MINOR=44',
        'AERON_VERSION_PATCH=1',
        'AERON_VERSION_GITSHA=\\"bazel\\"',
        'AERON_VERSION_TXT=\\"1.44.1\\"',
    ],
)
