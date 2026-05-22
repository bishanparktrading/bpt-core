# bpt-research

Jupyter notebooks + Python helpers for strategy *discovery* — the
under-built side of bpt-core. Lives outside the C++ Bazel build because
it's iterative research, not production code.

Consumes:
- `bpt-canon/python/bpt_canon` — canon → pandas
- `bpt-features/python/bpt_features` — same C++ feature impls AS uses,
  via pybind11 (so research and prod can't drift)

See `notebooks/` for current work. Roadmap in `docs/backlog.md` →
"Research stack".
