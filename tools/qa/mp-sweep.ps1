<#
.SYNOPSIS
    Runs multiple BGONE mp-run.ps1 scenarios sequentially and reports one summary table.

.DESCRIPTION
    Supports -OneServer grouping (reusing one dedicated server across non-conflicting scenarios),
    optional background -Record, and one retry on setup/machine-struggle failures.
    PowerShell 5.1 ONLY: no &&/||, no ternary, no null-coalescing.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$WorktreePath,

    [Parameter(Mandatory = $true)]
    [string[]]$Scenarios,

    [string]$Engine = '',
    [switch]$DiscardRdb,
    [switch]$KeepProfile,

    [int]$Clients = 0,
    [int]$ServerReadyTimeoutSec = 0,
    [int]$ScenarioTimeoutSec = 0,
    [int]$LoadTimeoutSec = 0,
    [string]$ConfigPath,

    [int]$IdleTimeoutSec = 600,
    [switch]$StopOnFirstFailure,

    [switch]$Record,
    [int[]]$RecordClients = @(1),
    [switch]$Hand,
    [switch]$OneServer,
    [int]$RetryOnSetupFailure = 1
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'lib/QaCommon.ps1')
. (Join-Path $here 'lib/QaProcess.ps1')
. (Join-Path $here 'lib/QaConfig.ps1')
. (Join-Path $here 'lib/QaMemory.ps1')
. (Join-Path $here 'lib/QaLogParse.ps1')
. (Join-Path $here 'lib/QaSweep.ps1')

if ($Hand) {
    Write-QaError '-Hand is not supported: BGONE QA is strictly headless (no real mouse or foreground focus).'
    exit 2
}

$sweepConfigPath = $ConfigPath
if ([string]::IsNullOrWhiteSpace($sweepConfigPath)) { $sweepConfigPath = Join-Path $here 'engines.json' }
$memConfig = $null
$sweepConfig = $null
try {
    $sweepConfig = Get-QaConfig -Path $sweepConfigPath
    $memConfig = Get-QaMemoryConfig -Defaults (Get-QaProp -Object $sweepConfig -Name 'defaults')
    if ([string]::IsNullOrWhiteSpace($Engine)) { $Engine = Get-QaTargetEngine -Defaults (Get-QaProp -Object $sweepConfig -Name 'defaults') }
} catch {
    Write-QaWarn ('Could not read ' + $sweepConfigPath + '; using defaults.')
    $memConfig = Get-QaMemoryConfig -Defaults $null
    if ([string]::IsNullOrWhiteSpace($Engine)) { $Engine = 'stable' }
}

$sweepWorld = 'Worlds/MPTest/MPTest.ent'
$sweepMission = 'Missions/BGONE_MPTest.conf'
if ($null -ne $sweepConfig) {
    try {
        $sweepMp = Get-QaRigConfig -Config $sweepConfig -Engine $Engine -Section 'mp'
        $sweepWorld = [string](Get-QaProp -Object $sweepMp -Name 'worldPath' -Default $sweepWorld)
        $sweepMission = [string](Get-QaProp -Object $sweepMp -Name 'missionHeaderPath' -Default $sweepMission)
    } catch { }
}

$mpRun = Join-Path $here 'mp-run.ps1'
if (-not (Test-Path -LiteralPath $mpRun)) {
    Write-QaError ('mp-run.ps1 not found next to this script: ' + $mpRun)
    exit 2
}

Write-Host ''
Write-QaStep ('BGONE MP sweep: ' + @($Scenarios).Count + ' scenario(s), sequential')
Write-QaInfo ('Worktree   : ' + $WorktreePath)
Write-QaInfo ('Scenarios  : ' + (@($Scenarios) -join ', '))
Write-QaInfo ('Free RAM   : ' + (Format-QaMemorySample -Sample (Get-QaMemorySnapshot)))

$results = New-Object System.Collections.ArrayList
$exitCodes = Get-QaExitCodes
$sharedStatePath = Join-Path (Join-Path (Join-Path $WorktreePath '.qa') 'runs') 'shared-server.json'
$keepOwner = [guid]::NewGuid().ToString('N')
$runDirFile = Join-Path ([System.IO.Path]::GetTempPath()) ('bgone-sweep-' + $keepOwner + '-rundir.txt')

function Get-SweepLastRunInfo {
    $info = [pscustomobject]@{ Reason = ''; Verdict = ''; BotArmed = $false; Struggle = '' }
    if (-not (Test-Path -LiteralPath $runDirFile)) { return $info }
    $dir = ([System.IO.File]::ReadAllText($runDirFile)).Trim()
    if (-not $dir) { return $info }
    $summary = Join-Path $dir 'summary.txt'
    if (-not (Test-Path -LiteralPath $summary)) { return $info }
    foreach ($line in [System.IO.File]::ReadLines($summary)) {
        if ($line -match '^\s*Reason\s*:\s*(?<v>.*)$') { $info.Reason = $Matches['v'] }
        elseif ($line -match '^\s*C1Verdict\s*:\s*(?<v>.*)$') { $info.Verdict = $Matches['v'] }
        elseif ($line -match '^\s*C1Bot\s*:\s*(?<v>.*)$') { $info.BotArmed = ($Matches['v'].Trim() -ne '(never)') }
        elseif ($line -match '^\s*MachineStruggle\s*:\s*(?<v>.*)$') { if ($Matches['v'].Trim() -ne 'no') { $info.Struggle = $Matches['v'].Trim() } }
    }
    return $info
}

function Get-KeptServer {
    $s = Get-QaOwnedKeptServer -StatePath $sharedStatePath -Owner $keepOwner
    if ($null -eq $s) { return $null }
    if (-not (Test-QaKeptServerReusable -Record $s)) { return $null }
    $now = Get-QaGitStamp -WorktreePath $WorktreePath
    if (-not (Test-QaKeptServerCodeMatch -Record $s -Commit $now.Commit -Dirty @($now.Dirty) -WorldPath $sweepWorld -MissionHeader $sweepMission -Engine $Engine)) { return $null }
    return $s
}

function Stop-KeptServer {
    $s = Get-QaOwnedKeptServer -StatePath $sharedStatePath -Owner $keepOwner -IncludeDead
    if ($null -eq $s) { return $true }
    $stop = Stop-QaVerifiedWorkbench -Pids @($s.serverPids) -Identities @($s.serverPidIdentities) -GraceSec 10
    if (@($stop.Stopped).Count -gt 0) {
        Write-QaInfo ('Stopping kept server (PID ' + (@($stop.Stopped) -join ', ') + ')')
        Start-Sleep -Seconds 2
    }
    $retired = -not (Test-QaKeptServerReusable -Record $s)
    if (Test-QaMpPortFree -Port 2001) {
        $released = Close-QaRigAdmission
        if ($released.Released -or $released.Reason -eq 'absent') {
            Remove-Item -LiteralPath $sharedStatePath -Force -ErrorAction SilentlyContinue
        } else {
            Write-QaWarn ('Rig admission could not be released (' + $released.Reason + ').')
            $retired = $false
        }
    } else {
        [void](Set-QaMpRigLockRetained -Why 'port')
        Write-QaWarn 'UDP 2001 still held after kept server stopped.'
        $retired = $false
    }
    return $retired
}

$items = @($Scenarios | ForEach-Object { Split-SweepScenario -Token $_ })
$plan = New-Object System.Collections.ArrayList
if ($OneServer) {
    $touches = Get-QaProp -Object (Get-QaRigConfig -Config $sweepConfig -Engine $Engine -Section 'mp') -Name 'sessionTouches'
    $groups = @(Get-SweepGroups -Items $items -Touches $touches)
    $n = 0
    foreach ($g in $groups) {
        $n++
        Write-QaInfo ('Server group ' + $n + ': ' + (($g.Items | ForEach-Object { $_.Token }) -join ', '))
        $pos = 0
        foreach ($item in $g.Items) {
            $pos++
            [void]$plan.Add([pscustomobject]@{ Item = $item; Group = $n; First = ($pos -eq 1); Last = ($pos -eq $g.Items.Count) })
        }
    }
} else {
    foreach ($item in $items) { [void]$plan.Add([pscustomobject]@{ Item = $item; Group = 0; First = $true; Last = $true }) }
}

$index = 0
try {
foreach ($step in $plan.ToArray()) {
    $index++
    $scenario = $step.Item.Name
    Write-Host ''
    Write-QaStep ('[' + $index + '/' + $plan.Count + '] ' + $step.Item.Token)

    $serverUp = $OneServer -and ($null -ne (Get-KeptServer))
    if ($serverUp -and (-not $step.First)) {
        Start-Sleep -Seconds 5
    } else {
        if (-not (Wait-QaMpIdle -WorktreePath $WorktreePath -TimeoutSec $IdleTimeoutSec)) {
            Write-QaWarn ('Engine processes still alive after ' + $IdleTimeoutSec + 's.')
        }
        [void](Wait-QaMpMemorySettle -MemConfig $memConfig)
    }

    $memBefore = Get-QaMemorySnapshot
    Write-QaInfo ('Free RAM   : ' + (Format-QaMemorySample -Sample $memBefore))

    $mpArgs = @{
        WorktreePath = $WorktreePath
        Scenario     = $scenario
        Engine       = $Engine
        Queue        = $true
    }
    if ($OneServer) { $mpArgs['KeepOwner'] = $keepOwner }
    if ($DiscardRdb)  { $mpArgs['DiscardRdb']  = $true }
    if ($KeepProfile) { $mpArgs['KeepProfile'] = $true }
    $runClients = $Clients
    if ($step.Item.Clients -gt 0) { $runClients = $step.Item.Clients }
    if ($runClients -gt 0)            { $mpArgs['Clients']               = $runClients }
    if ($ServerReadyTimeoutSec -gt 0) { $mpArgs['ServerReadyTimeoutSec'] = $ServerReadyTimeoutSec }
    if ($ScenarioTimeoutSec -gt 0)    { $mpArgs['ScenarioTimeoutSec']    = $ScenarioTimeoutSec }
    if ($LoadTimeoutSec -gt 0)        { $mpArgs['LoadTimeoutSec']        = $LoadTimeoutSec }
    if (-not [string]::IsNullOrWhiteSpace($ConfigPath)) { $mpArgs['ConfigPath'] = $ConfigPath }
    if ($Record) {
        $mpArgs['Record'] = $true
        $rc = @($RecordClients)
        if ($runClients -gt 1 -and $rc.Count -eq 1 -and $rc[0] -eq 1) { $rc = 1..$runClients }
        $mpArgs['RecordClients'] = $rc
    }
    if ($OneServer) {
        if ($serverUp -and (-not $step.First)) {
            $mpArgs['ReuseServer'] = $true
            if ($step.Last) { $mpArgs['StopServer'] = $true } else { $mpArgs['KeepServer'] = $true }
        } elseif (-not $step.Last) {
            $mpArgs['KeepServer'] = $true
        }
    }

    $started = Get-Date
    $code = 1
    $retryNote = ''
    $retryRefused = $false
    $restartWorld = $false
    for ($attempt = 1; $attempt -le (1 + $RetryOnSetupFailure); $attempt++) {
        Remove-Item -LiteralPath $runDirFile -Force -ErrorAction SilentlyContinue
        $mpArgs['RunDirFile'] = $runDirFile
        & $mpRun @mpArgs
        $code = $LASTEXITCODE
        if ($code -eq 0) { break }
        if ($attempt -gt $RetryOnSetupFailure) { break }

        $last = Get-SweepLastRunInfo
        if ($last.Struggle) {
            Write-QaWarn ('Attempt ' + $attempt + ' failed under struggle (' + $last.Struggle + '). Retrying on fresh world.')
            $retryNote = 'struggle: ' + $last.Struggle
            $restartWorld = $true
        } else {
            if (-not (Test-QaSetupFailure -ExitCode $code -Codes $exitCodes -VerdictLine $last.Verdict -BotArmed $last.BotArmed)) { break }
            $why = Get-QaSetupFailureWords -Reason $last.Reason
            Write-QaWarn ('Attempt ' + $attempt + ' failed before test started (' + $why + '). Retrying.')
            $retryNote = $why
        }

        if ($OneServer) {
            foreach ($k in @('KeepServer', 'ReuseServer', 'StopServer')) { $mpArgs.Remove($k) }
            $retired = $true
            if ($restartWorld -and ($null -ne (Get-KeptServer))) { $retired = [bool](Stop-KeptServer) }
            $upNow = $null -ne (Get-KeptServer)
            $retryPlan = Get-QaRetryServerPlan -RestartWorld $restartWorld -RetirementConfirmed $retired -NowReusable $upNow -Last $step.Last
            if ($retryPlan.Refuse) { $retryRefused = $true; break }
            if (-not $upNow) { [void](Stop-KeptServer) }
            if ($retryPlan.Reuse) { $mpArgs['ReuseServer'] = $true }
            if ($retryPlan.Stop)  { $mpArgs['StopServer'] = $true }
            if ($retryPlan.Keep)  { $mpArgs['KeepServer'] = $true }
        }
    }
    if ($retryRefused) { $retryNote = 'unrecoverable: kept server could not be retired' }
    elseif ($code -eq 0 -and $retryNote) { $retryNote = 'passed on retry (' + $retryNote + ')' }
    elseif ($code -ne 0) { $retryNote = '' }

    $elapsed = [int]((Get-Date) - $started).TotalSeconds
    $memAfter = Get-QaMemorySnapshot
    [void]$results.Add([pscustomobject]@{
        Scenario     = $step.Item.Token
        Note         = $retryNote
        ExitCode     = $code
        Passed       = ($code -eq 0)
        ElapsedSec   = $elapsed
        FreeMbBefore = $(if ($null -ne $memBefore) { [int]$memBefore.FreeMb } else { -1 })
        FreeMbAfter  = $(if ($null -ne $memAfter)  { [int]$memAfter.FreeMb }  else { -1 })
    })

    if ($OneServer -and (-not $step.Last) -and ($null -eq (Get-KeptServer))) {
        [void](Stop-KeptServer)
        if ($index -lt $plan.Count) { $plan[$index].First = $true }
    }
    if ($code -ne 0 -and $StopOnFirstFailure) { break }
}
} finally {
    if ($OneServer) { [void](Stop-KeptServer) }
    Remove-Item -LiteralPath $runDirFile -Force -ErrorAction SilentlyContinue
}

[void](Wait-QaMpIdle -WorktreePath $WorktreePath -TimeoutSec 120)

Write-Host ''
Write-QaStep 'SWEEP SUMMARY'
Write-Host ('    {0,-28} {1,-5} {2,9} {3,5}   {4}' -f 'SCENARIO', '', 'EXIT', 'TIME', 'FREE RAM (start -> end)')
$failed = 0
foreach ($r in $results) {
    $verdict = 'PASS'
    if ($r.ExitCode -eq 7) { $verdict = 'INCON'; $failed++ }
    elseif (-not $r.Passed) { $verdict = 'FAIL'; $failed++ }
    $ram = 'n/a'
    if ($r.FreeMbBefore -ge 0) {
        $ram = Format-QaMemoryMb -Mb $r.FreeMbBefore
        if ($r.FreeMbAfter -ge 0) { $ram = $ram + ' -> ' + (Format-QaMemoryMb -Mb $r.FreeMbAfter) }
    }
    $noteText = ''
    if ($r.Note) { $noteText = '  <- ' + $r.Note }
    Write-Host ('    {0,-28} {1,-5} exit {2,-4} {3,5}s  {4}{5}' -f $r.Scenario, $verdict, $r.ExitCode, $r.ElapsedSec, $ram, $noteText)
}

$ran = @($results).Count
$skipped = @($Scenarios).Count - $ran
Write-Host ''
if ($failed -eq 0 -and $skipped -eq 0) {
    Write-QaInfo ('SWEEP PASS: all ' + $ran + ' scenario(s) passed.')
    exit 0
}
Write-QaError ('SWEEP FAIL: ' + $failed + ' failed, ' + $skipped + ' skipped of ' + @($Scenarios).Count + '.')
exit 1
