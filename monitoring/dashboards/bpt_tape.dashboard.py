"""BPT Tape — recorder freshness, throughput, and disk health.

The dashboard a recorder operator opens first when something feels off.
Designed around the failure mode that bit us on 2026-05-09: writer alive,
no data flowing, no alert. Top row makes that condition visible at a
glance; lower rows are the supporting evidence (rate, rotations, errors).

Render with:
    cd monitoring && make

Output lands in monitoring/generated/bpt_tape.json and is picked up by
Grafana via file provisioning on next reload.
"""
from grafanalib.core import (
    BYTES_FORMAT,
    Dashboard,
    Graph,
    GridPos,
    OPS_FORMAT,
    SECONDS_FORMAT,
    SHORT_FORMAT,
    Stat,
    Target,
    Templating,
    Time,
    YAxes,
    YAxis,
)

DATASOURCE = "Prometheus"


def stat(title, expr, *, x, y, w=4, h=3, unit=SHORT_FORMAT,
         color_mode="value", thresholds=None, decimals=None):
    s = Stat(
        title=title,
        dataSource=DATASOURCE,
        targets=[Target(expr=expr, refId="A")],
        gridPos=GridPos(h=h, w=w, x=x, y=y),
        format=unit,
        colorMode=color_mode,
        reduceCalc="lastNotNull",
        graphMode="area",
        textMode="value_and_name",
    )
    if thresholds is not None:
        s.thresholds = thresholds
    if decimals is not None:
        s.decimals = decimals
    return s


def graph(title, targets, *, x, y, w=12, h=7, unit=SHORT_FORMAT):
    return Graph(
        title=title,
        dataSource=DATASOURCE,
        targets=targets,
        gridPos=GridPos(h=h, w=w, x=x, y=y),
        yAxes=YAxes(
            left=YAxis(format=unit),
            right=YAxis(format=SHORT_FORMAT),
        ),
    )


def target(expr, legend):
    return Target(expr=expr, legendFormat=legend, refId=legend)


# Freshness thresholds: green if last write was within 1 min, yellow up
# to 5 min, red beyond. Matches TapeWriterStale alert (300s = critical).
FRESHNESS_THRESHOLDS = [
    {"color": "green",  "value": None},
    {"color": "yellow", "value": 60},
    {"color": "red",    "value": 300},
]

HEALTH_THRESHOLDS = [
    {"color": "red",   "value": None},
    {"color": "green", "value": 1},
]


# ── Row 1: At-a-glance freshness (y=0) ────────────────────────────────
# Two big numbers + the healthy gauge. If the writer-stale number turns
# red, you're in incident mode — go check the recorder before reading
# anything else on this dashboard.

top_row = [
    stat(
        "tape healthy",
        'bpt_tape_healthy{job="bpt-tape"} or on() vector(0)',
        x=0, y=0, w=4, h=4,
        thresholds=HEALTH_THRESHOLDS,
    ),
    stat(
        "since last write — md (s)",
        'time() - bpt_tape_last_wslog_write_unix_seconds{job="bpt-tape",venue="hyperliquid"}',
        x=4, y=0, w=10, h=4,
        unit=SECONDS_FORMAT,
        decimals=0,
        thresholds=FRESHNESS_THRESHOLDS,
    ),
    stat(
        "since last write — rest (s)",
        'time() - bpt_tape_last_wslog_write_unix_seconds{job="bpt-tape",venue="hyperliquid-rest"}',
        x=14, y=0, w=10, h=4,
        unit=SECONDS_FORMAT,
        decimals=0,
        # REST polls hourly so 5min freshness threshold doesn't apply;
        # set a wider threshold (90 min before red).
        thresholds=[
            {"color": "green",  "value": None},
            {"color": "yellow", "value": 3700},   # ~62 min
            {"color": "red",    "value": 5400},   # 90 min
        ],
    ),
]


# ── Row 2: Throughput (y=4) ───────────────────────────────────────────

throughput_panels = [
    graph(
        "Frames written rate",
        [target('rate(bpt_tape_frames_written_total{job="bpt-tape"}[1m])', '{{venue}}')],
        x=0, y=4, w=12, h=7, unit=OPS_FORMAT,
    ),
    graph(
        "Bytes written rate",
        [target('rate(bpt_tape_bytes_written_total{job="bpt-tape"}[1m])', '{{venue}}')],
        x=12, y=4, w=12, h=7, unit=BYTES_FORMAT,
    ),
]


# ── Row 3: Rotations + failures (y=11) ────────────────────────────────
# Rotations are a heartbeat: ~1/hour/venue under default config. A drop
# in rotation rate is the second-strongest signal of a stuck writer
# (after last_write going stale).
# rotation_failures is the smoking gun — non-zero means the recorder
# crashed-and-restarted.

rotation_panels = [
    graph(
        "Rotations / min",
        [target('rate(bpt_tape_wslog_rotations_total{job="bpt-tape"}[5m]) * 60',
                '{{venue}}')],
        x=0, y=11, w=12, h=7,
    ),
    graph(
        # `or vector(0)` so the panel renders 0 instead of "no data" when
        # the counter has never incremented. The series goes away again
        # if/when an actual failure happens (the labeled series takes
        # over via PromQL's set semantics).
        "Rotation failures (5m)",
        [target(
            'increase(bpt_tape_wslog_rotation_failures_total{job="bpt-tape"}[5m]) '
            'or on() vector(0)',
            '{{venue}}/{{cause}}',
        )],
        x=12, y=11, w=12, h=7,
    ),
]


# ── Row 4: Disk on the recorder (y=18) ────────────────────────────────
# Pulled from node-exporter (filesystem collector). The recorder doesn't
# run node-exporter today; this row will be empty until #monitoring
# adds it. Keeping the panel here so the dashboard is complete the
# moment the data arrives.

disk_panels = [
    graph(
        "/opt/bpt/data free",
        [target(
            'node_filesystem_avail_bytes{mountpoint="/opt/bpt/data"}',
            '{{instance}}',
        )],
        x=0, y=18, w=12, h=7, unit=BYTES_FORMAT,
    ),
    graph(
        "/opt/bpt/data % used",
        [target(
            '100 * (1 - node_filesystem_avail_bytes{mountpoint="/opt/bpt/data"}'
            '         / node_filesystem_size_bytes{mountpoint="/opt/bpt/data"})',
            '{{instance}}',
        )],
        x=12, y=18, w=12, h=7,
    ),
]


# ── Dashboard ─────────────────────────────────────────────────────────

dashboard = Dashboard(
    title="BPT Tape",
    description=(
        "Recorder freshness, throughput, rotations, disk. Top row "
        "answers 'is tape capturing right now'; rest are evidence."
    ),
    tags=["bpt", "tape", "ops"],
    timezone="browser",
    refresh="10s",
    time=Time("now-30m", "now"),
    templating=Templating(list=[]),
    panels=(
        top_row
        + throughput_panels
        + rotation_panels
        + disk_panels
    ),
    uid="bpt-tape",
    version=1,
    schemaVersion=30,
).auto_panel_ids()
