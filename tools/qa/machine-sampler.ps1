<#
.SYNOPSIS
    Write the computer's state to a CSV every few seconds while a BGONE QA run is in progress.

.DESCRIPTION
    Started hidden by mp-run.ps1 (Start-QaMachineSampler in lib/QaMachine.ps1). One row per sample:
    time, free memory (MB), processor use (%), memory held by the game's processes (MB). Stops when
    <OutCsv>.stop appears or the parent process exits, and after 3 hours at the latest.
    Reads counters only; touches no input. PowerShell 5.1 compatible.
#>
param(
    [Parameter(Mandatory)] [string]$OutCsv,
    [int]$IntervalSec = 5,
    [int]$ParentPid = 0,
    [string]$ProcessPrefix = 'ArmaReforgerS'
)

$ErrorActionPreference = 'SilentlyContinue'
$inv = [System.Globalization.CultureInfo]::InvariantCulture
$stop = $OutCsv + '.stop'
$deadline = (Get-Date).AddHours(3)
[System.IO.File]::WriteAllText($OutCsv, "time,freeMb,cpuPct,engineMb`r`n")

$cpu = New-Object System.Diagnostics.PerformanceCounter('Processor', '% Processor Time', '_Total')
[void]$cpu.NextValue()

while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $stop) { break }
    if ($ParentPid -gt 0 -and -not (Get-Process -Id $ParentPid -ErrorAction SilentlyContinue)) { break }
    Start-Sleep -Seconds $IntervalSec

    $freeMb = -1
    $os = Get-CimInstance Win32_OperatingSystem
    if ($os) { $freeMb = [int][math]::Round($os.FreePhysicalMemory / 1024.0) }
    $cpuPct = [math]::Round($cpu.NextValue(), 1)
    $engineMb = 0
    foreach ($p in @(Get-Process | Where-Object { $_.Name -like ($ProcessPrefix + '*') })) { $engineMb += [int]($p.WorkingSet64 / 1MB) }

    $row = (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss', $inv) + ',' + $freeMb + ',' + $cpuPct.ToString($inv) + ',' + $engineMb
    try { [System.IO.File]::AppendAllText($OutCsv, $row + "`r`n") } catch { }
}
