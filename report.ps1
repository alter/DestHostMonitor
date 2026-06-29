<#
.SYNOPSIS
  Build an ISP-ready evidence package from pingtrace data for a time window.

.EXAMPLE
  .\report.ps1 -From 24h
  .\report.ps1 -From "2026-06-28 00:00" -To "2026-06-28 12:00"

  Produces reports\evidence_<timestamp>\ with:
    report.txt        - summary + loss-by-hour + heatmap + path ladders
    events.csv        - outages in the window (id, time, duration, lost, type, trace)
    rollup\*.csv       - per-minute loss%/RTT time series for the window dates
    traces\*.txt       - traceroute captured at each in-window outage
    raw\*.csv          - packet-by-packet export around the longest outages
    README.txt        - what's inside + headline numbers
#>
param(
  [string]$From = "24h",
  [string]$To   = "now",
  [string]$Config = "config.json",
  [int]$RawOutages = 10,      # export raw window for the N longest outages
  [int]$RawPadSec  = 120      # seconds of context around each of those
)

# Note: do NOT set $ErrorActionPreference="Stop" — pingtrace logs to stderr, and
# in PS 5.1 native stderr under Stop is treated as a terminating error.
$root = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$exe  = Join-Path $root "build\pingtrace.exe"
$cfgPath = if ([IO.Path]::IsPathRooted($Config)) { $Config } else { Join-Path $root $Config }

if (-not (Test-Path $exe))     { throw "pingtrace.exe not found at $exe (build it first)" }
if (-not (Test-Path $cfgPath)) { throw "config not found at $cfgPath" }

$cfg = Get-Content $cfgPath -Raw | ConvertFrom-Json
$dataDir = $cfg.storage.dir

# --- resolve the window to absolute UTC ms -------------------------------------
$nowMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
function To-Ms([string]$s, [long]$nowMs) {
  if ($s -eq "now") { return $nowMs }
  if ($s -match '^\s*(\d+)\s*([dhm])\s*$') {
    $n = [long]$Matches[1]
    switch ($Matches[2]) {
      'd' { return $nowMs - $n*86400000 }
      'h' { return $nowMs - $n*3600000 }
      'm' { return $nowMs - $n*60000 }
    }
  }
  $dt = [DateTime]::Parse($s)              # interpreted as local time
  return [DateTimeOffset]::new($dt).ToUnixTimeMilliseconds()
}
function Ms-ToLocal([long]$ms) {
  [DateTimeOffset]::FromUnixTimeMilliseconds($ms).LocalDateTime.ToString("yyyy-MM-dd HH:mm:ss")
}

$fromMs = To-Ms $From $nowMs
$toMs   = To-Ms $To   $nowMs

# --- output folder -------------------------------------------------------------
$stamp = (Get-Date).ToString("yyyyMMdd_HHmmss")
$out = Join-Path $root ("reports\evidence_{0}" -f $stamp)
New-Item -ItemType Directory -Force -Path $out | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $out "rollup") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $out "traces") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $out "raw")    | Out-Null

Write-Host "Window : $(Ms-ToLocal $fromMs) .. $(Ms-ToLocal $toMs) (local)"
Write-Host "Output : $out"

# --- 1) human-readable report --------------------------------------------------
& $exe analyze --config $cfgPath --from $From --to $To --by-hour --heatmap --ladder 2>$null |
  Out-File -FilePath (Join-Path $out "report.txt") -Encoding utf8
Write-Host "  report.txt"

# --- 2) outages in the window --------------------------------------------------
$eventsSrc = Join-Path $dataDir "events.csv"
$outages = @()
if (Test-Path $eventsSrc) {
  $outages = Import-Csv $eventsSrc | Where-Object {
    [long]$_.start_utc -ge $fromMs -and [long]$_.start_utc -le $toMs
  }
  $outages | Export-Csv -Path (Join-Path $out "events.csv") -NoTypeInformation -Encoding utf8
  Write-Host "  events.csv ($($outages.Count) outages)"
} else {
  Write-Host "  (no events.csv yet)"
}

# --- 3) per-minute rollups for the window dates --------------------------------
$rollupSrc = Join-Path $dataDir "rollup"
if (Test-Path $rollupSrc) {
  $d = [DateTimeOffset]::FromUnixTimeMilliseconds($fromMs).UtcDateTime.Date
  $dEnd = [DateTimeOffset]::FromUnixTimeMilliseconds($toMs).UtcDateTime.Date
  while ($d -le $dEnd) {
    $f = Join-Path $rollupSrc ($d.ToString("yyyy-MM-dd") + ".csv")
    if (Test-Path $f) { Copy-Item $f (Join-Path $out "rollup") }
    $d = $d.AddDays(1)
  }
  Write-Host "  rollup\ (per-minute time series, UTC dates)"
}

# --- 4) traces + 5) raw export around the longest outages ----------------------
$top = $outages | Sort-Object { [double]$_.duration_s } -Descending | Select-Object -First $RawOutages
foreach ($o in $top) {
  if ($o.trace_ref) {
    $t = Join-Path $dataDir $o.trace_ref
    if (Test-Path $t) { Copy-Item $t (Join-Path $out "traces") }
  }
  $exFrom = Ms-ToLocal ([long]$o.start_utc - $RawPadSec*1000)
  $exTo   = Ms-ToLocal ([long]$o.end_utc   + $RawPadSec*1000)
  $name   = ("raw\{0}_{1}.csv" -f $o.event_id, ($o.target -replace '[^\w\-]','_'))
  & $exe export --config $cfgPath --from $exFrom --to $exTo --target $o.target --out (Join-Path $out $name) 2>$null
}
if ($top.Count -gt 0) { Write-Host "  traces\ + raw\ (top $($top.Count) longest outages)" }

# --- 6) README -----------------------------------------------------------------
$hostOut = ($outages | Where-Object { $_.type -eq 'host' }).Count
$localOut = ($outages | Where-Object { $_.type -eq 'local' }).Count
$gapOut = ($outages | Where-Object { $_.type -eq 'monitor_gap' }).Count
@"
pingtrace evidence package
Generated : $(Get-Date -Format "yyyy-MM-dd HH:mm:ss") (local)
Window    : $(Ms-ToLocal $fromMs) .. $(Ms-ToLocal $toMs) (local)

Outages in window: $($outages.Count)  (host=$hostOut, local=$localOut, monitor_gap=$gapOut)
  host        = a single target/path failing (the host or its route)
  local       = every target down at once = our side / the uplink, not the hosts
  monitor_gap = the monitoring PC slept or its clock jumped (ignore for ISP)

Files:
  report.txt   per-target loss/RTT summary, loss-by-hour-of-day, day x hour
               heatmap, and per-path hop ladders (where loss first climbs).
  events.csv   the outage log for this window (UTC ms + duration + type).
  rollup\      per-minute loss%/RTT time series (UTC dates) for graphing.
  traces\      traceroute captured at the moment of each (longest) outage.
  raw\         packet-by-packet probe records around the longest outages.

Notes for the ISP:
  - ICMP and TCP probes run side by side to the same hosts; TCP loss is
    data-plane loss (cannot be dismissed as ICMP deprioritization).
  - Probes are phase-spread across the interval (no synchronized bursts),
    so recorded loss is not self-induced.
  - The home gateway (192.168.31.1) is monitored as hop 0; loss appearing
    beyond it while it stays clean localizes the fault upstream of the LAN.
  All times are UTC milliseconds in the CSVs; report.txt is in local time.
"@ | Out-File -FilePath (Join-Path $out "README.txt") -Encoding utf8
Write-Host "  README.txt"

Write-Host ""
Write-Host "Done. Package: $out"
