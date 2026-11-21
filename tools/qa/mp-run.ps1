<#
.SYNOPSIS
    BGONE single-addon headless MP QA rig (1.9.0.37 retail server + retail client pair).

.DESCRIPTION
    Stands up a local dedicated server (ArmaReforgerServer.exe) from an external staging junction
    (C:\wt\.bgone-stage\<sha1-8>\Addons\BGONE -> <WorktreePath>), joins 1..2 retail clients
    (ArmaReforgerSteam.exe), runs BGONE_MPBotController scenarios, parses every log, and exits 0/1/2/3/7.

    Shares MRS-GC's machine-wide locks (C:\wt\.mp-rig.lock, Global\MRS_QaRigAdmission,
    %LOCALAPPDATA%\MRS-QA\screen-claim.json) and is 100% compatible with rig_queue.py / queue-run.ps1.
    BGONE QA is strictly headless (no real mouse); -Hand is refused cleanly with exit 2.

.NOTES
    Exit codes: 0 PASS | 1 FAIL / script errors | 2 setup/launch refusal | 3 timeout | 7 inconclusive.
    PowerShell 5.1 ONLY: no &&/||, no ternary, no null-coalescing.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$WorktreePath,

    [string]$Engine = '',
    [string]$ServerExe,
    [string]$ClientExe,

    [string]$World,
    [string]$MissionHeaderPath,

    [string]$Scenario = 'identity',
    [string[]]$ExpectedError = @(),

    [ValidateRange(1, 2)]
    [int]$Clients = 1,

    [int]$ServerReadyTimeoutSec = 0,
    [int]$ScenarioTimeoutSec = 0,
    [int]$LoadTimeoutSec = 0,
    [int]$GraceSec = 0,
    [int]$KeepRuns = 0,

    [switch]$KeepProfile,

    # Optional background window recording via ffmpeg gfxcapture (touches no mouse or focus).
    [switch]$Record,
    [int[]]$RecordClients = @(1),
    [int[]]$LateJoinClients = @(),

    # BGONE QA is headless/no-real-mouse. Passing -Hand refuses cleanly with exit 2.
    # (Queue compatibility note: MRS_QA_QUEUE_NO_HAND_WAIT is never waited on here.)
    [switch]$Hand,

    [string]$RunDirFile = '',

    # One-server sweep switches (mp-sweep.ps1 -OneServer).
    [switch]$KeepServer,
    [switch]$ReuseServer,
    [switch]$StopServer,
    [string]$KeepOwner = '',

    [switch]$Queue,
    [int]$QueueTimeoutSec = 0,

    [switch]$ForceRigLock,
    [switch]$NoReapOrphans,
    [switch]$NoGitExclude,
    [switch]$AllowUnsafeWorktree,
    [switch]$AcceptEngineVersion,
    [switch]$DiscardRdb,

    # Queue prep-lane marker checked verbatim by rig_queue.py.
    [switch]$PrepRdb,

    [Alias('DryRun')]
    [switch]$WhatIf,

    [string]$ConfigPath
)

$ErrorActionPreference = 'Stop'

# ------------------------------------------------------------------------------------------------
# Load libraries
# ------------------------------------------------------------------------------------------------
$libDir = Join-Path $PSScriptRoot 'lib'
. (Join-Path $libDir 'QaCommon.ps1')
. (Join-Path $libDir 'QaLogParse.ps1')
. (Join-Path $libDir 'QaReport.ps1')
. (Join-Path $libDir 'QaConfig.ps1')
. (Join-Path $libDir 'QaArtifacts.ps1')
. (Join-Path $libDir 'QaProcess.ps1')
. (Join-Path $libDir 'QaMemory.ps1')
. (Join-Path $libDir 'QaMachine.ps1')
. (Join-Path $libDir 'QaCapture.ps1')
. (Join-Path $libDir 'QaSweep.ps1')

$codes = Get-QaExitCodes

# Refuse -Hand cleanly: BGONE QA is strictly headless/no-real-mouse.
if ($Hand) {
    Write-QaError '-Hand is not supported: BGONE QA is strictly headless (no real mouse or foreground focus).'
    exit $codes.Setup
}

# Verify shared MRS-GC rig-lock helpers exist before doing anything.
foreach ($required in @('New-QaMpRigLock', 'Remove-QaMpRigLock', 'Close-QaRigAdmission', 'Add-QaMpRigLockTokens', 'Test-QaRunGone', 'Format-QaRefusal', 'Test-QaRefusalFinal', 'Get-QaRefusalAdvice')) {
    if (-not (Get-Command $required -ErrorAction SilentlyContinue)) {
        Write-QaError ("Missing required rig-lock helper '" + $required + "' in " + (Join-Path $libDir 'QaProcess.ps1'))
        exit $codes.Setup
    }
}

# ------------------------------------------------------------------------------------------------
# Config
# ------------------------------------------------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path $PSScriptRoot 'engines.json'
}
$config = $null
$patterns = Get-QaDefaultPatterns
$defaults = $null
try {
    $config = Get-QaConfig -Path $ConfigPath
    $defaults = Get-QaProp -Object $config -Name 'defaults'
    $patterns = Merge-QaPatterns -Overrides (Get-QaProp -Object $config -Name 'patterns')
    if ([string]::IsNullOrWhiteSpace($Engine)) { $Engine = Get-QaTargetEngine -Defaults $defaults }
} catch {
    Write-QaError ("Failed to load config '" + $ConfigPath + "': " + $_.Exception.Message)
    exit $codes.Setup
}

try {
    $mpCfg = Get-QaRigConfig -Config $config -Engine $Engine -Section 'mp'
} catch {
    Write-QaError $_.Exception.Message
    exit $codes.Setup
}
if ($null -eq $mpCfg) {
    Write-QaError ("Engine '" + $Engine + "' has no 'mp' section in engines.json.")
    exit $codes.Setup
}
$mpServerCfg = Get-QaProp -Object $mpCfg -Name 'server'
$mpClientCfg = Get-QaProp -Object $mpCfg -Name 'client'
if (($null -eq $mpServerCfg) -or ($null -eq $mpClientCfg)) {
    Write-QaError ("engines." + $Engine + ".mp must define both 'server' and 'client'.")
    exit $codes.Setup
}

# ------------------------------------------------------------------------------------------------
# Single-addon worktree validation (<WorktreePath>\addon.gproj)
# ------------------------------------------------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($WorktreePath)) {
    Write-QaError 'No -WorktreePath given. Try: mp-run.ps1 -WorktreePath C:\wt\<worktree>'
    exit $codes.Setup
}
if (-not (Test-Path -LiteralPath $WorktreePath)) {
    Write-QaError ("Worktree path does not exist: " + $WorktreePath)
    exit $codes.Setup
}
$WorktreePath = (Resolve-Path -LiteralPath $WorktreePath).ProviderPath
$rootGproj = Join-Path $WorktreePath 'addon.gproj'
if (-not (Test-Path -LiteralPath $rootGproj)) {
    Write-QaError ("Not a valid BGONE single-addon worktree - missing root addon.gproj under " + $WorktreePath)
    exit $codes.Setup
}

$safety = Test-QaWorktreeSafety -WorktreePath $WorktreePath -SafetyConfig (Get-QaProp -Object $defaults -Name 'worktreeSafety')
if (-not $safety.Ok) {
    foreach ($i in $safety.Issues) {
        if ($WhatIf -or $AllowUnsafeWorktree) { Write-QaWarn $i } else { Write-QaError $i }
    }
    if (-not $WhatIf -and -not $AllowUnsafeWorktree) {
        Write-QaInfo 'Override with -AllowUnsafeWorktree if you accept the risk.'
        exit $codes.Setup
    }
}

$excluded = @(Get-QaProp -Object $mpCfg -Name 'excludeAddons' -Default @())
$addonIds = @(Get-QaAddonIds -AddonsDir $WorktreePath | Where-Object { $excluded -notcontains $_ })
if ($addonIds.Count -eq 0) {
    Write-QaError ("No addon ID found in " + $rootGproj)
    exit $codes.Setup
}
$addonId = [string]$addonIds[0]
$addonGuid = Get-QaAddonGuid -GprojPath $rootGproj
if ([string]::IsNullOrWhiteSpace($addonGuid)) {
    $addonGuid = [string](Get-QaProp -Object $defaults -Name 'addonGuid' -Default '5F1EE615E7AE3106')
}
$stageInfo = Get-QaStageInfo -WorktreePath $WorktreePath -AddonName $addonId

# ------------------------------------------------------------------------------------------------
# Resolve executables, dirs, versions, timings, patterns
# ------------------------------------------------------------------------------------------------
try {
    $serverExePath = Resolve-QaWorkbenchExe -EngineConfig $mpServerCfg -Override $ServerExe -EngineName ($Engine + '/mp-server')
    $serverDir     = Resolve-QaGameDir -EngineConfig $mpServerCfg -Override '' -EngineName ($Engine + '/mp-server')
    $clientExePath = Resolve-QaWorkbenchExe -EngineConfig $mpClientCfg -Override $ClientExe -EngineName ($Engine + '/mp-client')
    $clientDir     = Resolve-QaGameDir -EngineConfig $mpClientCfg -Override '' -EngineName ($Engine + '/mp-client')
} catch {
    Write-QaError $_.Exception.Message
    exit $codes.Setup
}

$validatedVersion = [string](Get-QaProp -Object (Get-QaEngineConfig -Config $config -Engine $Engine) -Name 'validatedVersion' -Default '')
$versionProblem = Get-QaEngineVersionProblem -Engine $Engine -ValidatedVersion $validatedVersion -Exes @($serverExePath, $clientExePath)
if ($versionProblem) {
    if ($WhatIf -or $AcceptEngineVersion) { Write-QaWarn $versionProblem } else { Write-QaError $versionProblem; exit $codes.Setup }
}

$ServerReadyTimeoutSec = [int](Get-QaOrDefault -Value $ServerReadyTimeoutSec -Fallback (Get-QaProp -Object $mpServerCfg -Name 'readyTimeoutSec' -Default 240))
$scenarioTimeoutFallback = Get-QaProp -Object $mpCfg -Name 'scenarioTimeoutSec' -Default 180
if ($Scenario -ne 'identity') {
    $scenarioTimeoutFallback = Get-QaProp -Object $mpCfg -Name 'gameplayScenarioTimeoutSec' -Default 240
}
$ScenarioTimeoutSec = [int](Get-QaOrDefault -Value $ScenarioTimeoutSec -Fallback $scenarioTimeoutFallback)
$LoadTimeoutSec     = [int](Get-QaOrDefault -Value $LoadTimeoutSec     -Fallback (Get-QaProp -Object $mpCfg -Name 'loadTimeoutSec' -Default 360))
$GraceSec           = [int](Get-QaOrDefault -Value $GraceSec           -Fallback (Get-QaProp -Object $defaults -Name 'graceSec' -Default 10))
$KeepRuns           = [int](Get-QaOrDefault -Value $KeepRuns           -Fallback (Get-QaProp -Object $defaults -Name 'keepRuns' -Default 20))
$QueueTimeoutSec    = [int](Get-QaOrDefault -Value $QueueTimeoutSec    -Fallback (Get-QaProp -Object $defaults -Name 'queueTimeoutSec' -Default 14400))
$pollSec            = [int](Get-QaProp -Object $defaults -Name 'pollSec'       -Default 2)
$pidResolveSec      = [int](Get-QaProp -Object $defaults -Name 'pidResolveSec' -Default 20)
$logFileName        = [string](Get-QaProp -Object $defaults -Name 'logFileName'  -Default 'console.log')
$errorLogName       = [string](Get-QaProp -Object $defaults -Name 'errorLogFileName' -Default 'error.log')
$rdbGlob            = [string](Get-QaProp -Object $defaults -Name 'rdbChurnGlob' -Default 'resourceDatabase.rdb')
$vanillaGuid        = [string](Get-QaProp -Object $defaults -Name 'vanillaAddonGuid' -Default '58D0FB3206B6F859')

$serverProcPrefix    = [string](Get-QaProp -Object $mpServerCfg -Name 'processNamePrefix' -Default 'ArmaReforgerServer')
$clientProcPrefix    = [string](Get-QaProp -Object $mpClientCfg -Name 'processNamePrefix' -Default 'ArmaReforgerSteam')
$readyPattern        = [string](Get-QaProp -Object $mpServerCfg -Name 'readyPattern' -Default 'NETWORK\s+:\s*Starting RPL server, listening on')
$connectedPattern    = [string](Get-QaProp -Object $mpClientCfg -Name 'connectedPattern' -Default 'RPL\s+:\s*Client event Connected')
$mpPatterns          = Get-QaProp -Object $mpCfg -Name 'patterns'
$botConnectedPattern = [string](Get-QaProp -Object $mpPatterns -Name 'BotConnected' -Default 'BGONE_MPBOT \| CONNECTED')

if ([string]::IsNullOrWhiteSpace($Scenario)) { $Scenario = 'identity' }
if ($Scenario -notmatch '^[A-Za-z0-9_]+$') {
    Write-QaError ("Invalid -Scenario '" + $Scenario + "': scenario names must be alphanumeric/underscore.")
    exit $codes.Setup
}
$botVerdictPattern   = 'BGONE_MPBOT \| VERDICT \| (?<result>PASS|FAIL) \| ' + $Scenario + ' \|'
$botServerAckPattern = 'BGONE_MPBOT \| SERVER_ACK \| ' + $Scenario + ' \| (?<result>PASS|FAIL) \|'

$worldPath     = [string](Get-QaProp -Object $mpCfg -Name 'worldPath' -Default 'Worlds/MPTest/MPTest.ent')
$missionHeader = [string](Get-QaProp -Object $mpCfg -Name 'missionHeaderPath' -Default 'Missions/BGONE_MPTest.conf')
if (-not [string]::IsNullOrWhiteSpace($World))             { $worldPath = $World }
if (-not [string]::IsNullOrWhiteSpace($MissionHeaderPath)) { $missionHeader = $MissionHeaderPath }

# ------------------------------------------------------------------------------------------------
# Run context + command lines
# ------------------------------------------------------------------------------------------------
$startTime = Get-Date
$gitStamp = Get-QaGitStamp -WorktreePath $WorktreePath
$memConfig = Get-QaMemoryConfig -Defaults $defaults
$memAtStart = Get-QaMemorySnapshot
$ctx = New-QaRunContext -WorktreePath $WorktreePath -Engine ($Engine + '-mp') -StartTime $startTime
if ($RunDirFile -and (-not $WhatIf) -and (-not $PrepRdb)) { [System.IO.File]::WriteAllText($RunDirFile, $ctx.RunDir) }
$serverProfile = Get-QaStageProfileDir -Context $ctx -Slot ($Engine + '-mp-server')

$sharedStatePath = Join-Path $ctx.RunsRoot 'shared-server.json'
$sharedState = $null
$serverSkipLines = 0
if ($ReuseServer) {
    if (Test-Path -LiteralPath $sharedStatePath) {
        try { $sharedState = Get-Content -LiteralPath $sharedStatePath -Raw | ConvertFrom-Json } catch { $sharedState = $null }
    }
    if ($null -eq $sharedState -or -not (Test-QaKeptServerReusable -Record $sharedState)) {
        Write-QaError ('-ReuseServer: no kept server this run owns is running (' + $sharedStatePath + ').')
        exit $codes.Setup
    }
    if ([string]$sharedState.owner -ne $KeepOwner) {
        Write-QaError ('-ReuseServer: kept server belongs to another run (' + [string]$sharedState.startedBy + ').')
        exit $codes.Setup
    }
    if (-not (Test-QaKeptServerCodeMatch -Record $sharedState -Commit $gitStamp.Commit -Dirty @($gitStamp.Dirty) -WorldPath $worldPath -MissionHeader $missionHeader -Engine $Engine)) {
        Write-QaError ('-ReuseServer: kept server loaded different code/world/engine.')
        exit $codes.Setup
    }
    $serverProfile = [string]$sharedState.serverProfile
}

$serverTokens = @{
    '{ADDONSDIR}'     = $stageInfo.AddonsDir
    '{ADDONLIST}'     = ($addonIds -join ',')
    '{PROFILE}'       = $serverProfile
    '{WORKTREE}'      = $WorktreePath
    '{WORLDPATH}'     = $worldPath
    '{MISSIONHEADER}' = $missionHeader
    '{SCENARIO}'      = $Scenario
    '{CLIENTINDEX}'   = '0'
}

$clientRecList = New-Object System.Collections.ArrayList
for ($ci = 1; $ci -le $Clients; $ci++) {
    $slot = $Engine + '-mp-client' + $ci
    $prof = Get-QaStageProfileDir -Context $ctx -Slot $slot
    $tok = @{}
    foreach ($k in $serverTokens.Keys) { $tok[$k] = $serverTokens[$k] }
    $tok['{PROFILE}'] = $prof
    $tok['{CLIENTINDEX}'] = [string]$ci
    $clientExtra = @()
    if ($Record -and (@($RecordClients) -contains $ci)) { $clientExtra = @('-bgoneBotPace', '1500') }
    $cArgs = Build-QaArgumentList -EngineConfig $mpClientCfg -Tokens $tok -Autotest $false -AdditionalArgs $clientExtra
    [void]$clientRecList.Add(@{
        Index            = $ci
        Profile          = $prof
        Args             = $cArgs
        Cmd              = (Format-QaCommandLine -Exe $clientExePath -Arguments $cArgs -WorkingDirectory $clientDir)
        Pids             = @()
        Log              = $null
        ConnectedLine    = $null
        BotConnectedLine = $null
        VerdictLine      = $null
        Start            = $null
        Record           = ($Record -and (@($RecordClients) -contains $ci))
        Capture          = $null
        RecordStatus     = 'not requested'
        Held             = $false
    })
}
$clientRecs = @($clientRecList.ToArray())
if (@($LateJoinClients).Count -eq 0) {
    $lateCfg = Get-QaProp -Object (Get-QaProp -Object $mpCfg -Name 'lateJoin') -Name $Scenario
    if ($lateCfg) { $LateJoinClients = @($lateCfg | ForEach-Object { [int]$_ }) }
}
foreach ($c in $clientRecs) { if (@($LateJoinClients) -contains $c.Index -and $c.Index -ne 1) { $c.Held = $true } }

if ($KeepServer) { $serverTokens['{SCENARIO}'] = 'batch' }
$serverArgs = Build-QaArgumentList -EngineConfig $mpServerCfg -Tokens $serverTokens -Autotest $false
$serverCmd = Format-QaCommandLine -Exe $serverExePath -Arguments $serverArgs -WorkingDirectory $serverDir

Write-QaStep 'BGONE headless MP rig (1.9.0.37 retail + retail)'
Write-QaInfo ('Worktree      : ' + $WorktreePath)
Write-QaInfo ('Stage dir     : ' + $stageInfo.AddonsDir + '  (junction ' + $stageInfo.AddonLink + ' -> ' + $WorktreePath + ')')
Write-QaInfo ('Engine        : ' + $Engine + ' (validated ' + $validatedVersion + ')')
Write-QaInfo ('Code          : ' + $gitStamp.Text)
Write-QaInfo ('Addons        : ' + ($addonIds -join ',') + ' (GUID ' + $addonGuid + ')')
Write-QaInfo ('World         : ' + $worldPath + '   header: ' + $missionHeader)
Write-QaInfo ('Scenario      : ' + $Scenario)
Write-QaInfo ('Server exe    : ' + $serverExePath)
Write-QaInfo ('Client exe    : ' + $clientExePath)
Write-QaInfo ('Server profile: ' + $serverProfile)
Write-QaInfo ('Clients       : ' + $Clients)
foreach ($c in $clientRecs) { Write-QaInfo ('  client ' + $c.Index + ' profile: ' + $c.Profile) }
Write-QaInfo ('Timeouts      : server-ready ' + $ServerReadyTimeoutSec + 's, loading ' + $LoadTimeoutSec + 's, scenario ' + $ScenarioTimeoutSec + 's')
Write-QaInfo ('Free RAM      : ' + (Format-QaMemorySample -Sample $memAtStart))
Write-Host ''
Write-Host 'Resolved command line (stage 1 - dedicated server):'
Write-Host $serverCmd -ForegroundColor DarkGray
foreach ($c in $clientRecs) {
    Write-Host ''
    Write-Host ('Resolved command line (stage 2 - game client ' + $c.Index + ', joins 127.0.0.1:2001):')
    Write-Host $c.Cmd -ForegroundColor DarkGray
}

$memWarnLines = Get-QaLowMemoryWarningLines -Sample $memAtStart -ThresholdMb $memConfig.LowFreeWarnMb
if (@($memWarnLines).Count -gt 0) {
    Write-Host ''
    foreach ($l in $memWarnLines) { Write-QaWarn $l }
}

# Root resourceDatabase.rdb coverage check
if ($DiscardRdb) { Write-QaDiscardRdbDeprecated }
if (-not $WhatIf) { [void](Set-QaRdbSkipWorktree -WorktreePath $WorktreePath -PathSpec $rdbGlob) }
$staleRdb = @(Get-QaStaleRdbAddons -AddonsDir $WorktreePath -AddonIds $addonIds)
foreach ($s in $staleRdb) {
    Write-QaWarn ($s.Addon + ' resourceDatabase.rdb does not list ' + @($s.Missing).Count + ' resource(s), e.g. ' + @($s.Missing)[0])
}

# Queue prep lane (-PrepRdb): regenerate root resourceDatabase.rdb without holding the MP rig lock.
if ($PrepRdb -and -not $WhatIf) {
    if ($staleRdb.Count -gt 0) {
        $staleRdb = @(Update-QaStaleRdb -WorktreePath $WorktreePath -AddonIds $addonIds -Stale $staleRdb -Engine $Engine -QaRunPath (Join-Path $PSScriptRoot 'qa-run.ps1') -QueueTimeoutSec $QueueTimeoutSec -ConfigPath $ConfigPath -AcceptEngineVersion:$AcceptEngineVersion)
    }
    if ($staleRdb.Count -gt 0) {
        Write-QaWarn ('Prep: resourceDatabase.rdb is still stale; the run will retry in its own slot.')
        exit $codes.Setup
    }
    Write-QaInfo 'Prep: BGONE resourceDatabase.rdb lists all resources.'
    exit $codes.Clean
}

if ($WhatIf) {
    Write-Host ''
    Write-QaInfo 'DRY RUN (-WhatIf): nothing was launched, no junctions or artifacts were written.'
    exit $codes.Clean
}

# Ensure external staging junction exists.
try {
    $stageInfo = Ensure-QaStageAddonsDir -WorktreePath $WorktreePath -AddonName $addonId
} catch {
    Write-QaError ('Could not prepare staging junction: ' + $_.Exception.Message)
    exit $codes.Setup
}

$machineCfg = Get-QaMachineConfig -Defaults $defaults
$machineGate = $null

[void](New-QaDirectory -Path $ctx.RunDir)
[void](New-QaDirectory -Path $serverProfile)
foreach ($c in $clientRecs) { [void](New-QaDirectory -Path $c.Profile) }
if (-not $NoGitExclude) {
    try { [void](Add-QaGitExclude -RepoPath $WorktreePath -Rule '.qa/') } catch { }
}
$cmdLines = New-Object System.Collections.ArrayList
[void]$cmdLines.Add('# stage 1 - dedicated server')
[void]$cmdLines.Add($serverCmd)
foreach ($c in $clientRecs) {
    [void]$cmdLines.Add('')
    [void]$cmdLines.Add('# stage 2 - game client ' + $c.Index)
    [void]$cmdLines.Add($c.Cmd)
}
Write-QaTextFile -Path (Join-Path $ctx.RunDir 'command.txt') -Lines $cmdLines.ToArray()

function Find-MpLogLine {
    param([string]$LogPath, [string]$Pattern, [int]$SkipLines = 0)
    if ([string]::IsNullOrWhiteSpace($LogPath)) { return $null }
    if (-not (Test-Path -LiteralPath $LogPath)) { return $null }
    if ($SkipLines -gt 0) {
        $after = @(Read-QaLines -Path $LogPath) | Select-Object -Skip $SkipLines
        foreach ($line in $after) { if ($line -match $Pattern) { return $line.Trim() } }
        return $null
    }
    $m = Select-String -LiteralPath $LogPath -Pattern $Pattern -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($m) { return $m.Line.Trim() }
    return $null
}

function Find-MpConsoleLog {
    param([string]$ProfileDir, [datetime]$After = [datetime]::MinValue)
    $logsRoot = Join-Path $ProfileDir 'logs'
    if (-not (Test-Path -LiteralPath $logsRoot)) { return $null }
    $dirs = @(Get-ChildItem -LiteralPath $logsRoot -Directory -Filter 'logs_*' -ErrorAction SilentlyContinue | Sort-Object Name -Descending)
    foreach ($d in $dirs) {
        if ($d.CreationTime -lt $After) { continue }
        $c = Join-Path $d.FullName 'console.log'
        if (Test-Path -LiteralPath $c) { return $c }
    }
    return $null
}

$serverPids = @()
$serverPidIdentities = @()
$serverLog = $null
$outcomeCode = $codes.Hang
$silentExit = $null
$outcomeReason = 'scenario window elapsed with no verdict'
$windowExpiredReason = $null
$serverReadyLine = $null
$serverAckLine = $null
$itemTableLine = $null

$preTokens = @($serverProfile)
$stopServerNow = ($StopServer -or (-not ($KeepServer -or $ReuseServer)))
$serverKept = $false
foreach ($c in $clientRecs) { $preTokens += @($c.Profile) }
$machineSampler = $null
$mpBranch = ''
try { $mpBranch = ([string](& git -C $WorktreePath rev-parse --abbrev-ref HEAD 2>$null)).Trim() } catch { $mpBranch = '' }

if (-not $ReuseServer) {
    if ($Queue) {
        Write-QaStep ('Queued mode: waiting for the MP rig to go idle (up to ' + $QueueTimeoutSec + 's)')
        if (Wait-QaMpIdle -WorktreePath $WorktreePath -TimeoutSec $QueueTimeoutSec -Announce) {
            Write-QaInfo 'MP rig is idle; continuing.'
        } else {
            Write-QaWarn ('Still busy after ' + $QueueTimeoutSec + 's; continuing to orphan reap and lock check.')
        }
        [void](Wait-QaMpMemorySettle -MemConfig $memConfig)
    }

    $reap = Invoke-QaOrphanReap -WorktreePath $WorktreePath -ExcludeRunDir $ctx.RunDir -GraceSec $GraceSec
    foreach ($r in @($reap.Reaped)) {
        Write-QaWarn ('Reclaimed orphaned run: host PID ' + $r.HostPid + ' left PID(s) ' + (@($r.Pids) -join ', ') + ' alive. Killed them.')
    }

    $launchNeedMb = Get-QaLaunchNeedMb -Config $machineCfg -StartsServer $true -Clients $Clients
    $machineGate = Wait-QaMachineReady -NeedMb $launchNeedMb -WaitSec $machineCfg.LaunchWaitSec
    Write-QaInfo ('Memory gate   : ' + $machineGate.Note)

    $rigTokens = @($serverProfile) + @($clientRecs | ForEach-Object { $_.Profile })
    $rigAcquired = $false
    $rigToken = ''
    $preflightOk = $false
    try {
        $rigLock = New-QaMpRigLock -WorktreePath $WorktreePath -Branch $mpBranch -Port 2001 -Force:$ForceRigLock -Kind 'mp' -Tokens $rigTokens -RunDir $ctx.RunDir
        if ($Queue -and (-not $rigLock.Taken) -and (-not (Test-QaRefusalFinal -Reason $rigLock.Reason))) {
            Write-QaStep ('MP rig lock is held by another run - waiting up to ' + $QueueTimeoutSec + 's ' + (Format-QaRefusal -Result $rigLock))
            $lockDeadline = (Get-Date).AddSeconds($QueueTimeoutSec)
            while ((-not $rigLock.Taken) -and (-not (Test-QaRefusalFinal -Reason $rigLock.Reason)) -and ((Get-Date) -lt $lockDeadline)) {
                Start-Sleep -Seconds 10
                $rigLock = New-QaMpRigLock -WorktreePath $WorktreePath -Branch $mpBranch -Port 2001 -Kind 'mp' -Tokens $rigTokens -RunDir $ctx.RunDir
            }
        }
        if (-not $rigLock.Taken) {
            $h = $rigLock.Holder
            if (Test-QaRefusalFinal -Reason $rigLock.Reason) {
                Write-QaError ('The MP rig cannot be taken. ' + (Get-QaRefusalAdvice -Reason $rigLock.Reason) + ' ' + (Format-QaRefusal -Result $rigLock))
            } elseif ($h) {
                Write-QaError ('The MP rig is LOCKED by PID ' + $h.pid + ' (' + $h.branch + ', ' + $h.worktree + '). ' + (Format-QaRefusal -Result $rigLock))
            } else {
                Write-QaError ('The MP rig lock could not be taken. ' + (Format-QaRefusal -Result $rigLock))
            }
            exit $codes.Setup
        }
        $rigAcquired = $true
        $rigToken = $rigLock.Token
        Write-QaInfo ('MP rig lock: TAKEN (pid ' + $PID + ', branch ' + $mpBranch + ').')

        $holder = Get-QaUdpPortHolder -Port 2001
        if ($holder) {
            Write-QaError ('UDP port 2001 is already held by ' + $holder.Name + ' (PID ' + $holder.OwningPid + ', IsQaRun=' + $holder.IsQaRun + '). Refusing to launch.')
            exit $codes.Setup
        }

        if ($staleRdb.Count -gt 0) {
            $staleRdb = @(Update-QaStaleRdb -WorktreePath $WorktreePath -AddonIds $addonIds -Stale $staleRdb -Engine $Engine -QaRunPath (Join-Path $PSScriptRoot 'qa-run.ps1') -QueueTimeoutSec $QueueTimeoutSec -ConfigPath $ConfigPath -AcceptEngineVersion:$AcceptEngineVersion)
            if ($staleRdb.Count -gt 0) {
                foreach ($s in $staleRdb) {
                    Write-QaError ($s.Addon + ' resourceDatabase.rdb still does not list: ' + (@($s.Missing) -join ', '))
                }
                Write-QaError 'Refusing to launch MP with stale resourceDatabase.rdb.'
                exit $codes.Setup
            }
            Write-QaInfo 'BGONE resourceDatabase.rdb now lists all resources.'
        }
        $preflightOk = $true
    } finally {
        if ($rigAcquired -and (-not $preflightOk)) { [void](Close-QaRigAdmission -Token $rigToken) }
    }
}

try {
    Write-QaRunProcessRecord -RunDir $ctx.RunDir -Tokens $preTokens
    if ($ReuseServer) {
        $reg = Register-QaRigEngines -Tokens $preTokens -TimeoutSec 60
        if (-not $reg.Registered) {
            $outcomeCode = $codes.Setup
            $outcomeReason = ('could not register engines with reused MP rig lock (' + $reg.Reason + ')')
            Write-QaError ('-ReuseServer: ' + $outcomeReason)
            exit $codes.Setup
        }
    }

    $machineSampler = Start-QaMachineSampler -OutCsv (Join-Path $ctx.RunDir 'machine.csv') -IntervalSec $machineCfg.SampleSec
    $launchNeedMb = Get-QaLaunchNeedMb -Config $machineCfg -StartsServer (-not $ReuseServer) -Clients $Clients
    $launchGateWait = $machineCfg.LaunchWaitSec
    if (($null -ne $machineGate) -and ($machineGate.Status -eq 'short')) { $launchGateWait = 0 }
    $machineGate = Wait-QaMachineReady -NeedMb $launchNeedMb -WaitSec $launchGateWait
    Write-QaInfo ('Memory gate   : ' + $machineGate.Note + ' (at launch)')

    if ($ReuseServer) {
        $serverEntries = @(Get-QaKeptServerLiveEntries -Record $sharedState)
        $serverPids = @($serverEntries | ForEach-Object { [int]$_.Pid })
        $serverPidIdentities = @($serverEntries | ForEach-Object { [string]$_.Identity })
        $serverLog = [string]$sharedState.serverLog
        $serverSkipLines = @(Read-QaLines -Path $serverLog).Count
        $serverReadyLine = '(reused kept server from ' + [string]$sharedState.startedBy + ')'
        Write-QaStep 'Reusing kept dedicated server'
        Write-QaInfo ('Server PID(s): ' + (@($serverPids) -join ', ') + '; reading after line ' + $serverSkipLines)
    } else {
        Write-QaStep 'Launching dedicated server'
        $sProc = Start-QaWorkbench -Exe $serverExePath -Arguments $serverArgs -WorkingDirectory $serverDir -WindowStyle Hidden
        Write-QaInfo ('Seed PID   : ' + $sProc.Id)
        $serverPids = Resolve-QaWorkbenchPids -SeedPid $sProc.Id -Token $serverProfile -NamePrefix $serverProcPrefix -TimeoutSec $pidResolveSec -PollSec $pollSec
        $serverPidIdentities = @(Get-QaProcessIdentities -Pids $serverPids)
        if (@($serverPids).Count -gt 0) { Write-QaInfo ('Live PID(s): ' + (@($serverPids) -join ', ')) }
        Write-QaRunProcessRecord -RunDir $ctx.RunDir -Tokens $preTokens -EnginePids @($serverPids)

        Write-QaStep ('Waiting for server ready marker (regex: ' + $readyPattern + ')')
        $deadline = (Get-Date).AddSeconds($ServerReadyTimeoutSec)
        $lastNote = Get-Date
        while ($true) {
            Start-Sleep -Seconds $pollSec
            if ($null -eq $serverLog) { $serverLog = Find-MpConsoleLog -ProfileDir $serverProfile -After $startTime.AddSeconds(-5) }
            if ($serverLog) {
                $serverReadyLine = Find-MpLogLine -LogPath $serverLog -Pattern $readyPattern
                if ($serverReadyLine) { break }
                $fatal = Find-MpLogLine -LogPath $serverLog -Pattern '\((F)\):|Unable to initialize the game'
                if ($fatal) {
                    $outcomeCode = $codes.Setup
                    $outcomeReason = ('server boot failed: ' + $fatal)
                    throw ('Server boot failed: ' + $fatal)
                }
            }
            if ((@($serverPids).Count -gt 0) -and (-not (Test-QaAnyPidAlive -Pids $serverPids))) {
                $outcomeCode = $codes.Setup
                $outcomeReason = 'server process exited before ready marker'
                $silentExit = 'dedicated server exited before ready marker with no fatal line'
                throw 'Server process exited before ready marker.'
            }
            if ((Get-Date) -ge $deadline) {
                $outcomeCode = $codes.Hang
                $outcomeReason = ('server not ready within ' + $ServerReadyTimeoutSec + 's')
                throw ('Server not ready within ' + $ServerReadyTimeoutSec + 's.')
            }
            if (((Get-Date) - $lastNote).TotalSeconds -ge 30) {
                $lastNote = Get-Date
                Write-QaInfo ('  ... still waiting for server (' + [int]((Get-Date) - $startTime).TotalSeconds + 's elapsed)')
            }
        }
        Write-QaInfo ('Server READY: ' + $serverReadyLine)

        if ($KeepServer) {
            [ordered]@{
                serverPids          = @($serverPids)
                serverPidIdentities = @($serverPidIdentities)
                serverProfile       = $serverProfile
                serverLog           = $serverLog
                startedBy           = $ctx.RunDir
                hostPid             = $PID
                owner               = $KeepOwner
                codeCommit          = $gitStamp.Commit
                codeDirty           = @($gitStamp.Dirty)
                worldPath           = $worldPath
                missionHeader       = $missionHeader
                engine              = $Engine
            } | ConvertTo-Json | Set-Content -LiteralPath $sharedStatePath -Encoding UTF8
            $serverKept = $true
            Write-QaInfo ('Server kept for next scenarios (' + $sharedStatePath + ')')
        }
    }

    # --------------------------------------------------------------------------------------------
    # Stage 2: clients + scenario window
    # --------------------------------------------------------------------------------------------
    $cStart = Get-Date
    $launchClient = {
        param($c)
        Write-QaStep ('Launching game client ' + $c.Index + ' of ' + $Clients)
        $cProc = Start-QaWorkbench -Exe $clientExePath -Arguments $c.Args -WorkingDirectory $clientDir
        Write-QaInfo ('Seed PID   : ' + $cProc.Id)
        $c.Start = Get-Date
        $c.Pids = Resolve-QaWorkbenchPids -SeedPid $cProc.Id -Token $c.Profile -NamePrefix $clientProcPrefix -TimeoutSec $pidResolveSec -PollSec $pollSec
        $allPids = @($serverPids)
        foreach ($rec in $clientRecs) { $allPids += @($rec.Pids) }
        Write-QaRunProcessRecord -RunDir $ctx.RunDir -Tokens $preTokens -EnginePids $allPids
        if (@($c.Pids).Count -gt 0) { Write-QaInfo ('Live PID(s): ' + (@($c.Pids) -join ', ')) }
    }
    foreach ($c in $clientRecs) {
        if ($c.Held) { Write-QaInfo ('Client ' + $c.Index + ' held for late join.'); continue }
        . $launchClient $c
    }

    Write-QaStep ('Scenario window (load ' + $LoadTimeoutSec + 's, scenario ' + $ScenarioTimeoutSec + 's): waiting for BGONE_MPBOT verdicts')
    $windowClient = $null
    foreach ($c in $clientRecs) { if ((-not $c.Held) -and ($null -eq $windowClient)) { $windowClient = $c } }
    $botStartedAt = $null
    $lastNote = Get-Date
    while ($true) {
        Start-Sleep -Seconds $pollSec
        $held = @($clientRecs | Where-Object { $_.Held })
        if ($held.Count -gt 0 -and $clientRecs[0].Log -and (Find-MpLogLine -LogPath $clientRecs[0].Log -Pattern 'BGONE_MPBOT \| LATE_JOIN')) {
            foreach ($c in $held) { $c.Held = $false; . $launchClient $c }
        }
        foreach ($c in $clientRecs) {
            if ($c.Held) { continue }
            if ($c.Record -and ($null -eq $c.Capture) -and ($c.RecordStatus -eq 'not requested')) {
                $hwnd = Find-QaProcessWindow -Pids $c.Pids
                if ($hwnd -ne 0) {
                    $c.Capture = Start-QaCapture -Hwnd $hwnd -RawPath (Join-Path $ctx.RunDir ('client' + $c.Index + '.raw.mkv'))
                    if ($c.Capture) {
                        $c.RecordStatus = 'recording'
                        Write-QaInfo ('Client ' + $c.Index + ' recording started (window ' + $c.Capture.Size + ')')
                    } else {
                        $c.RecordStatus = 'ffmpeg unavailable'
                    }
                }
            }
            if ($null -eq $c.Log) { $c.Log = Find-MpConsoleLog -ProfileDir $c.Profile }
            if (-not $c.Log) { continue }
            if ($null -eq $c.ConnectedLine) {
                $c.ConnectedLine = Find-MpLogLine -LogPath $c.Log -Pattern $connectedPattern
                if ($c.ConnectedLine) { Write-QaInfo ('Client ' + $c.Index + ' CONNECTED: ' + $c.ConnectedLine) }
            }
            if ($null -eq $c.BotConnectedLine) {
                $c.BotConnectedLine = Find-MpLogLine -LogPath $c.Log -Pattern $botConnectedPattern
                if ($c.BotConnectedLine) {
                    Write-QaInfo ('Bot ' + $c.Index + ': ' + $c.BotConnectedLine)
                    if (($null -eq $botStartedAt) -and ($null -ne $windowClient) -and ($c.Index -eq $windowClient.Index)) {
                        $botStartedAt = Get-Date
                        Write-QaInfo ('Scenario window starts now: ' + $ScenarioTimeoutSec + 's (load took ' + [int]($botStartedAt - $cStart).TotalSeconds + 's)')
                    }
                }
            }
            if ($null -eq $c.VerdictLine) {
                $c.VerdictLine = Find-MpLogLine -LogPath $c.Log -Pattern $botVerdictPattern
                if ($c.VerdictLine) { Write-QaInfo ('Bot ' + $c.Index + ': ' + $c.VerdictLine) }
            }
        }
        if ($null -eq $serverAckLine) {
            $serverAckLine = Find-MpLogLine -LogPath $serverLog -Pattern $botServerAckPattern -SkipLines $serverSkipLines
            if ($serverAckLine) { Write-QaInfo ('Server: ' + $serverAckLine) }
        }
        $allVerdicts = $true
        foreach ($c in $clientRecs) { if ($null -eq $c.VerdictLine) { $allVerdicts = $false } }
        if ($allVerdicts -and $serverAckLine) { break }

        if ($null -eq $itemTableLine) {
            foreach ($c in $clientRecs) {
                $hit = Find-MpLogLine -LogPath $c.Log -Pattern 'Inconsistent item table on Slave connection'
                if (-not $hit) { continue }
                $itemTableLine = $hit
                Write-QaError ('Replication item-table divergence on client ' + $c.Index + ': ' + $hit)
                $outcomeCode = $codes.Compile
                $outcomeReason = ('inconsistent item table on client ' + $c.Index + ': ' + $hit)
                break
            }
            if ($itemTableLine) { break }
        }

        $deadClient = $null
        foreach ($c in $clientRecs) {
            if ((@($c.Pids).Count -gt 0) -and (-not (Test-QaAnyPidAlive -Pids $c.Pids)) -and ($null -eq $c.VerdictLine)) {
                $deadClient = $c
                break
            }
        }
        if ($deadClient) {
            $outcomeReason = ('client ' + $deadClient.Index + ' process exited before a verdict')
            if ($null -eq $deadClient.BotConnectedLine) {
                $silentExit = ('client ' + $deadClient.Index + ' exited before its bot armed')
            }
            break
        }

        $win = Get-QaMpWindowState -LaunchTime $cStart -BotStartedAt $botStartedAt -LoadTimeoutSec $LoadTimeoutSec -ScenarioTimeoutSec $ScenarioTimeoutSec -Now (Get-Date)
        if ($win.Expired) {
            $windowExpiredReason = $win.Reason
            $outcomeReason = $win.Reason
            break
        }
        if (((Get-Date) - $lastNote).TotalSeconds -ge 30) {
            $lastNote = Get-Date
            $states = New-Object System.Collections.ArrayList
            foreach ($c in $clientRecs) {
                $s = 'joining'
                if ($c.VerdictLine) { $s = 'reported' }
                elseif ($c.ConnectedLine) { $s = 'connected' }
                [void]$states.Add('c' + $c.Index + '=' + $s)
            }
            Write-QaInfo ('  ... ' + [int]((Get-Date) - $cStart).TotalSeconds + 's elapsed, ' + $win.Phase + ' (' + ($states.ToArray() -join ', ') + ')')
        }
    }
} catch {
    Write-QaError $_.Exception.Message
} finally {
    if ($KeepServer -and (-not $ReuseServer) -and (-not $serverKept)) { $stopServerNow = $true }
    $cleanupFailures = New-Object System.Collections.ArrayList
    Invoke-QaCleanupStage -Name 'the machine sampler' -Errors $cleanupFailures -Action {
        Stop-QaMachineSampler -Sampler $machineSampler
    }
    $machineSampler = $null
    Write-QaStep 'Teardown (token-matched kills only)'
    Invoke-QaCleanupStage -Name 'the recorders' -Errors $cleanupFailures -Action {
        foreach ($c in $clientRecs) {
            if ($null -eq $c.Capture) { continue }
            $ffWarn = Stop-QaCapture -Capture $c.Capture
            if ($ffWarn) { Write-QaInfo ('Client ' + $c.Index + ' ffmpeg: ' + $ffWarn) }
        }
    }
    Invoke-QaCleanupStage -Name 'the game engines' -Errors $cleanupFailures -Action {
        foreach ($c in $clientRecs) {
            if (@($c.Pids).Count -eq 0) { continue }
            $stop = Stop-QaWorkbench -Pids $c.Pids -GraceSec $GraceSec
            Write-QaInfo ('Client ' + $c.Index + ' stopped (graceful=' + $stop.Graceful + ')')
        }
        if ($stopServerNow -and (@($serverPids).Count -gt 0)) {
            $stop = Stop-QaVerifiedWorkbench -Pids $serverPids -Identities $serverPidIdentities -GraceSec $GraceSec
            if (@($stop.RefusedAlive).Count -gt 0) {
                Write-QaWarn ('Kept server PID(s) ' + (@($stop.RefusedAlive) -join ', ') + ' recycled; not stopped.')
            }
            Write-QaInfo ('Server stopped (graceful=' + $stop.Graceful + ')')
        }
        if ($stopServerNow -and ($KeepServer -or $ReuseServer) -and (Test-Path -LiteralPath $sharedStatePath)) { Remove-Item -LiteralPath $sharedStatePath -Force -ErrorAction SilentlyContinue }
    }
    $tokens = New-Object System.Collections.ArrayList
    if ($stopServerNow) { [void]$tokens.Add($serverProfile) }
    foreach ($c in $clientRecs) { [void]$tokens.Add($c.Profile) }
    foreach ($tok in $tokens.ToArray()) {
        $left = @(Get-QaWorkbenchProcesses -NamePrefix 'ArmaReforger' -OwnedByProfile $tok)
        foreach ($p in $left) {
            try { & taskkill.exe /PID $p.ProcessId /T /F | Out-Null } catch {}
        }
    }
    Start-Sleep -Seconds 2

    if ((-not $NoReapOrphans) -and $stopServerNow) {
        $reap = Stop-QaMpOrphanEngines -WorktreePath $WorktreePath -OwnedTokens @($tokens.ToArray())
        if ($reap.Killed -gt 0) { Write-QaInfo ('Reaped ' + $reap.Killed + ' leftover engine process(es).') }
        $lateReap = Invoke-QaOrphanReap -WorktreePath $WorktreePath -ExcludeRunDir $ctx.RunDir -GraceSec $GraceSec
        foreach ($r in @($lateReap.Reaped)) {
            Write-QaWarn ('Reclaimed orphaned run PID(s) ' + (@($r.Pids) -join ', '))
        }
        if (@($reap.Survivors).Count -gt 0) {
            Write-QaError ('REAP FAILED: engine pid(s) ' + (@($reap.Survivors) -join ', ') + ' survived.')
        }
    }

    if (-not $stopServerNow) {
        Write-QaInfo 'Server and rig lock kept for next scenario.'
    } elseif (Test-QaMpPortFree -Port 2001) {
        Remove-QaMpRigLock
    } else {
        $holders = @(Get-QaMpPortHolders -Port 2001)
        $who = '(unknown)'; if ($holders.Count -gt 0) { $who = ($holders -join ', ') }
        [void](Set-QaMpRigLockRetained -Why 'port')
        Write-QaError ('NOT releasing MP rig lock: UDP 2001 still held by pid(s) ' + $who + '.')
    }
}

# ------------------------------------------------------------------------------------------------
# Post-mortem
# ------------------------------------------------------------------------------------------------
Write-QaStep 'Collecting artifacts'
if ($null -eq $serverLog) { $serverLog = Find-MpConsoleLog -ProfileDir $serverProfile -After $startTime.AddSeconds(-5) }
foreach ($c in $clientRecs) { if ($null -eq $c.Log) { $c.Log = Find-MpConsoleLog -ProfileDir $c.Profile } }

$sides = New-Object System.Collections.ArrayList
[void]$sides.Add(@('server', $serverLog))
foreach ($c in $clientRecs) {
    $name = 'client'
    if ($Clients -gt 1) { $name = 'client' + $c.Index }
    [void]$sides.Add(@($name, $c.Log))
}
$collected = New-Object System.Collections.ArrayList
foreach ($pair in $sides.ToArray()) {
    $side = $pair[0]; $log = $pair[1]
    if ($log -and (Test-Path -LiteralPath $log)) {
        Copy-Item -LiteralPath $log -Destination (Join-Path $ctx.RunDir ($side + '-console.log')) -Force -ErrorAction SilentlyContinue
        [void]$collected.Add($side + '-console.log')
        $err = Join-Path (Split-Path -Parent $log) $errorLogName
        if (Test-Path -LiteralPath $err) {
            Copy-Item -LiteralPath $err -Destination (Join-Path $ctx.RunDir ($side + '-error.log')) -Force -ErrorAction SilentlyContinue
            [void]$collected.Add($side + '-error.log')
        }
    } else {
        Write-QaWarn ('No console.log found for ' + $side + '.')
    }
}
if ($collected.Count -gt 0) { Write-QaInfo ('Collected  : ' + ($collected.ToArray() -join ', ')) }

foreach ($c in $clientRecs) {
    if ($null -eq $c.VerdictLine)      { $c.VerdictLine      = Find-MpLogLine -LogPath $c.Log -Pattern $botVerdictPattern }
    if ($null -eq $c.BotConnectedLine) { $c.BotConnectedLine = Find-MpLogLine -LogPath $c.Log -Pattern $botConnectedPattern }
    if ($null -eq $c.ConnectedLine)    { $c.ConnectedLine    = Find-MpLogLine -LogPath $c.Log -Pattern $connectedPattern }
}
if ($null -eq $serverAckLine) { $serverAckLine = Find-MpLogLine -LogPath $serverLog -Pattern $botServerAckPattern -SkipLines $serverSkipLines }

$expectedErrorCfg = Get-QaProp -Object $mpCfg -Name 'expectedErrors'
$expectedErrorPatterns = New-Object System.Collections.ArrayList
foreach ($key in @('all', $Scenario)) {
    foreach ($pat in @(Get-QaProp -Object $expectedErrorCfg -Name $key -Default @())) {
        if (-not [string]::IsNullOrWhiteSpace($pat)) { [void]$expectedErrorPatterns.Add([string]$pat) }
    }
}
foreach ($pat in @($ExpectedError)) {
    if (-not [string]::IsNullOrWhiteSpace($pat)) { [void]$expectedErrorPatterns.Add([string]$pat) }
}

function Test-MpExpectedError {
    param([string]$Text, [string[]]$Patterns)
    foreach ($p in @($Patterns)) { if ($Text -match $p) { return $true } }
    return $false
}

$compileErrors = 0
$expectedErrorHits = 0
$addonResIssues = New-Object System.Collections.ArrayList
$scanLogs = New-Object System.Collections.ArrayList
[void]$scanLogs.Add($serverLog)
foreach ($c in $clientRecs) { [void]$scanLogs.Add($c.Log) }
foreach ($log in $scanLogs.ToArray()) {
    if ($log -and (Test-Path -LiteralPath $log)) {
        $scanLines = @(Read-QaLines -Path $log)
        if (($log -eq $serverLog) -and ($serverSkipLines -gt 0)) { $scanLines = @($scanLines | Select-Object -Skip $serverSkipLines) }
        $f = Get-QaLogFindings -Lines $scanLines -Patterns $patterns
        if (($log -ne $serverLog) -or ($serverSkipLines -eq 0)) {
            $r = Test-QaAddonResolution -Findings $f -WorktreePath $WorktreePath -StageAddonLink $stageInfo.AddonLink -AddonGuid $addonGuid -VanillaGuid $vanillaGuid
            if (-not $r.Ok) {
                foreach ($iss in $r.Issues) { [void]$addonResIssues.Add($iss) }
            }
        }
        foreach ($d in @(Get-QaUniqueDiagnostics -Diagnostics @($f.Errors))) {
            $text = [string]$d.Raw
            if ([string]::IsNullOrWhiteSpace($text)) { $text = [string]$d }
            if (Test-MpExpectedError -Text $text -Patterns $expectedErrorPatterns.ToArray()) {
                $expectedErrorHits++
            } else {
                $compileErrors++
            }
        }
    }
}

$clientPassed = $true
$firstFailVerdict = $null
$missingVerdictClient = $null
$failedIndexes = New-Object System.Collections.ArrayList
$missingIndexes = New-Object System.Collections.ArrayList
foreach ($c in $clientRecs) {
    if ($null -eq $c.VerdictLine) {
        $clientPassed = $false
        [void]$missingIndexes.Add($c.Index)
        if ($null -eq $missingVerdictClient) { $missingVerdictClient = $c }
        continue
    }
    if ($c.VerdictLine -match '\| VERDICT \| PASS \|') { continue }
    $clientPassed = $false
    [void]$failedIndexes.Add($c.Index)
    if ($null -eq $firstFailVerdict) { $firstFailVerdict = $c }
}
$anyVerdict = $false
foreach ($c in $clientRecs) { if ($c.VerdictLine) { $anyVerdict = $true } }

$serverPassed = $false
if ($serverAckLine -and ($serverAckLine -match ('\| ' + $Scenario + ' \| PASS \|'))) { $serverPassed = $true }

$replFailLine = $null
if ($serverLog -and (Test-Path -LiteralPath $serverLog)) {
    $replFailLine = Find-MpLogLine -LogPath $serverLog -Pattern 'Unable to start replication'
}

$checksumHint = $null
foreach ($logPath in @($serverLog) + @($clientRecs | ForEach-Object { $_.Log })) {
    if (-not $checksumHint) { $checksumHint = Get-QaProtocolChecksumHint -Line (Find-MpLogLine -LogPath $logPath -Pattern 'script protocol checksum does not match') }
}

if ($checksumHint) {
    $outcomeCode = $codes.Setup
    $outcomeReason = ('VOID - ' + $checksumHint)
} elseif ($addonResIssues.Count -gt 0) {
    $outcomeCode = $codes.Setup
    $outcomeReason = ('Addon resolution check failed: ' + ($addonResIssues.ToArray() -join ' | '))
} elseif ($replFailLine) {
    $outcomeCode = $codes.Setup
    $outcomeReason = ('VOID - server could not start replication on UDP 2001: ' + $replFailLine)
} elseif ($clientPassed -and $serverPassed -and ($compileErrors -eq 0)) {
    $sidesLabel = 'BOTH sides'
    if ($Clients -gt 1) { $sidesLabel = 'ALL ' + $Clients + ' clients + the server' }
    $outcomeCode = $codes.Clean
    $outcomeReason = ("scenario '" + $Scenario + "' PASSED on " + $sidesLabel)
} elseif ($itemTableLine) {
    $outcomeCode = $codes.Compile
    $outcomeReason = ('inconsistent item table: ' + $itemTableLine)
} elseif ($firstFailVerdict) {
    $outcomeCode = $codes.Compile
    $outcomeReason = ('bot verdict FAIL on client ' + $firstFailVerdict.Index + ': ' + $firstFailVerdict.VerdictLine)
} elseif ($serverAckLine -and (-not $serverPassed)) {
    $outcomeCode = $codes.Compile
    $outcomeReason = ('server SERVER_ACK FAIL: ' + $serverAckLine)
} elseif ($windowExpiredReason -and (-not $anyVerdict)) {
    $outcomeCode = $codes.Hang
    $outcomeReason = ($windowExpiredReason + ' - absent verdict is a failure')
} elseif ($compileErrors -gt 0) {
    $outcomeCode = $codes.Compile
    $outcomeReason = ($compileErrors.ToString() + ' unexpected script error(s) in run logs')
} elseif ($anyVerdict -and $missingVerdictClient -and $serverAckLine) {
    $outcomeCode = $codes.Compile
    $outcomeReason = ('client ' + $missingVerdictClient.Index + " never reported VERDICT for '" + $Scenario + "'")
} elseif ($anyVerdict -and (-not $serverAckLine)) {
    $outcomeCode = $codes.Compile
    $outcomeReason = ("client verdict present but NO server SERVER_ACK for '" + $Scenario + "'")
} elseif ($serverAckLine -and (-not $anyVerdict)) {
    $outcomeCode = $codes.Compile
    $outcomeReason = ("server SERVER_ACK present but NO client VERDICT for '" + $Scenario + "'")
} elseif ($outcomeCode -eq $codes.Hang) {
    $outcomeCode = $codes.Hang
    $outcomeReason = ("NEITHER side reported a verdict for '" + $Scenario + "' (" + $outcomeReason + ")")
}

if ($silentExit -and ($outcomeCode -ne $codes.Clean) -and (-not $anyVerdict) -and (-not $serverAckLine) -and (-not $replFailLine) -and ($compileErrors -eq 0)) {
    $outcomeCode = $codes.Inconclusive
    $outcomeReason = ('INCONCLUSIVE - ' + $silentExit)
}

$machineSamples = @(Read-QaMachineSamples -Csv (Join-Path $ctx.RunDir 'machine.csv'))
$fpsSamples = @()
foreach ($c in $clientRecs) { if ($c.Log) { $fpsSamples += @(Read-QaFpsSamples -LogPath $c.Log -Day $startTime) } }
$machineStruggle = $null
if ($outcomeCode -ne $codes.Clean) {
    $failAt = Get-Date
    foreach ($c in $clientRecs) {
        if (-not $c.Log) { continue }
        foreach ($e in @(Get-QaBotEvents -LogPath $c.Log -Day $startTime)) {
            if ($e.Kind -eq 'beat' -and $e.State -eq 'FAIL' -and $e.Time -lt $failAt) { $failAt = $e.Time }
        }
    }
    $machineStruggle = Get-QaMachineStruggle -Samples $machineSamples -FpsSamples $fpsSamples -Around $failAt -Config $machineCfg
    if ($machineStruggle) { $outcomeReason = ('LOW CONFIDENCE - ' + $machineStruggle + ' ' + $outcomeReason) }
}
$memAtEnd = Get-QaMemorySnapshot

$runOutcomeWord = 'failed'
if ($outcomeCode -eq $codes.Clean) { $runOutcomeWord = 'passed' }
foreach ($c in $clientRecs) {
    if ($null -eq $c.Capture) {
        if ($c.Record -and $c.RecordStatus -eq 'not requested') { $c.RecordStatus = 'no client window appeared' }
        continue
    }
    try {
        $c.RecordStatus = Complete-QaCapture -Capture $c.Capture -ClientLog $c.Log -OutDir $ctx.RunDir -BaseName ('client' + $c.Index) -KeepBody:($Clients -gt 1) -CodeVersion $gitStamp.Text -RunOutcome $runOutcomeWord
    } catch {
        $c.RecordStatus = ('encode failed: ' + $_.Exception.Message)
    }
    Write-QaInfo ('Client ' + $c.Index + ' video: ' + $c.RecordStatus)
}

$combinedStatus = ''
$bodies = @()
foreach ($c in $clientRecs) {
    $body = Join-Path $ctx.RunDir ('client' + $c.Index + '.body.mp4')
    if (($null -ne $c.Capture) -and (Test-Path -LiteralPath $body) -and ($null -ne $c.Capture.VideoStart)) {
        $bodies += @(@{ Path = $body; Start = $c.Capture.VideoStart })
    }
}
if (@($bodies).Count -ge 2) {
    try {
        if (New-QaSideBySide -Videos $bodies -OutPath (Join-Path $ctx.RunDir 'combined.mp4')) {
            $combinedStatus = 'combined.mp4 (' + @($bodies).Count + ' clients side by side)'
        } else {
            $combinedStatus = 'side-by-side encode failed'
        }
    } catch {
        $combinedStatus = ('side-by-side failed: ' + $_.Exception.Message)
    }
}
foreach ($c in $clientRecs) { Remove-Item -LiteralPath (Join-Path $ctx.RunDir ('client' + $c.Index + '.body.mp4')) -Force -ErrorAction SilentlyContinue }

$failureNoteStatus = ''
if ($outcomeCode -ne $codes.Clean) {
    try {
        $noteClients = @($clientRecs | ForEach-Object { @{ Index = $_.Index; Log = $_.Log } })
        Write-QaFailureNote -OutPath (Join-Path $ctx.RunDir 'why-it-failed.txt') -Scenario $Scenario -Reason $outcomeReason -Clients $noteClients -ServerLog $serverLog -ServerSkipLines $serverSkipLines -Day $startTime
        $failureNoteStatus = 'why-it-failed.txt'
    } catch {
        $failureNoteStatus = ('could not write failure note: ' + $_.Exception.Message)
    }
}

$gitStampAtStamp = Get-QaGitStamp -WorktreePath $WorktreePath
$checkoutChanged = ($gitStampAtStamp.Commit -ne $gitStamp.Commit) -or ((@($gitStampAtStamp.Dirty) -join ',') -ne (@($gitStamp.Dirty) -join ','))
$uncommittedField = 'none'
if ($checkoutChanged) { $uncommittedField = 'checkout changed during run' }
elseif (@($gitStamp.Dirty).Count -gt 0) { $uncommittedField = (@($gitStamp.Dirty) -join ', ') }

$header = [ordered]@{
    'Timestamp'       = $startTime.ToString('yyyy-MM-dd HH:mm:ss')
    'Worktree'        = $WorktreePath
    'StageDir'        = $stageInfo.AddonsDir
    'Engine'          = $Engine
    'Commit'          = $(if ($gitStamp.Commit) { $gitStamp.Commit } else { '(unknown)' })
    'Uncommitted'     = $uncommittedField
    'ServerExe'       = $serverExePath
    'ClientExe'       = $clientExePath
    'ServerReady'     = $(if ($serverReadyLine) { $serverReadyLine } else { '(never)' })
    'Clients'         = $Clients
    'ServerAck'       = $(if ($serverAckLine) { $serverAckLine } else { '(never)' })
    'BotScenario'     = $Scenario
    'Scenario'        = ($worldPath + '  +  ' + $missionHeader)
    'ItemTable'       = $(if ($itemTableLine) { 'DIVERGED: ' + $itemTableLine } else { 'consistent' })
    'ScriptErrors'    = $compileErrors
    'ExpectedErrs'    = $expectedErrorHits
    'FreeRamStart'    = $(if ($null -ne $memAtStart) { [string]$memAtStart.FreeMb + ' MB' } else { 'unavailable' })
    'FreeRamEnd'      = $(if ($null -ne $memAtEnd) { [string]$memAtEnd.FreeMb + ' MB' } else { 'unavailable' })
    'FreeRamDelta'    = Format-QaMemoryDelta -Start $memAtStart -End $memAtEnd
    'ServerLog'       = $(if ($serverLog) { $serverLog } else { '(none)' })
    'ExitCode'        = $outcomeCode
    'Reason'          = $outcomeReason
    'Combined'        = $(if ($combinedStatus) { $combinedStatus } else { '(single client)' })
    'Machine'         = (Format-QaMachineSummary -Samples $machineSamples -FpsSamples $fpsSamples)
    'MachineGate'     = $(if ($machineGate) { $machineGate.Note } else { '(not gated)' })
    'MachineStruggle' = $(if ($machineStruggle) { $machineStruggle } else { 'no' })
    'FailureNote'     = $(if ($failureNoteStatus) { $failureNoteStatus } else { '(passed)' })
}
foreach ($c in $clientRecs) {
    $p = 'C' + $c.Index
    $header[$p + 'Join']    = $(if ($c.ConnectedLine)    { $c.ConnectedLine }    else { '(never)' })
    $header[$p + 'Bot']     = $(if ($c.BotConnectedLine) { $c.BotConnectedLine } else { '(never)' })
    $header[$p + 'Verdict'] = $(if ($c.VerdictLine)      { $c.VerdictLine }      else { '(none)' })
    $header[$p + 'Log']     = $(if ($c.Log)              { $c.Log }              else { '(none)' })
    if ($c.Record) { $header[$p + 'Video'] = $c.RecordStatus }
}
$body = @()
foreach ($k in $header.Keys) { $body += ('  ' + $k.PadRight(15) + ': ' + $header[$k]) }
Write-QaSummary -Path (Join-Path $ctx.RunDir 'summary.txt') -Header $header -Body @()

$historyResult = 'fail'
if ($outcomeCode -eq $codes.Clean) { $historyResult = 'pass' }
elseif ($outcomeCode -eq $codes.Setup) { $historyResult = 'setup' }
elseif ($outcomeCode -eq $codes.Inconclusive) { $historyResult = 'inconclusive' }
[void](Add-QaRunHistory -Record @{
    Time = (Get-Date).ToString('s'); Scenario = $Scenario; Clients = $Clients; Engine = $Engine
    Commit = $(if ($checkoutChanged) { '' } else { $gitStamp.Short }); Uncommitted = @($gitStamp.Dirty).Count
    Branch = $mpBranch; Worktree = $WorktreePath
    Result = $historyResult; ExitCode = $outcomeCode; MachineStruggled = [bool]$machineStruggle; Struggle = [string]$machineStruggle
    Machine = (Format-QaMachineSummary -Samples $machineSamples -FpsSamples $fpsSamples); RunDir = $ctx.RunDir
})

Write-Host ''
foreach ($l in $body) { Write-Host $l }
Write-QaInfo ('Artifacts  : ' + $ctx.RunDir)

if (-not $KeepProfile) {
    $pruned = Remove-QaOldRuns -RunsRoot $ctx.RunsRoot -Keep $KeepRuns
    if (@($pruned).Count -gt 0) { Write-QaInfo ('Pruned ' + @($pruned).Count + ' old run(s).') }
}

Write-Host ''
if ($outcomeCode -eq $codes.Clean) {
    Write-Host ('MP RIG PASS (exit 0): ' + $outcomeReason) -ForegroundColor Green
} elseif ($outcomeCode -eq $codes.Inconclusive) {
    Write-Host ('MP RIG INCONCLUSIVE (exit ' + $outcomeCode + '): ' + $outcomeReason) -ForegroundColor Yellow
} else {
    Write-Host ('MP RIG FAIL (exit ' + $outcomeCode + '): ' + $outcomeReason) -ForegroundColor Red
}
exit $outcomeCode
