<#
.SYNOPSIS
  Distribution of network interruptions from pingtrace events.csv.
  Clusters simultaneous per-target outages into episodes (one uplink drop =
  many target rows -> one interruption), then reports count, duration
  distribution and hour-of-day spread.

.EXAMPLE
  .\outage_stats.ps1
  .\outage_stats.ps1 -GapSec 20 -From "2026-06-30 00:00"
#>
param(
  [string]$Config = "config.json",
  [int]$GapSec = 15,          # outages starting within this gap = one episode
  [string]$From = "",         # optional local start "YYYY-MM-DD[ HH:MM]" or "<N>d/h"
  [string]$To = ""
)

$root = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$cfgPath = if ([IO.Path]::IsPathRooted($Config)) { $Config } else { Join-Path $root $Config }
$cfg = Get-Content $cfgPath -Raw | ConvertFrom-Json
$events = Join-Path $cfg.storage.dir "events.csv"
if (-not (Test-Path $events)) { Write-Host "no events.csv at $events"; return }

$now = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
function To-Ms([string]$s){
  if ([string]::IsNullOrWhiteSpace($s)) { return $null }
  if ($s -match '^\s*(\d+)\s*([dh])\s*$'){ $n=[long]$Matches[1]; if($Matches[2] -eq 'd'){return $now-$n*86400000}else{return $now-$n*3600000} }
  return [DateTimeOffset]::new([DateTime]::Parse($s)).ToUnixTimeMilliseconds()
}
$fromMs = To-Ms $From; $toMs = To-Ms $To

$rows = Import-Csv $events | Where-Object {
  ($null -eq $fromMs -or [long]$_.start_utc -ge $fromMs) -and
  ($null -eq $toMs   -or [long]$_.start_utc -le $toMs)
} | Sort-Object { [long]$_.start_utc }

if (-not $rows) { Write-Host "no events in range"; return }

# data span + raw counts
$first = [long]$rows[0].start_utc
$last  = [long]$rows[-1].start_utc
$byType = $rows | Group-Object type | ForEach-Object { "$($_.Name)=$($_.Count)" }
Write-Host ""
Write-Host ("Logs span : {0} .. {1} (local)" -f
  ([DateTimeOffset]::FromUnixTimeMilliseconds($first).LocalDateTime.ToString("yyyy-MM-dd HH:mm")),
  ([DateTimeOffset]::FromUnixTimeMilliseconds($last ).LocalDateTime.ToString("yyyy-MM-dd HH:mm")))
Write-Host ("Raw events: {0}  ({1})" -f $rows.Count, ($byType -join ', '))

# cluster into episodes by start-time proximity
$episodes = New-Object System.Collections.Generic.List[object]
$cur = $null
foreach ($e in $rows) {
  $s = [long]$e.start_utc; $en = [long]$e.end_utc
  if ($null -eq $cur -or ($s - $cur.lastStart) -gt ($GapSec*1000)) {
    if ($cur) { $episodes.Add($cur) }
    $cur = [pscustomobject]@{ start=$s; end=$en; lastStart=$s; targets=@{}; anyLocal=$false }
  } else {
    if ($en -gt $cur.end) { $cur.end = $en }
    $cur.lastStart = $s
  }
  $cur.targets[$e.target] = $true
  if ($e.type -eq 'local') { $cur.anyLocal = $true }
}
if ($cur) { $episodes.Add($cur) }

$durs = $episodes | ForEach-Object { [math]::Round((($_.end - $_.start)/1000.0),1) }
$sorted = $durs | Sort-Object
function Pctl($a,$p){ if(-not $a){return 0}; $i=[int][math]::Floor($p*($a.Count-1)); $a[$i] }

Write-Host ""
Write-Host ("Distinct interruptions (episodes, gap<=${GapSec}s): {0}" -f $episodes.Count)
$wide = ($episodes | Where-Object { $_.targets.Count -ge 5 }).Count
Write-Host ("  of them widespread (>=5 targets at once): {0}" -f $wide)
$sumS = ($durs | Measure-Object -Sum).Sum
Write-Host ("  total downtime: {0:N0} s (~{1:N1} min)   median {2}s  p90 {3}s  max {4}s" -f
  $sumS, ($sumS/60), (Pctl $sorted 0.5), (Pctl $sorted 0.9), ($sorted[-1]))

# duration buckets
Write-Host ""
Write-Host "Duration distribution:"
$buckets = [ordered]@{ "<5s"=0; "5-15s"=0; "15-30s"=0; "30-60s"=0; "1-5m"=0; ">5m"=0 }
foreach ($d in $durs) {
  if     ($d -lt 5)   { $buckets["<5s"]++ }
  elseif ($d -lt 15)  { $buckets["5-15s"]++ }
  elseif ($d -lt 30)  { $buckets["15-30s"]++ }
  elseif ($d -lt 60)  { $buckets["30-60s"]++ }
  elseif ($d -lt 300) { $buckets["1-5m"]++ }
  else                { $buckets[">5m"]++ }
}
foreach ($k in $buckets.Keys) {
  $n=$buckets[$k]; $bar = '#' * [int]([math]::Min(50, $n))
  "  {0,-7} {1,4}  {2}" -f $k, $n, $bar
}

# by hour of day (local)
Write-Host ""
Write-Host "By hour of day (local) - when interruptions start:"
$hours = New-Object int[] 24
foreach ($ep in $episodes) {
  $h = [DateTimeOffset]::FromUnixTimeMilliseconds($ep.start).LocalDateTime.Hour
  $hours[$h]++
}
$maxH = ($hours | Measure-Object -Maximum).Maximum
for ($h=0; $h -lt 24; $h++) {
  $n=$hours[$h]; $bar = '#' * [int]([math]::Round( ($(if($maxH){$n/$maxH}else{0})) * 40 ))
  "  {0:00}:00 {1,4}  {2}" -f $h, $n, $bar
}
Write-Host ""
