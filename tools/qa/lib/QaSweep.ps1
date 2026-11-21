# QaSweep.ps1 - pure sweep and kept-server decisions for BGONE MP rig (PowerShell 5.1 ONLY).

function Split-SweepScenario {
    param([string]$Token)
    $name = $Token
    $count = 0
    if ($Token -match '^(?<n>[A-Za-z0-9_]+):(?<c>\d+)$') { $name = $Matches['n']; $count = [int]$Matches['c'] }
    return [pscustomobject]@{ Name = $name; Clients = $count; Token = $Token }
}

function Get-SweepGroups {
    param($Items, $Touches)
    $groups = New-Object System.Collections.ArrayList
    foreach ($item in @($Items)) {
        $mine = @()
        if ($null -ne $Touches) {
            $prop = $Touches.PSObject.Properties[$item.Name]
            if ($null -ne $prop) { $mine = @($prop.Value) }
        }
        $placed = $false
        foreach ($g in $groups) {
            $clash = $false
            foreach ($tag in $mine) { if ($g.Tags -contains $tag) { $clash = $true } }
            if (-not $clash) {
                [void]$g.Items.Add($item)
                foreach ($tag in $mine) { if ($g.Tags -notcontains $tag) { $g.Tags += $tag } }
                $placed = $true
                break
            }
        }
        if (-not $placed) {
            $g = [pscustomobject]@{ Items = (New-Object System.Collections.ArrayList); Tags = @($mine) }
            [void]$g.Items.Add($item)
            [void]$groups.Add($g)
        }
    }
    return @($groups.ToArray())
}

function Test-QaSetupFailure {
    param([int]$ExitCode, $Codes, [string]$VerdictLine, [bool]$BotArmed)
    if ($ExitCode -eq 0) { return $false }
    if ($ExitCode -eq $Codes.Setup -or $ExitCode -eq $Codes.Inconclusive) { return $true }
    if ($ExitCode -eq $Codes.Hang -and (-not $BotArmed)) { return $true }
    if ($VerdictLine -match 'never reported the scenario staged|never gained a controlled character|never gained a character') { return $true }
    return $false
}

function Get-QaMpWindowState {
    param([datetime]$LaunchTime, $BotStartedAt, [int]$LoadTimeoutSec, [int]$ScenarioTimeoutSec, [datetime]$Now)
    if ($null -eq $BotStartedAt) {
        $deadline = $LaunchTime.AddSeconds($LoadTimeoutSec)
        $state = [pscustomobject]@{ Phase = 'loading'; Deadline = $deadline; Expired = ($Now -ge $deadline); Reason = '' }
        if ($state.Expired) { $state.Reason = ('the game was still loading when the ' + $LoadTimeoutSec + '-second loading limit ran out (the bot never started)') }
        return $state
    }
    $deadline = ([datetime]$BotStartedAt).AddSeconds($ScenarioTimeoutSec)
    $state = [pscustomobject]@{ Phase = 'scenario'; Deadline = $deadline; Expired = ($Now -ge $deadline); Reason = '' }
    if ($state.Expired) { $state.Reason = ('the scenario was still running ' + $ScenarioTimeoutSec + ' seconds after the bot started (scenario limit)') }
    return $state
}

function Get-QaSetupFailureWords {
    param([string]$Reason)
    if ($Reason -match 'server boot failed|server not ready|server process exited') { return 'the game server did not start' }
    if ($Reason -match 'could not start replication|VOID') { return 'the game server did not start properly' }
    if ($Reason -match 'never reported the scenario staged|never gained') { return 'the player never got into the game' }
    if ($Reason -match 'still loading when the') { return 'the game was too slow to load' }
    if ($Reason -match 'exited before its bot armed|INCONCLUSIVE') { return 'the game closed before the test began' }
    return 'the test did not get as far as starting'
}

function Get-QaRetryServerPlan {
    param([bool]$RestartWorld, [bool]$RetirementConfirmed, [bool]$NowReusable, [bool]$Last)
    $plan = [pscustomobject]@{ Refuse = $false; Reuse = $false; Keep = $false; Stop = $false; Reason = '' }
    if ($RestartWorld) {
        if ($NowReusable -or (-not $RetirementConfirmed)) {
            $plan.Refuse = $true
            $plan.Reason = 'the failed attempt''s changed world could not be retired'
            return $plan
        }
        $plan.Keep = (-not $Last)
        return $plan
    }
    if ($NowReusable) {
        $plan.Reuse = $true
        if ($Last) { $plan.Stop = $true } else { $plan.Keep = $true }
        return $plan
    }
    $plan.Keep = (-not $Last)
    return $plan
}

function Get-QaOwnedKeptServer {
    param([string]$StatePath, [string]$Owner, [switch]$IncludeDead)
    if (-not (Test-Path -LiteralPath $StatePath)) { return $null }
    try { $s = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json } catch { return $null }
    if ($null -eq $s -or [string]::IsNullOrEmpty($Owner) -or ([string]$s.owner -ne $Owner)) { return $null }
    if ($IncludeDead -or (Test-QaAnyPidAlive -Pids @($s.serverPids))) { return $s }
    return $null
}

function Get-QaKeptServerLiveEntries {
    param($Record)
    if ($null -eq $Record) { return @() }
    return @(Get-QaVerifiedProcessEntries -Pids @($Record.serverPids) -Identities @($Record.serverPidIdentities))
}

function Get-QaKeptServerLivePids {
    param($Record)
    return @(Get-QaKeptServerLiveEntries -Record $Record | ForEach-Object { [int]$_.Pid })
}

function Test-QaKeptServerReusable {
    param($Record)
    if ($null -eq $Record) { return $false }
    $pids = @($Record.serverPids)
    if ($pids.Count -eq 0) { return $false }
    $ids = @($Record.serverPidIdentities)
    $live = 0
    for ($i = 0; $i -lt $pids.Count; $i++) {
        $p = 0
        try { $p = [int]$pids[$i] } catch { continue }
        if ($p -le 0) { continue }
        if (-not (Test-QaPidAlive -ProcessId $p)) { continue }
        $want = ''
        if ($i -lt $ids.Count) { $want = [string]$ids[$i] }
        if (-not (Test-QaProcessIdentity -ProcessId $p -ExpectedStartUtc $want)) { return $false }
        $live++
    }
    return ($live -gt 0)
}

function Test-QaKeptServerCodeMatch {
    param($Record, [string]$Commit, [string[]]$Dirty, [string]$WorldPath, [string]$MissionHeader, [string]$Engine)
    if ($null -eq $Record) { return $false }
    $rCommit = [string]$Record.codeCommit
    if ([string]::IsNullOrWhiteSpace($rCommit) -or ($rCommit -ne [string]$Commit)) { return $false }
    $rDirty = (@(@($Record.codeDirty) | ForEach-Object { [string]$_ } | Where-Object { $_ }) -join ',')
    $wantDirty = (@(@($Dirty) | ForEach-Object { [string]$_ } | Where-Object { $_ }) -join ',')
    if ($rDirty -ne $wantDirty) { return $false }
    if ([string]$Record.worldPath -ne [string]$WorldPath) { return $false }
    if ([string]$Record.missionHeader -ne [string]$MissionHeader) { return $false }
    if ([string]$Record.engine -ne [string]$Engine) { return $false }
    return $true
}
