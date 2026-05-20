load("@rules_cc//cc:cc_binary.bzl", "cc_binary")

# Smoke test: loads an instrument_mapping.json file via bpt-refdata's
# actual InstrumentMappingLoader. Validates that the renderer's output
# is wire-compatible with the existing C++ trading stack.

cc_binary(
    name = "smoke_load_mapping",
    srcs = ["smoke_load_mapping.cpp"],
    deps = [
        "//bpt-refdata:refdata_lib",
    ],
)
