load("@rules_cc//cc:cc_library.bzl", "cc_library")

# secp256k1 normally builds with autoconf. We skip that and compile the three
# translation units directly with the same defines autoconf would emit. The
# `recovery` module is required for Hyperliquid's ECDSA-with-recovery signing.
cc_library(
    name = "secp256k1",
    srcs = [
        "src/secp256k1.c",
        "src/precomputed_ecmult.c",
        "src/precomputed_ecmult_gen.c",
    ],
    hdrs = [
        "include/secp256k1.h",
        "include/secp256k1_preallocated.h",
        "include/secp256k1_recovery.h",
    ],
    textual_hdrs = glob([
        "src/**/*.h",
        "src/**/*.c",
    ], exclude = [
        "src/secp256k1.c",
        "src/precomputed_ecmult.c",
        "src/precomputed_ecmult_gen.c",
        "src/bench*.c",
        "src/tests*.c",
        "src/valgrind_ctime_test.c",
        "src/ctime_tests.c",
    ]),
    includes = ["include"],
    defines = [
        "ENABLE_MODULE_RECOVERY=1",
        "ECMULT_GEN_PREC_BITS=4",
        "ECMULT_WINDOW_SIZE=15",
    ],
    copts = [
        "-w",  # library emits many -Wunused-function warnings by design
    ],
    visibility = ["//visibility:public"],
)
