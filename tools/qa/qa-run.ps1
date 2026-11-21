<#
.SYNOPSIS
    BGONE single-addon worktree QA compile gate (PowerShell 5.1 ONLY).

.DESCRIPTION
    Stage 0 (optional): runs tools\lint\lint-run.ps1 if present in the worktree (gracefully skips if absent).
    Stage 1 (compile gate): launches Workbench against the external staging junction
    (C:\wt\.bgone-stage\<sha1-8>\Addons\BGONE -> <WorktreePath>) with an isolated per-run profile,
    waits for script compilation to finish, terminates Workbench, and judges strictly from console.log.

    Compatible with MRS-GC's shared queue (rig_queue.py / queue-run.ps1), including -CompileFrom and -RunDirFile.

.NOTES
    Exit codes: 0 clean | 1 compile/lint errors | 2 launch/setup failure | 3 hang.
    PowerShell 5.1 compatible: no &&/||, no ternary, no null-coalescing.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$WorktreePath,

    [string]$Module,
    [string]$Engine = '',
    [string]$CompileGate,
    [string]$WbExe,
    [string]$GameDir,

    [int]$TimeoutSec = 0,
    [int]$QuietSec = 0,
    [int]$SettledQuietSec = 0,
    [int]$GraceSec = 0,
    [int]$KeepRuns = 0,

    [switch]$KeepProfile,
    [switch]$Queue,
    [int]$QueueTimeoutSec = 0,
    [switch]$IgnoreInteractiveWorkbench,
    [switch]$Clean,
    [switch]$NoGitExclude,
    [switch]$AllowUnsafeWorktree,
    [switch]$AcceptEngineVersion,
    [switch]$DiscardRdb,
    [switch]$NoLint,

    # Prep-lane marker checked verbatim by rig_queue.py and queue-run.ps1.
    [string]$CompileFrom,
    [string]$RunDirFile,

    [Alias('DryRun')]
    [switch]$WhatIf,

    [switch]$SelfTest,
    [string]$ParseLog,
    [string]$ParseReport,
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
. (Join-Path $libDir 'QaSweep.ps1')
. (Join-Path $libDir 'QaSelfTest.ps1')

$codes = Get-QaExitCodes

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

# ------------------------------------------------------------------------------------------------
# Mode: -SelfTest (offline)
# ------------------------------------------------------------------------------------------------
if ($SelfTest) {
    Write-Host 'BGONE QA runner - self-test (no Workbench launched)'
    $rc = Invoke-QaSelfTest -Patterns $patterns
    exit $rc
}

# ------------------------------------------------------------------------------------------------
# Mode: -ParseLog (offline)
# ------------------------------------------------------------------------------------------------
if (-not [string]::IsNullOrWhiteSpace($ParseLog)) {
    if (-not (Test-Path -LiteralPath $ParseLog)) {
        Write-QaError ("Log not found: " + $ParseLog)
        exit $codes.Setup
    }
    Write-QaStep ("Parsing " + $ParseLog)
    $lines = Read-QaLines -Path $ParseLog
    $findings = Get-QaLogFindings -Lines $lines -Patterns $patterns

    if (-not [string]::IsNullOrWhiteSpace($WorktreePath)) {
        [void](Add-QaDiagnosticOwners -Diagnostics $findings.Errors -WorktreePath $WorktreePath -AddonName 'BGONE')
    }

    $report = $null
    if (-not [string]::IsNullOrWhiteSpace($ParseReport)) {
        $report = Read-QaTestReport -Path $ParseReport
    }

    $outcome = Resolve-QaOutcome -Findings $findings
    foreach ($l in (Format-QaRunReport -Findings $findings -Outcome $outcome -Report $report)) { Write-Host $l }
    exit $outcome.ExitCode
}

# ------------------------------------------------------------------------------------------------
# Validate single-addon worktree (<WorktreePath>\addon.gproj)
# ------------------------------------------------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($WorktreePath)) {
    Write-QaError 'No -WorktreePath given. Try: qa-run.ps1 -WorktreePath C:\wt\bgone-wt   (or -SelfTest / -ParseLog <console.log>)'
    exit $codes.Setup
}
if (-not (Test-Path -LiteralPath $WorktreePath)) {
    Write-QaError ("Worktree path does not exist: " + $WorktreePath)
    exit $codes.Setup
}
$WorktreePath = (Resolve-Path -LiteralPath $WorktreePath).ProviderPath

if ($Clean) {
    Write-QaStep 'Cleaning .qa artifacts'
    $did = Remove-QaArtifacts -WorktreePath $WorktreePath
    if ($did) { Write-QaInfo ('Removed ' + (Join-Path $WorktreePath '.qa')) } else { Write-QaInfo 'Nothing to clean.' }
    exit $codes.Clean
}

$rootGproj = Join-Path $WorktreePath 'addon.gproj'
if (-not (Test-Path -LiteralPath $rootGproj)) {
    Write-QaError ("Not a valid BGONE single-addon worktree - missing root addon.gproj: " + $rootGproj)
    exit $codes.Setup
}

$addonIds = @(Get-QaAddonIds -AddonsDir $WorktreePath)
if ($addonIds.Count -eq 0) {
    Write-QaError ("Could not read addon ID from " + $rootGproj)
    exit $codes.Setup
}
$addonId = [string]$addonIds[0]
$addonGuid = Get-QaAddonGuid -GprojPath $rootGproj
if ([string]::IsNullOrWhiteSpace($addonGuid)) {
    $addonGuid = [string](Get-QaProp -Object $defaults -Name 'addonGuid' -Default '5F1EE615E7AE3106')
}
$Module = Get-QaOrDefault -Value $Module -Fallback $addonId

# Optional Stage 0 lint (gracefully skips if tools\lint\lint-run.ps1 is absent).
$lintScript = Join-Path $WorktreePath 'tools\lint\lint-run.ps1'
if (-not (Test-Path -LiteralPath $lintScript)) { $lintScript = $null }

# ------------------------------------------------------------------------------------------------
# Resolve engine, exe, game dir, timings
# ------------------------------------------------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($CompileGate)) {
    $CompileGate = [string](Get-QaProp -Object $defaults -Name 'gate' -Default 'workbench')
}

try {
    $engineCfg = Get-QaEngineConfig -Config $config -Engine $Engine
    $gateCfg   = Get-QaGateConfig   -EngineConfig $engineCfg -Gate $CompileGate -EngineName $Engine
    $exePath   = Resolve-QaWorkbenchExe -EngineConfig $gateCfg -Override $WbExe -EngineName ($Engine + '/' + $CompileGate)
    $gameRoot  = Resolve-QaGameDir      -EngineConfig $gateCfg -Override $GameDir -EngineName ($Engine + '/' + $CompileGate)
} catch {
    Write-QaError $_.Exception.Message
    exit $codes.Setup
}

$validatedVersion = [string](Get-QaProp -Object $engineCfg -Name 'validatedVersion' -Default '')
$versionProblem = Get-QaEngineVersionProblem -Engine $Engine -ValidatedVersion $validatedVersion -Exes @($exePath)
if ($versionProblem) {
    if ($WhatIf -or $AcceptEngineVersion) { Write-QaWarn $versionProblem } else { Write-QaError $versionProblem; exit $codes.Setup }
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

$TimeoutSec      = [int](Get-QaOrDefault -Value $TimeoutSec      -Fallback (Get-QaProp -Object $defaults -Name 'timeoutSec'      -Default 1200))
$QuietSec        = [int](Get-QaOrDefault -Value $QuietSec        -Fallback (Get-QaProp -Object $defaults -Name 'quietSec'        -Default 60))
$SettledQuietSec = [int](Get-QaOrDefault -Value $SettledQuietSec -Fallback (Get-QaProp -Object $defaults -Name 'settledQuietSec' -Default 10))
$GraceSec        = [int](Get-QaOrDefault -Value $GraceSec        -Fallback (Get-QaProp -Object $defaults -Name 'graceSec'        -Default 10))
$KeepRuns        = [int](Get-QaOrDefault -Value $KeepRuns        -Fallback (Get-QaProp -Object $defaults -Name 'keepRuns'        -Default 20))
$QueueTimeoutSec = [int](Get-QaOrDefault -Value $QueueTimeoutSec -Fallback (Get-QaProp -Object $defaults -Name 'queueTimeoutSec' -Default 14400))
$pollSec         = [int](Get-QaProp -Object $defaults -Name 'pollSec'       -Default 2)
$pidResolveSec   = [int](Get-QaProp -Object $defaults -Name 'pidResolveSec' -Default 20)
$procPrefix      = [string](Get-QaProp -Object $gateCfg  -Name 'processNamePrefix' -Default 'ArmaReforgerWorkbench')
$logFileName     = [string](Get-QaProp -Object $defaults -Name 'logFileName'  -Default 'console.log')
$errorLogName    = [string](Get-QaProp -Object $defaults -Name 'errorLogFileName' -Default 'error.log')
$rdbGlob         = [string](Get-QaProp -Object $defaults -Name 'rdbChurnGlob' -Default 'resourceDatabase.rdb')
$reportNames     = @(Get-QaProp -Object $defaults -Name 'reportFileNames' -Default @('junit.xml'))
$reportExcludes  = @(Get-QaProp -Object $defaults -Name 'reportExcludePatterns' -Default @())
$fallbackLogRoots= @(Get-QaProp -Object $defaults -Name 'fallbackLogRoots' -Default @())
$concurrency     = [string](Get-QaProp -Object $defaults -Name 'concurrency' -Default 'concurrent')
$vanillaGuid     = [string](Get-QaProp -Object $defaults -Name 'vanillaAddonGuid' -Default '58D0FB3206B6F859')
$addonEnforce    = [string](Get-QaProp -Object $defaults -Name 'addonResolutionEnforce' -Default 'fail')

if ($DiscardRdb) { Write-QaDiscardRdbDeprecated }
if (-not $WhatIf) { [void](Set-QaRdbSkipWorktree -WorktreePath $WorktreePath -PathSpec $rdbGlob) }

# ------------------------------------------------------------------------------------------------
# External staging junction + run context
# ------------------------------------------------------------------------------------------------
$stageInfo = Get-QaStageInfo -WorktreePath $WorktreePath -AddonName $addonId
$startTime = Get-Date
$memConfig = Get-QaMemoryConfig -Defaults $defaults
$memAtStart = Get-QaMemorySnapshot
$profileSlot = $Engine
if ($CompileGate -ne 'workbench') { $profileSlot = $Engine + '-' + $CompileGate }
$ctx = New-QaRunContext -WorktreePath $WorktreePath -Engine $profileSlot -StartTime $startTime

# -CompileFrom support for rig_queue.py prep-lane reuse.
$compileFromLog = $null
if (-not [string]::IsNullOrWhiteSpace($CompileFrom)) {
    $fromProblem = ''
    $fromSummary = Join-Path $CompileFrom 'summary.txt'
    $fromLog = Join-Path $CompileFrom 'console.log'
    if (-not ((Test-Path -LiteralPath $fromSummary) -and (Test-Path -LiteralPath $fromLog))) {
        $fromProblem = 'no summary.txt and console.log in ' + $CompileFrom
    } else {
        $fromFields = @{}
        foreach ($l in @(Get-Content -LiteralPath $fromSummary -Encoding UTF8)) {
            if ($l -match '^(?<k>[A-Za-z]+)\s*:\s*(?<v>.*?)\s*$') { if (-not $fromFields.ContainsKey($Matches.k)) { $fromFields[$Matches.k] = $Matches.v } }
        }
        $fromWorktree = [string]$fromFields['Worktree']
        if ((Resolve-QaFullPath -Path $fromWorktree).TrimEnd('\') -ne (Resolve-QaFullPath -Path $WorktreePath).TrimEnd('\')) {
            $fromProblem = 'it compiled a different worktree (' + $fromWorktree + ')'
        } elseif ([string]$fromFields['Stage'] -ne 'compile') {
            $fromProblem = 'it was not a compile-only run (stage: ' + [string]$fromFields['Stage'] + ')'
        } elseif ([string]$fromFields['ExitCode'] -ne '0') {
            $fromProblem = 'it did not pass (exit ' + [string]$fromFields['ExitCode'] + ')'
        } elseif ([string]$fromFields['Engine'] -ne $Engine) {
            $fromProblem = 'it compiled for engine ' + [string]$fromFields['Engine'] + ', not ' + $Engine
        }
    }
    if ($fromProblem -ne '') {
        Write-QaError ('-CompileFrom cannot be used: ' + $fromProblem + '.')
        exit $codes.Setup
    }
    $compileFromLog = $fromLog
}

$tokens = @{
    '{GPROJ}'     = $stageInfo.GprojPath
    '{ADDONSDIR}' = $stageInfo.AddonsDir
    '{ADDONLIST}' = ($addonIds -join ',')
    '{PROFILE}'   = $ctx.ProfileDir
    '{WBMODULE}'  = [string](Get-QaProp -Object $gateCfg -Name 'wbModule' -Default 'ResourceManager')
    '{WORKTREE}'  = $WorktreePath
    '{MODULE}'    = $Module
}
$argList = Build-QaArgumentList -EngineConfig $gateCfg -Tokens $tokens -Autotest $false
$cmdLine = Format-QaCommandLine -Exe $exePath -Arguments $argList -WorkingDirectory $gameRoot

Write-QaStep 'BGONE worktree QA compile gate'
Write-QaInfo ('Worktree   : ' + $WorktreePath)
Write-QaInfo ('Addon      : ' + $addonId + ' (GUID ' + $addonGuid + ')')
Write-QaInfo ('Stage dir  : ' + $stageInfo.AddonsDir + '  (junction ' + $stageInfo.AddonLink + ' -> ' + $WorktreePath + ')')
Write-QaInfo ('Engine     : ' + $Engine + ' (validated ' + $validatedVersion + ')   Gate: ' + $CompileGate)
Write-QaInfo ('Executable : ' + $exePath)
Write-QaInfo ('Game dir   : ' + $gameRoot)
Write-QaInfo ('Profile    : ' + $ctx.ProfileDir)
Write-QaInfo ('Free RAM   : ' + (Format-QaMemorySample -Sample $memAtStart))
Write-QaInfo ('Timeout    : ' + $TimeoutSec + 's (quiet window ' + $QuietSec + 's, settled ' + $SettledQuietSec + 's)')
Write-Host ''
Write-Host 'Resolved command line (stage 1 - compile gate):'
Write-Host $cmdLine -ForegroundColor DarkGray
if (-not $NoLint) {
    if ($null -ne $lintScript) {
        Write-Host 'Resolved command line (stage 0 - lint, offline, runs first):'
        Write-Host ($lintScript + ' -Worktree ' + $WorktreePath) -ForegroundColor DarkGray
    } else {
        Write-QaInfo 'Stage 0 - lint: tools\lint\lint-run.ps1 not present in worktree (will skip gracefully).'
    }
} else {
    Write-QaInfo 'Stage 0 - lint: will be skipped (-NoLint).'
}

$memWarnLines = Get-QaLowMemoryWarningLines -Sample $memAtStart -ThresholdMb $memConfig.LowFreeWarnMb
if (@($memWarnLines).Count -gt 0) {
    Write-Host ''
    foreach ($l in $memWarnLines) { Write-QaWarn $l }
}

if ($WhatIf) {
    Write-Host ''
    Write-QaInfo 'DRY RUN (-WhatIf): nothing was launched, no junctions or artifacts were written.'
    exit $codes.Clean
}

# ------------------------------------------------------------------------------------------------
# Stage 0: optional lint
# ------------------------------------------------------------------------------------------------
if ($compileFromLog) {
    Write-QaInfo ('Stages 0 and 1 (lint, compile gate): reusing clean run from ' + $CompileFrom)
} elseif (-not $NoLint) {
    if ($null -eq $lintScript) {
        Write-QaInfo 'Stage 0 - lint: skipped (tools\lint\lint-run.ps1 absent in worktree).'
    } else {
        Write-QaStep 'Stage 0 - lint (offline, no engine)'
        & $lintScript -Worktree $WorktreePath
        $lintCode = $LASTEXITCODE
        if ($lintCode -eq 0) {
            Write-QaInfo 'Stage 0 - lint: clean.'
        } elseif ($lintCode -eq 2) {
            Write-QaError 'Stage 0 - lint: linter setup failed (exit 2).'
            exit $codes.Setup
        } else {
            Write-QaError 'Stage 0 - lint: errors found (re-run with -NoLint to skip explicitly).'
            exit $codes.Compile
        }
    }
} else {
    Write-QaInfo 'Stage 0 - lint: skipped (-NoLint).'
}

# Ensure external staging junction exists before launch.
if (-not $compileFromLog) {
    try {
        $stageInfo = Ensure-QaStageAddonsDir -WorktreePath $WorktreePath -AddonName $addonId
    } catch {
        Write-QaError ('Could not prepare staging junction: ' + $_.Exception.Message)
        exit $codes.Setup
    }
}

# Reclaim dead-host orphans from prior runs of this worktree.
$reap = Invoke-QaOrphanReap -WorktreePath $WorktreePath -ExcludeRunDir $ctx.RunDir -GraceSec $GraceSec
foreach ($r in @($reap.Reaped)) {
    Write-QaWarn ('Reclaimed orphaned run: host PID ' + $r.HostPid + ' left PID(s) ' + (@($r.Pids) -join ', ') + ' alive. Killed them.')
}
Write-QaRunProcessRecord -RunDir $ctx.RunDir -Tokens @($ctx.ProfileDir)

# Concurrency guard
$running = @()
if (-not $compileFromLog) { $running = @(Get-QaWorkbenchProcesses -NamePrefix $procPrefix) }
if ($running.Count -gt 0) {
    if ($Queue -or ($concurrency -eq 'queued')) {
        $split = Split-QaWorkbenchProcesses -Processes $running
        if (@($split.Interactive).Count -gt 0 -and -not $IgnoreInteractiveWorkbench) {
            Write-QaError ('Refusing to queue: ' + @($split.Interactive).Count + ' INTERACTIVE Workbench session(s) are open.')
            foreach ($p in @($split.Interactive)) { Write-QaInfo ('  interactive: ' + (Get-QaProcessDescription -Process $p)) }
            exit $codes.Setup
        }
        if (@($split.Harness).Count -gt 0) {
            if (-not $Queue) {
                Write-QaError "Another QA Workbench is running and concurrency='queued'. Re-run with -Queue."
                exit $codes.Setup
            }
            Write-QaStep 'Queued mode: waiting for other harness Workbench instances to close'
            if (-not (Wait-QaWorkbenchFree -NamePrefix $procPrefix -TimeoutSec $QueueTimeoutSec -ScopeToHarness)) {
                Write-QaError ('Timed out after ' + $QueueTimeoutSec + 's waiting for Workbench instances to close.')
                exit $codes.Setup
            }
        }
    }
}

# Artifacts + git exclude
[void](New-QaDirectory -Path $ctx.ProfileDir)
if (-not [string]::IsNullOrWhiteSpace($RunDirFile)) {
    Write-QaTextFile -Path $RunDirFile -Lines @($ctx.RunDir)
}
if (-not $NoGitExclude) {
    try { [void](Add-QaGitExclude -RepoPath $WorktreePath -Rule '.qa/') } catch { }
}
Write-QaTextFile -Path (Join-Path $ctx.RunDir 'command.txt') -Lines @('# stage 1 - compile gate', $cmdLine)

$logRoots = New-Object System.Collections.ArrayList
[void]$logRoots.Add($ctx.ProfileDir)
foreach ($r in $fallbackLogRoots) { [void]$logRoots.Add($r) }
$logRoots = @($logRoots.ToArray())

# ------------------------------------------------------------------------------------------------
# Launch + wait + terminate
# ------------------------------------------------------------------------------------------------
$pids = @()
$wait = $null
try {
    if ($compileFromLog) {
        $wait = [pscustomobject]@{ Reason = ('reused from ' + $CompileFrom); LogPath = $compileFromLog; ElapsedSec = 0 }
    } else {
        Write-QaStep 'Launching Workbench'
        $proc = Start-QaWorkbench -Exe $exePath -Arguments $argList -WorkingDirectory $gameRoot
        Write-QaInfo ('Seed PID   : ' + $proc.Id)

        $pids = Resolve-QaWorkbenchPids -SeedPid $proc.Id -Token $ctx.ProfileDir -NamePrefix $procPrefix -TimeoutSec $pidResolveSec -PollSec $pollSec
        if (@($pids).Count -gt 0) { Write-QaInfo ('Live PID(s): ' + (@($pids) -join ', ')) }

        Write-QaStep 'Waiting for script compilation to finish'
        $script:lastProgress = 0
        $onProgress = {
            param($elapsed, $compileSeen, $logPath)
            if (($elapsed - $script:lastProgress) -ge 30) {
                $script:lastProgress = $elapsed
                $state = 'loading'
                if ($compileSeen) { $state = 'compiling/idle' }
                Write-QaInfo ('  ... ' + $elapsed + 's elapsed (' + $state + ')')
            }
        }

        $wait = Wait-QaRunCompletion -Pids $pids -LogSearchRoots $logRoots -LogFileName $logFileName `
            -OwnershipToken $ctx.ProfileDir -ReportFileNames $reportNames -ReportExcludePatterns $reportExcludes `
            -StartTime $startTime -TimeoutSec $TimeoutSec -QuietSec $QuietSec -SettledQuietSec $SettledQuietSec -PollSec $pollSec `
            -WatchForReport $false -Patterns $patterns -OnProgress $onProgress

        Write-QaInfo ('Completion : ' + $wait.Reason + ' after ' + $wait.ElapsedSec + 's')
    }
} catch {
    Write-QaError ('Launch failed: ' + $_.Exception.Message)
    exit $codes.Setup
} finally {
    if (@($pids).Count -gt 0) {
        Write-QaStep 'Terminating Workbench'
        $stop = Stop-QaWorkbench -Pids $pids -GraceSec $GraceSec
        if ($stop.Graceful) { Write-QaInfo 'Closed gracefully.' } else { Write-QaInfo ('Force-killed PID(s): ' + (@($stop.Killed) -join ', ')) }
        Start-Sleep -Seconds 2
    }
}

# ------------------------------------------------------------------------------------------------
# Post-mortem
# ------------------------------------------------------------------------------------------------
Write-QaStep 'Parsing artifacts'

$logPath = $null
if ($wait) { $logPath = $wait.LogPath }
if (-not $logPath) {
    $logPath = Find-QaConsoleLog -SearchRoots $logRoots -FileName $logFileName -Since $startTime -OwnershipToken $ctx.ProfileDir -Patterns $patterns
}

$logFound = $false
$lines = @()
if ($logPath -and (Test-Path -LiteralPath $logPath)) {
    $logFound = $true
    $lines = Read-QaLines -Path $logPath
    Write-QaInfo ('Log        : ' + $logPath + ' (' + $lines.Count + ' lines)')
    Copy-Item -LiteralPath $logPath -Destination (Join-Path $ctx.RunDir 'console.log') -Force -ErrorAction SilentlyContinue
} else {
    Write-QaWarn 'No console.log could be located for this run.'
}

$errorLogPath = $null
if ($logPath) {
    $candidate = Join-Path (Split-Path -Parent $logPath) $errorLogName
    if (Test-Path -LiteralPath $candidate) {
        $errorLogPath = $candidate
        Copy-Item -LiteralPath $candidate -Destination (Join-Path $ctx.RunDir $errorLogName) -Force -ErrorAction SilentlyContinue
    }
}
if ((-not $logFound) -and $errorLogPath) {
    $lines = Read-QaLines -Path $errorLogPath
    $logFound = $true
    Write-QaWarn 'console.log missing; falling back to error.log.'
}

$findings = Get-QaLogFindings -Lines $lines -Patterns $patterns
[void](Add-QaDiagnosticOwners -Diagnostics $findings.Errors -WorktreePath $WorktreePath -AddonName $addonId)

$resolution = $null
if ($logFound) {
    $resolution = Test-QaAddonResolution -Findings $findings -WorktreePath $WorktreePath -StageAddonLink $stageInfo.AddonLink -AddonGuid $addonGuid -VanillaGuid $vanillaGuid
}

$outcome = Resolve-QaOutcome -Findings $findings -LogFound $logFound
if ($resolution -and (-not $resolution.Ok) -and ($addonEnforce -eq 'fail')) {
    $outcome = [pscustomobject]@{
        ExitCode = $codes.Setup
        Reason   = ('Addon resolution check failed: ' + ($resolution.Issues -join ' | '))
    }
}

$memAtEnd = Get-QaMemorySnapshot
$body = Format-QaRunReport -Findings $findings -Outcome $outcome -Resolution $resolution -Enforce $addonEnforce `
    -MemoryStart $memAtStart -MemoryEnd $memAtEnd -MemoryWarnMb $memConfig.LowFreeWarnMb
foreach ($l in $body) { Write-Host $l }

$header = [ordered]@{
    'Timestamp'   = $startTime.ToString('yyyy-MM-dd HH:mm:ss')
    'Worktree'    = $WorktreePath
    'Module'      = $Module
    'Engine'      = $Engine
    'CompileGate' = $CompileGate
    'Executable'  = $exePath
    'GameDir'     = $gameRoot
    'StageDir'    = $stageInfo.AddonsDir
    'Profile'     = $ctx.ProfileDir
    'FreeRamStart'= $(if ($null -ne $memAtStart) { [string]$memAtStart.FreeMb + ' MB' } else { 'unavailable' })
    'FreeRamEnd'  = $(if ($null -ne $memAtEnd) { [string]$memAtEnd.FreeMb + ' MB' } else { 'unavailable' })
    'FreeRamDelta'= Format-QaMemoryDelta -Start $memAtStart -End $memAtEnd
    'Stage'       = 'compile'
    'Completion'  = $(if ($wait) { $wait.Reason + ' after ' + $wait.ElapsedSec + 's' } else { 'n/a' })
    'InitMs'      = $(if ($null -ne $findings.InitMs) { [string]$findings.InitMs } else { 'n/a' })
    'Log'         = $(if ($logPath) { $logPath } else { '(none)' })
    'ExitCode'    = $outcome.ExitCode
    'Verdict'     = $outcome.Reason
}
Write-QaSummary -Path (Join-Path $ctx.RunDir 'summary.txt') -Header $header -Body $body
Write-Host ''
Write-QaInfo ('Artifacts  : ' + $ctx.RunDir)

if (-not $KeepProfile) {
    $pruned = Remove-QaOldRuns -RunsRoot $ctx.RunsRoot -Keep $KeepRuns
    if (@($pruned).Count -gt 0) { Write-QaInfo ('Pruned ' + @($pruned).Count + ' old run(s).') }
}

Write-Host ''
if ($outcome.ExitCode -eq $codes.Clean) {
    Write-Host ('QA PASS (exit 0): ' + $outcome.Reason) -ForegroundColor Green
} else {
    Write-Host ('QA FAIL (exit ' + $outcome.ExitCode + '): ' + $outcome.Reason) -ForegroundColor Red
}
exit $outcome.ExitCode
