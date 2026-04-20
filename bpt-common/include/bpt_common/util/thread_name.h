#pragma once

// thread_name.h — helper to set the OS thread name (TASK_COMM) for the
// calling thread.
//
// Why: application threads inherit the process `comm` (post-prctl the role-
// qualified name like `bpt-mdgw-okx`). That makes every worker thread
// indistinguishable in `ps -L` / `top -H` / `perf top` — N identical rows
// per process. Naming long-lived role-distinct threads (adapter IO loops,
// publishers, main poll loops) is the last mile of ops visibility: when
// you see `mdgw-io-okx` burning CPU vs `mdgw-pub-okx`, you know which
// code path to look at.
//
// Rules baked in (same discipline as backend_thread_name_for() for the
// quill backend thread):
//   - Linux TASK_COMM_LEN = 16 bytes incl. null → 15 usable chars.
//     Overflow is silently truncated by the kernel; we pre-truncate here
//     so the truncated name is deterministic, not byte-count-dependent.
//   - Empty name = no-op (lets callers gate the call without branching).
//   - Call from inside the thread you want to name (uses pthread_self()).
//
// Intentionally NOT touching the main thread / leader — its `comm` IS the
// process `comm`, which we already set via prctl(PR_SET_NAME) in bpt-app.
// Overriding it here would undo that.

#include <cstring>
#include <pthread.h>
#include <string>
#include <string_view>

namespace bpt::common::util {

inline void set_thread_name(std::string_view name) {
    if (name.empty())
        return;

#ifdef __linux__
    // 15 usable chars; leave room for null.
    constexpr std::size_t kMax = 15;
    char buf[kMax + 1]{};
    const std::size_t n = name.size() < kMax ? name.size() : kMax;
    std::memcpy(buf, name.data(), n);
    buf[n] = '\0';
    // pthread_setname_np can fail (permission, invalid arg) but the only
    // consequence is the thread stays with the inherited comm — a name
    // failure shouldn't take down a hot-path thread at startup, so we
    // ignore the return value. Check it via ps/top post-start if you
    // need to know.
    (void)pthread_setname_np(pthread_self(), buf);
#else
    (void)name;  // macOS uses pthread_setname_np with no tid arg; BSDs differ — stub for now
#endif
}

}  // namespace bpt::common::util
