# pingtrace

<img width="1126" height="907" alt="image" src="https://github.com/user-attachments/assets/9bedce8c-016d-490b-8f22-12d4281f2352" />



Continuous reachability prober for Windows. It probes a list of hosts (ICMP or
TCP) around the clock, shows a live console dashboard, writes a compact
zstd-compressed binary journal, detects outages, and localizes *where* the path
breaks via a hop ladder plus a one-shot traceroute fired the moment an outage
starts. All analysis is offline, over any past window, through its own
subcommands.

It is built for the "is it me, my ISP, or the far host?" question — e.g. keeping
an eye on an exchange API, a VPN tunnel, or a remote server, and being able to
say afterwards *when* it broke, *how long*, *how often*, and *at which hop*.

## Highlights

- **ICMP and TCP probing**, per-target interval/timeout, no admin rights needed.
- **Live dashboard** that redraws in place with a sliding window of the last 10
  probes per target: loss and RTT min/mean/max/last, losing rows flagged in red.
- **Compact storage**: 12-byte raw records, hourly segments sealed with zstd
  (~3x), plus per-minute rollups kept forever. ~10 hosts ≈ tens of MB/year.
- **Outage detection** with classification: `host` (one target), `local` (all
  targets down at once = your side), `monitor_gap` (the machine slept / clock
  jumped).
- **Localization**: `discover` builds a hop ladder; every outage triggers a
  one-shot traceroute saved next to the event.
- **Offline reports**: per-target summary, loss-by-hour-of-day, day×hour heatmap,
  outage list, and raw CSV export for Excel / pandas / Grafana.
- **Crash-safe**: a kill mid-write leaves at most ~1.5s of data unflushed; a
  dangling segment is sealed automatically on the next start.

## Requirements

- Windows 10/11.
- Visual Studio 2022/2026 (the bundled CMake + Ninja + vcpkg are enough — no
  separate installs). Any MSVC + CMake + vcpkg toolchain also works.

## Build

**Easiest — `build.cmd`:** double-click it, or run it from any terminal. It
finds Visual Studio via `vswhere`, sets up the MSVC environment, and builds with
the bundled CMake/Ninja/vcpkg. The result is `build\pingtrace.exe` (with
`zstd.dll` copied next to it).

**Visual Studio:** `File → Open → Folder…` → the repo. VS reads `CMakeLists.txt`
+ `CMakePresets.json`, configures automatically, then `Build → Build All`.

**Command line** (from a *Developer* PowerShell/Command Prompt for VS):

```
cmake --preset default
cmake --build --preset default
```

Dependencies (`nlohmann-json`, `zstd`) are pulled by vcpkg on the first build.

## Configuration

`run`, `analyze`, `export`, `verify`, and `discover` read a JSON config
(default `config.json`; override with `--config <path>`). Start from
`config.example.json`:

```json
{
  "storage": { "dir": "C:/pingtrace/data", "segment_minutes": 60, "raw_retention_days": 14 },
  "defaults": { "interval_ms": 1000, "timeout_ms": 1500 },
  "events": { "fail_threshold": 3, "min_outage_ms": 2000, "trigger_traceroute": true },
  "targets": [
    { "name": "cloudflare", "address": "1.1.1.1", "proto": "icmp" },
    { "name": "bybit", "address": "api.bybit.com", "proto": "tcp", "port": 443 },
    { "name": "isp-hop1", "address": "10.0.0.1", "proto": "icmp", "path_group": "bybit", "hop_index": 1 }
  ]
}
```

| Section | Field | Meaning |
|---|---|---|
| `storage` | `dir` | Where data is written (created if missing). |
| | `segment_minutes` | Raw segment length before sealing (default 60). |
| | `raw_retention_days` | Delete sealed raw segments older than this (default 14). Rollups/events/traces are kept forever. |
| `defaults` | `interval_ms` / `timeout_ms` | Defaults applied to targets that don't set their own. |
| `events` | `fail_threshold` | Consecutive failures before an outage opens (default 3). |
| | `min_outage_ms` | Minimum duration to actually log an outage (filters blips). |
| | `trigger_traceroute` | Run a traceroute when an outage opens. |
| `targets[]` | `name` | Label shown everywhere (defaults to `address`). |
| | `address` | Hostname or IPv4. |
| | `proto` | `icmp` or `tcp`. |
| | `port` | Required for `tcp`. |
| | `interval_ms` / `timeout_ms` | Optional per-target overrides. |
| | `path_group` / `hop_index` | Place a target on a named path's ladder at a given hop (see `analyze --ladder`). `path_group: "*"` puts it in **every** ladder — e.g. the gateway as the shared hop 0. |
| | `group` | Dashboard section this target is shown under (e.g. `hunt`, `tcp`, `icmp`). Falls back to `path_group` when unset, so ladder hops group by their path. Affects the live view only, not `analyze --ladder`. |
| | `probe` | `false` = ladder label only: never probed (no false 100% loss for hops that ignore ping), but still shown as a `trace` rung and captured by trace-on-event. Default `true`. |

TCP probes connect and measure time to SYN-ACK (open) or RST (closed) — both
mean the host responded. Useful when routers deprioritize ICMP: probe the real
service port instead.

## Quick start

```
copy config.example.json config.json      :: then edit targets + storage dir
build\pingtrace.exe run                    :: live dashboard; Ctrl+C to stop
build\pingtrace.exe analyze --from 7d --by-hour --heatmap
```

## `run` — continuous probing

```
pingtrace run [--config <path>] [--duration <seconds>]
```

In an interactive terminal you get a **live full-screen dashboard** (alternate
screen buffer, like `top`) — bordered tables that redraw in place, with targets
grouped under cyan `- <group>` label rows and losing rows in red:

```
pingtrace live   2026-06-30 16:08:22 UTC   uptime 02:21:29   probes 76407   (rtt ms, Ctrl+C to stop)
┌─────────────┬────────┬──────┬───────────┬──────┬───────┬───────┬───────┬───────┐
│target       │loss(10)│win%  │total      │tot%  │min    │mean   │max    │last   │
├─────────────┼────────┼──────┼───────────┼──────┼───────┼───────┼───────┼───────┤
│─ root       │        │      │           │      │       │       │       │       │
│home-router  │0/10    │0.0%  │151/76407  │0.20% │1.0    │4.0    │71.0   │3.0    │
│─ trunk      │        │      │           │      │       │       │       │       │
│trunk-h1-gw  │0/10    │0.0%  │92/76401   │0.12% │2.0    │5.5    │23.0   │7.0    │
│─ icmp       │        │      │           │      │       │       │       │       │
│cloudflare   │0/10    │0.0%  │1129/76407 │1.48% │52.0   │58.3   │152.0  │60.0   │
│hk           │3/10    │30.0% │1476/76407 │1.93% │234.0  │256.6  │466.0  │237.0  │
└─────────────┴────────┴──────┴───────────┴──────┴───────┴───────┴───────┴───────┘
```

(the `hk` row is red; the `- root` / `- trunk` / `- icmp` label rows are cyan)

- `loss(10)` / `win%` — failures over a **sliding window of the last 10 probes**
  per target (1‑10, then 2‑11, …) and that window's loss %.
- `total` / `tot%` — totals since the process started.
- `min / mean / max / last` — RTT (ms) over the same 10‑probe window. A failed
  last probe shows its status (`TIMEOUT`, `UNREACH`, …); no successful samples in
  the window → `-`.
- Rows with loss in the window are shown in red. (The piped/plain view has no
  colour, so there it marks them `<- loss` instead.)

Targets are stacked under cyan `- <group>` label rows (each target's `group`,
else its `path_group`). The table **stretches to fill the terminal width**, and
on a **wide terminal** (≥150 cols) the groups split across **two tables side by
side** — each filling half — so the screen is used and the list is half as tall;
a narrow terminal gets one full-width table. Set `PINGTRACE_COLS=<n>` to override
the detected width (force one/two columns, or fix wrong detection when piped).

The alt-buffer view survives mouse clicks and scrolling, and on `Ctrl+C` it
restores your previous screen (all history is on disk). When stdout is **piped
or redirected** (a file, a service) `run` falls back to clean append-only blocks
with no escape codes — good for logs. Force the dashboard over a pipe with
`PINGTRACE_TTY=1`.

Flags:

- `--duration <seconds>` — run for N seconds then stop cleanly (handy for tests).
- `--config <path>` — config file (default `config.json`).

Stopping with `Ctrl+C` flushes and seals the current segment; nothing is lost.

## `discover` — build a hop ladder

```
pingtrace discover <dest> [--add] [--config <path>]
```

Runs one traceroute to `<dest>` and prints the hops:

```
traceroute to 8.8.8.8 (8.8.8.8):
   1  192.168.31.1     3.0ms
   2  10.200.210.1     56.0ms
   3  10.0.0.1         55.0ms
   ...
  10  8.8.8.8          67.0ms  (dest)
```

With `--add`, each responding hop is appended to the config as an ICMP target
tagged `path_group=<dest>`, `hop_index=N`. Those rungs are then probed like any
other target, so you continuously see **how far the path gets and where it
stalls** — far more reliable than reading hops from an occasional traceroute.

## `analyze` — offline reports

```
pingtrace analyze [--from <t>] [--to <t>] [--target <name|id>] [--by-hour] [--heatmap] [--ladder] [--config <path>]
```

Defaults to the last 30 days. Always prints a per-target summary and the outage
list; add `--by-hour` for a loss-by-hour-of-day histogram (catches "only in the
evening" problems), `--heatmap` for a day-of-week × hour grid, and `--ladder`
for per-path hop-by-hop loss/RTT.

```
target               sent    lost   loss%   rtt_min   rtt_avg   rtt_max
----------------------------------------------------------------------
cloudflare            900       3   0.33%      52.0      55.9      61.0
hk                    900      71   7.89%     234.0     256.0     466.0

outages:
  #12   hk    2026-06-28 19:18:27  dur=74.0s lost=75 type=host  trace=traces/1782661107003_6.txt
```

With `--ladder`, targets that have a `path_group` are walked in `hop_index`
order so you can see **at which hop loss first climbs**. Hops marked
`probe: false` show as `trace`; the gateway (`path_group: "*"`) heads every
ladder as hop 0:

```
path: trunk
  hop target                  loss%   rtt_avg
  0   home-router             0.22%       4.1
  1   trunk-h1-gw             trace         -
  2   trunk-h2                trace         -
  3   trunk-h3-isp            0.88%       7.9
  4   trunk-h4-isp            0.74%      51.6
  5   trunk-h5-comcor-a       2.19%     239.5  <- loss climbs here
  5   trunk-h5-comcor-b       0.76%      13.2
```

Built from the per-minute rollups (fast, kept forever) and `events.csv`.

## `export` — raw window to CSV

```
pingtrace export --from <t> [--to <t>] [--target <name|id>] [--out <file.csv>] [--config <path>]
```

One row per probe attempt. Writes to `--out` or stdout.

```
utc_iso,utc_ms,target_id,target,status,rtt_ms,reply_ttl,src_ip
2026-06-28 16:18:27.003,1782663507003,6,hk,OK,235.0,52,
2026-06-28 16:18:28.005,1782663508005,6,hk,TIMEOUT,,0,
```

`src_ip` is the router that returned an ICMP error (Time-Exceeded /
Unreachable) — free localization of where a packet died.

## `verify` — repair storage

```
pingtrace verify [--config <path>]
```

Seals any leftover `.part` files (e.g. after a hard crash) and rebuilds
`index.csv` by scanning segment headers.

## Time arguments

`--from` / `--to` accept:

- absolute **local** time `YYYY-MM-DD` or `YYYY-MM-DD HH:MM[:SS]` (DST-aware),
- relative `<N>d` / `<N>h` / `<N>m` — that long ago,
- `now`.

Example: `--from "2026-06-20 09:00" --to 1h`.

## How outages are detected

A target's outage **opens** after `fail_threshold` consecutive failures
(back-dated to the first one) and **closes** on the next success; it is only
logged if it lasted at least `min_outage_ms`. The `type` column tells you who is
at fault:

- `host` — only this target is failing → the host or its path.
- `local` — every target is failing at once → your channel / monitor, not the hosts.
- `monitor_gap` — the machine slept or the clock jumped; a synthetic marker so a
  gap isn't mistaken for an outage.

When `trigger_traceroute` is on, opening an outage saves a traceroute to
`traces/<start>_<targetid>.txt`, referenced from the event row.

## Data layout (under `storage.dir`)

| File | Contents |
|---|---|
| `seg_<startutc>.zst` | Sealed hourly raw segment (12-byte records, zstd). |
| `seg_<startutc>.part` | The active (current-hour) segment. |
| `index.csv` | `start,end,file,record_count,targets` — for fast seeks. |
| `rollup/<YYYY-MM-DD>.csv` | Per-minute `sent,lost,loss%,rtt_min/avg/p50/p95/max`. Kept forever. |
| `events.csv` | Outages: id, target, start/end, duration, lost, type, trace_ref. |
| `traces/<start>_<targetid>.txt` | Traceroute captured when an outage opened. |
| `addresses.csv` | id↔ip map for ICMP-error responders (`src_id`). |

Status codes: `OK · TIMEOUT · DEST_UNREACH · TTL_EXPIRED · OTHER_ICMP_ERR ·
SEND_ERR · MONITOR_GAP`.

Times are stored as **UTC milliseconds**; local-time conversion (DST-aware)
happens only at analysis time, so day/night histograms stay correct across DST.

## Notes & troubleshooting

- **No admin rights** are needed (`IcmpSendEcho2`).
- Keep **`zstd.dll` next to `pingtrace.exe`** (the build copies it). If you move
  the exe, move the dll too.
- The dashboard **disables QuickEdit** while running so a mouse click can't
  freeze output; it's restored on `Ctrl+C`. To copy text from the window, stop
  the run (history is on disk) — or pipe output to a file.
- Running as an **always-on service / scheduled task**: redirect stdout to a
  log file (you'll get the clean append-mode output) and point `storage.dir` at
  a persistent location.
- Sample volume: 10 hosts × 1 Hz ≈ 8 MB/day of raw before zstd, a few hundred
  MB/year after; rollups are tiny and compress to nothing.
