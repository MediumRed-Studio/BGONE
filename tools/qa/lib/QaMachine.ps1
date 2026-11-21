# QaMachine.ps1 - machine telemetry, launch memory gate, sampler and BGONE run history.
# History path: %LOCALAPPDATA%\BGONE-QA\run-history.jsonl
# PowerShell 5.1 ONLY.

function Get-QaMachineConfig {
    param($Defaults)
    $mem = Get-QaProp -Object $Defaults -Name 'memory'
    return [pscustomobject]@{
        LaunchNeedMb   = [int](Get-QaProp -Object $mem -Name 'launchNeedMb'   -Default 5120)
        LaunchWaitSec  = [int](Get-QaProp -Object $mem -Name 'launchWaitSec'  -Default 120)
        StruggleFreeMb = [int](Get-QaProp -Object $mem -Name 'struggleFreeMb' -Default 700)
        StruggleCpuPct = [int](Get-QaProp -Object $mem -Name 'struggleCpuPct' -Default 97)
        StruggleFps    = [int](Get-QaProp -Object $mem -Name 'struggleFps'    -Default 8)
        SampleSec      = [int](Get-QaProp -Object $mem -Name 'sampleSec'      -Default 5)
    }
}

function Get-QaLaunchNeedMb {
    param($Config, [bool]$StartsServer = $true, [int]$Clients = 1)
    $perProcessMb = [int]($Config.LaunchNeedMb / 2)
    $processes = [math]::Max(0, $Clients)
    if ($StartsServer) { $processes++ }
    return [int]($perProcessMb * $processes)
}

function Wait-QaMachineReady {
    param([int]$NeedMb, [int]$WaitSec, [int]$PollSec = 15)
    $start = Get-Date
    $sample = Get-QaMemorySnapshot
    if ($null -eq $sample) { return [pscustomobject]@{ Ready = $true; Status = 'unreadable'; WaitedSec = 0; FreeMb = -1; NeedMb = $NeedMb; Note = ('memory unreadable; not gated (wanted ' + $NeedMb + ' MB)') } }
    $announced = $false
    while ($sample.FreeMb -lt $NeedMb) {
        $waited = [int]((Get-Date) - $start).TotalSeconds
        if ($waited -ge $WaitSec) {
            return [pscustomobject]@{ Ready = $false; Status = 'short'; WaitedSec = $waited; FreeMb = $sample.FreeMb; NeedMb = $NeedMb; Note = ('started anyway after ' + $waited + ' s with ' + $sample.FreeMb + ' MB free (wanted ' + $NeedMb + ' MB)') }
        }
        if (-not $announced) {
            Write-QaInfo ('Waiting for memory: ' + $sample.FreeMb + ' MB free, the game needs about ' + $NeedMb + ' MB (up to ' + $WaitSec + ' s).')
            $announced = $true
        }
        Start-Sleep -Seconds $PollSec
        $sample = Get-QaMemorySnapshot
        if ($null -eq $sample) {
            $lost = [int]((Get-Date) - $start).TotalSeconds
            return [pscustomobject]@{ Ready = $true; Status = 'unreadable'; WaitedSec = $lost; FreeMb = -1; NeedMb = $NeedMb; Note = ('memory became unreadable after ' + $lost + ' s; not gated (wanted ' + $NeedMb + ' MB)') }
        }
    }
    $total = [int]((Get-Date) - $start).TotalSeconds
    return [pscustomobject]@{ Ready = $true; Status = 'ready'; WaitedSec = $total; FreeMb = $sample.FreeMb; NeedMb = $NeedMb; Note = ('ready with ' + $sample.FreeMb + ' MB free (wanted ' + $NeedMb + ' MB) after waiting ' + $total + ' s') }
}

function Start-QaMachineSampler {
    param([string]$OutCsv, [int]$IntervalSec = 5, [string]$ProcessPrefix = 'ArmaReforgerS')
    $script = Join-Path (Split-Path -Parent $PSScriptRoot) 'machine-sampler.ps1'
    if (-not (Test-Path -LiteralPath $script)) { $script = Join-Path $PSScriptRoot '..\machine-sampler.ps1' }
    $stop = $OutCsv + '.stop'
    Remove-Item -LiteralPath $stop -Force -ErrorAction SilentlyContinue
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $script + '"'), '-OutCsv', ('"' + $OutCsv + '"'), '-IntervalSec', $IntervalSec, '-ParentPid', $PID, '-ProcessPrefix', $ProcessPrefix)
    try {
        $p = Start-Process -FilePath 'powershell.exe' -ArgumentList $argList -WindowStyle Hidden -PassThru
        return [pscustomobject]@{ Process = $p; Csv = $OutCsv; StopFile = $stop }
    } catch {
        return $null
    }
}

function Stop-QaMachineSampler {
    param($Sampler)
    if ($null -eq $Sampler) { return }
    try { [System.IO.File]::WriteAllText($Sampler.StopFile, 'stop') } catch { }
    try { if (-not $Sampler.Process.WaitForExit(15000)) { $Sampler.Process.Kill() } } catch { }
    Remove-Item -LiteralPath $Sampler.StopFile -Force -ErrorAction SilentlyContinue
}

function Read-QaMachineSamples {
    param([string]$Csv)
    $rows = New-Object System.Collections.ArrayList
    if (-not $Csv -or -not (Test-Path -LiteralPath $Csv)) { return @() }
    $inv = [System.Globalization.CultureInfo]::InvariantCulture
    foreach ($line in @(Read-QaLines -Path $Csv | Select-Object -Skip 1)) {
        $f = $line.Split(',')
        if ($f.Count -lt 4) { continue }
        try {
            [void]$rows.Add([pscustomobject]@{
                Time     = [datetime]::ParseExact($f[0], 'yyyy-MM-ddTHH:mm:ss', $inv)
                FreeMb   = [int]$f[1]
                CpuPct   = [double]::Parse($f[2], $inv)
                EngineMb = [int]$f[3]
            })
        } catch { }
    }
    return @($rows.ToArray())
}

function Read-QaFpsSamples {
    param([string]$LogPath, [datetime]$Day)
    $rows = New-Object System.Collections.ArrayList
    if (-not $LogPath -or -not (Test-Path -LiteralPath $LogPath)) { return @() }
    $inv = [System.Globalization.CultureInfo]::InvariantCulture
    $clock = New-QaLogClock -LogPath $LogPath -Fallback $Day
    foreach ($line in @(Read-QaLines -Path $LogPath)) {
        if ($line -match '^\s*(\d{2}:\d{2}:\d{2})\.\d+.*BGONE_MPBOT \| (?:TELEMETRY \| fps=|FPS \| )([0-9.]+)') {
            try {
                $t = Get-QaLogClockTime -Clock $clock -TimeOfDay $Matches[1]
                [void]$rows.Add([pscustomobject]@{ Time = $t; Fps = [double]::Parse($Matches[2], $inv) })
            } catch { }
        }
    }
    return @($rows.ToArray())
}

function Get-QaMachineStruggle {
    param($Samples, $FpsSamples, [datetime]$Around, [int]$WindowSec = 45, $Config)
    $from = $Around.AddSeconds(-$WindowSec)
    $to = $Around.AddSeconds(10)
    $reasons = New-Object System.Collections.ArrayList
    $near = @($Samples | Where-Object { $_.Time -ge $from -and $_.Time -le $to })
    if ($near.Count -gt 0) {
        $minFree = ($near | Measure-Object FreeMb -Minimum).Minimum
        if ($minFree -lt $Config.StruggleFreeMb) { [void]$reasons.Add('free memory fell to ' + $minFree + ' MB') }
        $busy = @($near | Where-Object { $_.CpuPct -ge $Config.StruggleCpuPct })
        if ($busy.Count -ge 3) { [void]$reasons.Add('the processor was flat out (' + $busy.Count + ' samples at ' + $Config.StruggleCpuPct + '% or more)') }
    }
    $fpsNear = @($FpsSamples | Where-Object { $_.Time -ge $from -and $_.Time -le $to })
    if ($fpsNear.Count -gt 0) {
        $minFps = ($fpsNear | Measure-Object Fps -Minimum).Minimum
        if ($minFps -lt $Config.StruggleFps) { [void]$reasons.Add('the game dropped to ' + [math]::Round($minFps, 1) + ' frames per second') }
    }
    if ($reasons.Count -eq 0) { return $null }
    return ('The computer was struggling: ' + ($reasons.ToArray() -join '; ') + '.')
}

function Format-QaMachineSummary {
    param($Samples, $FpsSamples)
    $parts = New-Object System.Collections.ArrayList
    if (@($Samples).Count -gt 0) {
        [void]$parts.Add('free memory ' + ($Samples | Measure-Object FreeMb -Minimum).Minimum + '-' + ($Samples | Measure-Object FreeMb -Maximum).Maximum + ' MB')
        [void]$parts.Add('CPU peak ' + [math]::Round(($Samples | Measure-Object CpuPct -Maximum).Maximum) + '%')
        [void]$parts.Add('game memory peak ' + ($Samples | Measure-Object EngineMb -Maximum).Maximum + ' MB')
    }
    if (@($FpsSamples).Count -gt 0) {
        [void]$parts.Add('frame rate ' + [math]::Round(($FpsSamples | Measure-Object Fps -Minimum).Minimum, 1) + '-' + [math]::Round(($FpsSamples | Measure-Object Fps -Maximum).Maximum, 1))
    }
    if ($parts.Count -eq 0) { return '(no samples)' }
    return ($parts.ToArray() -join ', ')
}

function Get-QaRunHistoryPath {
    $dir = Join-Path $env:LOCALAPPDATA 'BGONE-QA'
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    return (Join-Path $dir 'run-history.jsonl')
}

function Add-QaRunHistory {
    param([hashtable]$Record, [string]$Path = '')
    if (-not $Path) { $Path = Get-QaRunHistoryPath }
    $json = (New-Object PSObject -Property $Record) | ConvertTo-Json -Compress -Depth 4
    for ($try = 0; $try -lt 5; $try++) {
        try {
            $fs = New-Object System.IO.FileStream($Path, [System.IO.FileMode]::Append, [System.IO.FileAccess]::Write, [System.IO.FileShare]::ReadWrite)
            $w = New-Object System.IO.StreamWriter($fs, (New-Object System.Text.UTF8Encoding($false)))
            $w.WriteLine($json)
            $w.Dispose()
            return $true
        } catch {
            Start-Sleep -Milliseconds 300
        }
    }
    return $false
}

function Read-QaRunHistory {
    param([string]$Path = '')
    if (-not $Path) { $Path = Get-QaRunHistoryPath }
    if (-not (Test-Path -LiteralPath $Path)) { return @() }
    $out = New-Object System.Collections.ArrayList
    foreach ($line in @(Read-QaLines -Path $Path)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        try { [void]$out.Add(($line | ConvertFrom-Json)) } catch { }
    }
    return @($out.ToArray())
}
