# QaMemory.ps1 - free-RAM diagnostics for BGONE QA runners (PowerShell 5.1 ONLY).

function New-QaMemorySample {
    param([int]$FreeMb, [int]$TotalMb = 0)
    $pct = 0
    if ($TotalMb -gt 0) { $pct = [int][math]::Round((100.0 * $FreeMb) / $TotalMb) }
    return [pscustomobject]@{
        FreeMb  = $FreeMb
        TotalMb = $TotalMb
        FreePct = $pct
    }
}

function Get-QaMemorySnapshot {
    $os = $null
    try {
        $os = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
    } catch {
        return $null
    }
    if ($null -eq $os) { return $null }
    $freeMb  = [int][math]::Round($os.FreePhysicalMemory / 1024.0)
    $totalMb = [int][math]::Round($os.TotalVisibleMemorySize / 1024.0)
    return (New-QaMemorySample -FreeMb $freeMb -TotalMb $totalMb)
}

function Format-QaMemoryMb {
    param([int]$Mb)
    if ($Mb -lt 1024) { return ([string]$Mb + ' MB') }
    $gb = [math]::Round($Mb / 1024.0, 1)
    return ([string]$gb + ' GB')
}

function Format-QaMemorySample {
    param($Sample)
    if ($null -eq $Sample) { return '(unavailable)' }
    $free = Format-QaMemoryMb -Mb $Sample.FreeMb
    if ($Sample.TotalMb -le 0) { return ($free + ' free') }
    $total = Format-QaMemoryMb -Mb $Sample.TotalMb
    return ($free + ' free of ' + $total + ' (' + [string]$Sample.FreePct + '%)')
}

function Format-QaMemoryDelta {
    param($Start, $End)
    if (($null -eq $Start) -and ($null -eq $End)) { return '(unavailable)' }
    if ($null -eq $Start) { return ('? -> ' + (Format-QaMemoryMb -Mb $End.FreeMb) + ' free') }
    if ($null -eq $End)   { return ((Format-QaMemoryMb -Mb $Start.FreeMb) + ' free -> ?') }

    $deltaMb = $End.FreeMb - $Start.FreeMb
    $sign = '+'
    if ($deltaMb -lt 0) { $sign = '-' }
    $magnitude = Format-QaMemoryMb -Mb ([math]::Abs($deltaMb))
    return ((Format-QaMemoryMb -Mb $Start.FreeMb) + ' -> ' + (Format-QaMemoryMb -Mb $End.FreeMb) +
            ' free (' + $sign + $magnitude + ' across the run)')
}

function Get-QaMemoryConfig {
    param($Defaults)
    $mem = Get-QaProp -Object $Defaults -Name 'memory'
    return [pscustomobject]@{
        LowFreeWarnMb     = [int](Get-QaProp -Object $mem -Name 'lowFreeWarnMb'       -Default 6144)
        SettleTimeoutSec  = [int](Get-QaProp -Object $mem -Name 'settleTimeoutSec'    -Default 120)
        SettlePollSec     = [int](Get-QaProp -Object $mem -Name 'settlePollSec'       -Default 5)
        SettleToleranceMb = [int](Get-QaProp -Object $mem -Name 'settleToleranceMb'   -Default 256)
        SettleSamples     = [int](Get-QaProp -Object $mem -Name 'settleStableSamples' -Default 3)
    }
}

function Test-QaMemoryLow {
    param([int]$FreeMb, [int]$ThresholdMb)
    if ($ThresholdMb -le 0) { return $false }
    return ($FreeMb -lt $ThresholdMb)
}

function Get-QaLowMemoryWarningLines {
    param($Sample, [int]$ThresholdMb)
    if ($null -eq $Sample) { return @() }
    if (-not (Test-QaMemoryLow -FreeMb $Sample.FreeMb -ThresholdMb $ThresholdMb)) { return @() }

    $lines = New-Object System.Collections.ArrayList
    [void]$lines.Add('LOW FREE RAM at launch: ' + (Format-QaMemoryMb -Mb $Sample.FreeMb) +
                     ' free, below the ' + (Format-QaMemoryMb -Mb $ThresholdMb) + ' warning line.')
    [void]$lines.Add('A FAILURE from this run is LOW CONFIDENCE under memory pressure. Nothing is blocked.')
    return @($lines.ToArray())
}

function Test-QaMemoryStabilised {
    param([int[]]$SamplesMb, [int]$ToleranceMb = 256, [int]$StableSamples = 3)

    if ($StableSamples -lt 2) { $StableSamples = 2 }
    $s = @($SamplesMb)
    if ($s.Count -lt $StableSamples) { return $false }

    $startIndex = $s.Count - $StableSamples
    $first = $s[$startIndex]
    for ($i = $startIndex; $i -lt $s.Count; $i++) {
        if (($s[$i] - $first) -gt $ToleranceMb) { return $false }
    }
    return $true
}

function Wait-QaMemorySettle {
    param(
        [int]$TimeoutSec = 120,
        [int]$PollSec = 5,
        [int]$ToleranceMb = 256,
        [int]$StableSamples = 3
    )
    if ($PollSec -lt 1) { $PollSec = 1 }

    $samples = New-Object System.Collections.ArrayList
    $started = Get-Date
    $deadline = $started.AddSeconds($TimeoutSec)
    $stabilised = $false
    $available = $true

    while ($true) {
        $snap = Get-QaMemorySnapshot
        if ($null -eq $snap) { $available = $false; break }
        [void]$samples.Add([int]$snap.FreeMb)

        if (Test-QaMemoryStabilised -SamplesMb @($samples.ToArray()) -ToleranceMb $ToleranceMb -StableSamples $StableSamples) {
            $stabilised = $true
            break
        }
        if ((Get-Date) -ge $deadline) { break }
        Start-Sleep -Seconds $PollSec
    }

    $arr = @($samples.ToArray())
    $firstMb = -1
    $lastMb = -1
    if ($arr.Count -gt 0) {
        $firstMb = $arr[0]
        $lastMb = $arr[$arr.Count - 1]
    }
    return [pscustomobject]@{
        Stabilised = $stabilised
        Available  = $available
        Samples    = $arr
        FirstMb    = $firstMb
        LastMb     = $lastMb
        WaitedSec  = [int]((Get-Date) - $started).TotalSeconds
    }
}

function Wait-QaMpMemorySettle {
    param($MemConfig)
    if ($null -eq $MemConfig) { return $null }
    if ($MemConfig.SettleTimeoutSec -le 0) { return $null }

    $settle = Wait-QaMemorySettle -TimeoutSec $MemConfig.SettleTimeoutSec -PollSec $MemConfig.SettlePollSec `
        -ToleranceMb $MemConfig.SettleToleranceMb -StableSamples $MemConfig.SettleSamples
    if (-not $settle.Available) {
        Write-QaInfo 'Memory settle: no free-RAM counter on this machine; continuing.'
        return $settle
    }
    $where = Format-QaMemoryMb -Mb $settle.LastMb
    if ($settle.Stabilised) {
        Write-QaInfo ('Memory settle: free RAM stabilised at ' + $where + ' after ' + $settle.WaitedSec + 's.')
    } else {
        Write-QaWarn ('Memory settle: free RAM still moving after ' + $settle.WaitedSec + 's (now ' + $where + '); starting anyway.')
    }
    return $settle
}
