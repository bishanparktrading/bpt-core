load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

# bpt-canon — derived canonical event tape. Reader/writer for `.canon`
# files, the schema-stable, SBE-encoded format the deterministic
# backtester consumes. Producers (wslog-replay, OKX-archive-ingest, etc.)
# live as separate binaries; this package is just the format library.
#
# Step 1 (this commit): library + roundtrip test only. Step 2 wires it
# into the backtester as an alternative source to `--wslog`.

cc_library(
    name = "bpt-canon",
    srcs = [
        "src/canon_writer.cpp",
    ],
    hdrs = [
        "include/canon/canon_format.h",
        "include/canon/canon_reader.h",
        "include/canon/canon_writer.h",
    ],
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [
        "//bpt-common",
    ],
)

# SBE encoder/decoder helpers. Kept in a separate target so consumers
# that only need the on-disk format (CanonReader-only readers, e.g.
# offline tooling) don't have to drag in md_gateway_lib + messages.
cc_library(
    name = "canon_sbe",
    srcs = [
        "src/canon_sbe.cpp",
    ],
    hdrs = [
        "include/canon/canon_recording_publisher.h",
        "include/canon/canon_sbe.h",
    ],
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [
        ":bpt-canon",
        "//bpt-md-gateway:md_gateway_lib",
        "//messages",
    ],
)

cc_binary(
    name = "bpt-canon-replay",
    srcs = [
        "src/replay_main.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":bpt-canon",
        ":canon_sbe",
        "//bpt-common",
        "//bpt-md-gateway:md_gateway_lib",
        "//bpt-refdata:mapping_lib",
        "//messages",
        "@cli11",
        "@fmt",
    ],
)

# OKX trades CSV → canon. Reads OKX's free daily trades archive
# (downloadable from https://www.okx.com/en-sg/historical-data) and
# emits canon TRADE events. Sibling: bpt-canon-ingest-okx-l2 covers the
# L2 orderbook NDJSON stream from the same data portal.
cc_binary(
    name = "bpt-canon-ingest-okx-trades",
    srcs = [
        "src/ingest_okx_trades_main.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":bpt-canon",
        ":canon_sbe",
        "//bpt-common",
        "//bpt-md-gateway:md_gateway_lib",
        "//bpt-refdata:mapping_lib",
        "//messages",
        "@cli11",
        "@fast_float",
        "@fmt",
    ],
)

# OKX L2 NDJSON → canon BBO + BOOK events. Stateful: maintains the full
# venue book in memory (snapshot + deltas), emits top-of-book + L2(20)
# canon events after each apply. The L2 archive is large (~2GB/day for
# busy instruments) — sequential stream, no random access.
cc_binary(
    name = "bpt-canon-ingest-okx-l2",
    srcs = [
        "src/ingest_okx_l2_main.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":bpt-canon",
        ":canon_sbe",
        "//bpt-common",
        "//bpt-md-gateway:md_gateway_lib",
        "//bpt-refdata:mapping_lib",
        "//messages",
        "@cli11",
        "@fast_float",
        "@fmt",
        "@simdjson",
    ],
)

# k-way ts-merge of canon files. Sibling producers (trades + L2) write
# independent files; this tool merges them into one canon the backtester
# can consume directly.
cc_binary(
    name = "bpt-canon-merge",
    srcs = [
        "src/merge_main.cpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":bpt-canon",
        "//bpt-common",
        "@cli11",
        "@fmt",
    ],
)

cc_test(
    name = "bpt_canon_unit_tests",
    srcs = glob(["tests/unit/*.cpp"]),
    deps = [
        ":bpt-canon",
        ":canon_sbe",
        "@googletest//:gtest_main",
    ],
)
