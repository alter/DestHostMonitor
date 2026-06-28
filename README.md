# pingtrace

Continuous reachability prober for Windows. It pings/connects to a list of
target hosts around the clock, writes a compact compressed binary journal,
localizes outages via a static hop ladder plus a one-shot traceroute fired on
each outage, and answers questions offline with its own subcommands.

## Build

Requires Visual Studio 2026 (or any MSVC + CMake + vcpkg). From a Developer
prompt at the repo root:

```
cmake --preset default
cmake --build --preset default
```

The preset points CMake at the VS-bundled vcpkg toolchain and pulls
`nlohmann-json` and `zstd`. The binary lands in `build/pingtrace.exe`.

## Quick start

```
copy config.example.json config.json   # edit targets/storage dir
build\pingtrace.exe run                 # Ctrl+C to stop
build\pingtrace.exe analyze --from 7d --by-hour --heatmap
```

## Subcommands

| Command | Purpose |
|---|---|
| `run [--config <path>]` | Continuous ICMP/TCP probing. `--duration <s>` for a bounded run. |
| `discover <dest> [--add]` | Traceroute to `<dest>`; with `--add` append the responding hops as ladder targets (`path_group`/`hop_index`). |
| `analyze [--from --to --target --by-hour --heatmap]` | Per-target summary, outage list, loss-by-hour-of-day, day×hour heatmap. |
| `export --from [--to] [--target] [--out f.csv]` | Dump raw records in a window to CSV. |
| `verify` | Seal dangling `.part` files and rebuild the segment index. |

Time arguments accept `YYYY-MM-DD[ HH:MM[:SS]]` (local, DST-aware), a relative
`<N>d|h|m` ("that long ago"), or `now`.

## Config (`config.example.json`)

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

## Data layout (under `storage.dir`)

- `seg_<startutc>.zst` — hourly sealed raw segments (12-byte records, zstd). `*.part` is the active hour.
- `index.csv` — segment index (start/end/file/count/targets) for fast seeks.
- `rollup/<YYYY-MM-DD>.csv` — per-minute summaries, kept forever.
- `events.csv` — detected outages (`host` / `local` / `monitor_gap`) with a `trace_ref`.
- `traces/<start>_<targetid>.txt` — traceroute captured at the moment of each outage.
- `addresses.csv` — id↔ip map for ICMP-error responders (`src_id` in raw records).

Times are stored as UTC milliseconds; local-time conversion (DST-aware) happens
only at analysis time.
