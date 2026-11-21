# QaSelfTest.ps1 - offline unit tests for BGONE QA parsers and pure decision helpers (PowerShell 5.1 ONLY).
# Runnable without Workbench or Arma Reforger: tools\qa\qa-run.ps1 -SelfTest

$script:QaTestPassed = 0
$script:QaTestFailed = 0

function Assert-QaEqual {
    param($Expected, $Actual, [string]$What)
    if ([string]$Expected -eq [string]$Actual) {
        $script:QaTestPassed++
        Write-Host ("  PASS  " + $What) -ForegroundColor Green
    } else {
        $script:QaTestFailed++
        Write-Host ("  FAIL  " + $What + " -- expected '" + $Expected + "', got '" + $Actual + "'") -ForegroundColor Red
    }
}

function Assert-QaTrue {
    param([bool]$Condition, [string]$What)
    Assert-QaEqual -Expected $true -Actual $Condition -What $What
}

function New-QaFixtureBgoneCleanLog {
    param([string]$StageLink = 'C:/wt/.bgone-stage/deadbeef/Addons/BGONE')
    return @(
        '---------------------------------------------',
        'Log C:\wt\bgone-test\.qa\runs\20261121-120000\profile\stable\logs\logs_2026-11-21_12-00-01\console.log started at 2026-11-21 12:00:01',
        ('12:00:02.575   ENGINE       : CLI Params: -gproj ' + $StageLink + '/addon.gproj -addonsDir C:/wt/.bgone-stage/deadbeef/Addons -profile C:\wt\bgone-test\.qa\runs\20261121-120000\profile\stable -wbModule=ResourceManager'),
        '12:00:02.595   ENGINE       : Available addons:',
        ('12:00:02.595    ENGINE       : gproj: ''' + $StageLink + '/addon.gproj'' guid: ''5F1EE615E7AE3106'''),
        '12:00:02.595    ENGINE       : gproj: ''Z:/SteamLibrary/steamapps/common/Arma Reforger/addons/data/ArmaReforger.gproj'' guid: ''58D0FB3206B6F859'' (packed)',
        '12:00:02.595   PROFILING    : Workbench Init Engine took: 12345.500000 ms',
        '12:00:27.837    SCRIPT       : SCRIPT       : Compiling Game scripts',
        '12:00:29.125    SCRIPT    (W): @"scripts/Game/Diag/BGONE_MPBotController.c,10": warning sample',
        '12:00:29.998    SCRIPT       : Module: Game; loaded 5659x files; 11072x classes; CRC32: 936bf6ca',
        '12:00:29.980   PROFILING    : Compiling Game scripts took: 2143.190600 ms',
        '12:00:30.000   PROFILING    : Workbench startup took: 14000.000000 ms'
    )
}

function Invoke-QaSelfTest {
    param($Patterns)

    if ($null -eq $Patterns) { $Patterns = Get-QaDefaultPatterns }
    $codes = Get-QaExitCodes
    $script:QaTestPassed = 0
    $script:QaTestFailed = 0

    Write-QaStep 'Self-test: log parsing'
    $clean = New-QaFixtureBgoneCleanLog
    $f = Get-QaLogFindings -Lines $clean -Patterns $Patterns
    Assert-QaEqual -Expected 0 -Actual $f.Errors.Count         -What 'clean log: 0 compile errors'
    Assert-QaEqual -Expected 1 -Actual $f.Warnings.Count       -What 'clean log: 1 compile warning'
    Assert-QaEqual -Expected 0 -Actual $f.SetupErrors.Count    -What 'clean log: 0 setup errors'
    Assert-QaTrue  -Condition $f.CompileStarted                -What 'clean log: compile-start marker found'
    Assert-QaTrue  -Condition $f.CompileCompleted              -What 'clean log: compile-done marker found'
    Assert-QaTrue  -Condition $f.ModuleLoaded                  -What 'clean log: Module: Game marker found'
    Assert-QaEqual -Expected 2 -Actual $f.ResolvedAddons.Count -What 'clean log: 2 addons parsed'
    Assert-QaEqual -Expected $codes.Clean -Actual (Resolve-QaOutcome -Findings $f).ExitCode -What 'clean log: exit 0'

    $errLines = @($clean) + @('12:00:28.500    SCRIPT    (E): @"scripts/Game/Diag/BGONE_MPBotController.c,42": Syntax error', '12:00:28.510    SCRIPT    (E): Can''t compile "Game" script module!')
    $fe = Get-QaLogFindings -Lines $errLines -Patterns $Patterns
    Assert-QaTrue  -Condition $fe.CompileFailed -What 'error log: compile-failed sentinel detected'
    Assert-QaEqual -Expected $codes.Compile -Actual (Resolve-QaOutcome -Findings $fe).ExitCode -What 'error log: exit 1'

    Write-QaStep 'Self-test: post-compile runtime error attribution'
    $selfWt = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).Path
    $vmLines = @($clean) + @('12:00:31.000    SCRIPT    (E): Virtual Machine Exception')
    $fvm = Get-QaLogFindings -Lines $vmLines -Patterns $Patterns
    [void](Add-QaDiagnosticOwners -Diagnostics $fvm.Errors -WorktreePath $selfWt)
    Assert-QaEqual -Expected 'unknown' -Actual $fvm.Errors[0].Owner -What 'unattributed VM exception marked owner=unknown'
    Assert-QaEqual -Expected $codes.Compile -Actual (Resolve-QaOutcome -Findings $fvm).ExitCode -What 'unattributed VM exception stays blocking (exit 1)'

    $vanLines = @($clean) + @('12:00:31.000    SCRIPT    (E): @"scripts/Game/VanillaOnlyMissingFile.c,9": noise')
    $fvan = Get-QaLogFindings -Lines $vanLines -Patterns $Patterns
    [void](Add-QaDiagnosticOwners -Diagnostics $fvan.Errors -WorktreePath $selfWt)
    Assert-QaEqual -Expected 'vanilla' -Actual $fvan.Errors[0].Owner -What 'non-BGONE post-compile file marked owner=vanilla'
    Assert-QaEqual -Expected $codes.Clean -Actual (Resolve-QaOutcome -Findings $fvan).ExitCode -What 'confirmed vanilla post-compile error is non-blocking (exit 0)'

    $ourLines = @($clean) + @('12:00:31.000    SCRIPT    (E): @"scripts/Game/Diag/BGONE_MPBotController.c,12": boom')
    $four = Get-QaLogFindings -Lines $ourLines -Patterns $Patterns
    [void](Add-QaDiagnosticOwners -Diagnostics $four.Errors -WorktreePath $selfWt)
    Assert-QaEqual -Expected 'BGONE' -Actual $four.Errors[0].Owner -What 'BGONE post-compile file marked owner=BGONE'
    Assert-QaEqual -Expected $codes.Compile -Actual (Resolve-QaOutcome -Findings $four).ExitCode -What 'BGONE post-compile error is blocking (exit 1)'

    Write-QaStep 'Self-test: strict BGONE addon resolution'
    $res = Test-QaAddonResolution -Findings $f -WorktreePath 'C:\wt\bgone-test' -StageAddonLink 'C:\wt\.bgone-stage\deadbeef\Addons\BGONE' -AddonGuid '5F1EE615E7AE3106' -VanillaGuid '58D0FB3206B6F859'
    Assert-QaTrue -Condition $res.Ok -What 'BGONE resolved from staging junction + vanilla present'

    $resForeign = Test-QaAddonResolution -Findings $f -WorktreePath 'C:\wt\other-wt' -StageAddonLink 'C:\wt\.bgone-stage\11111111\Addons\BGONE' -AddonGuid '5F1EE615E7AE3106' -VanillaGuid '58D0FB3206B6F859'
    Assert-QaTrue -Condition (-not $resForeign.Ok) -What 'foreign staging path rejected'

    $cwdLog = New-QaFixtureBgoneCleanLog -StageLink './addons/BGONE'
    $fcwd = Get-QaLogFindings -Lines $cwdLog -Patterns $Patterns
    $resCwd = Test-QaAddonResolution -Findings $fcwd -WorktreePath 'C:\wt\bgone-test' -StageAddonLink 'C:\wt\.bgone-stage\deadbeef\Addons\BGONE' -AddonGuid '5F1EE615E7AE3106' -VanillaGuid '58D0FB3206B6F859'
    Assert-QaTrue -Condition (-not $resCwd.Ok) -What 'CWD-relative ./addons BGONE resolution rejected'

    $resNoVan = Test-QaAddonResolution -Findings $f -WorktreePath 'C:\wt\bgone-test' -StageAddonLink 'C:\wt\.bgone-stage\deadbeef\Addons\BGONE' -AddonGuid '5F1EE615E7AE3106' -VanillaGuid 'DEADBEEFDEADBEEF'
    Assert-QaTrue -Condition (-not $resNoVan.Ok) -What 'missing vanilla GUID rejected'

    Write-QaStep 'Self-test: staging path & Get-QaProcessWorktree fallback'
    $st1 = Get-QaStageInfo -WorktreePath 'C:\wt\bgone-pr2-qa'
    $st2 = Get-QaStageInfo -WorktreePath 'c:\wt\bgone-pr2-qa\'
    Assert-QaEqual -Expected $st1.Hash -Actual $st2.Hash -What 'staging hash is deterministic and case/slash normalized'
    Assert-QaTrue -Condition ($st1.AddonsDir -like 'C:\wt\.bgone-stage\*\Addons') -What 'staging addonsDir lives outside worktree'

    $cmdBgone = '"Z:\SteamLibrary\steamapps\common\Arma Reforger\ArmaReforgerSteam.exe" -addonsDir C:\wt\.bgone-stage\12345678\Addons -profile C:\wt\bgone-pr2-qa\.qa\runs\20261121-120000\profile\stable-mp-client1'
    Assert-QaEqual -Expected 'C:\wt\bgone-pr2-qa' -Actual (Get-QaProcessWorktree -CommandLine $cmdBgone) -What 'Get-QaProcessWorktree maps staged BGONE process back to real worktree'

    $cmdMrs = '"Z:\SteamLibrary\steamapps\common\Arma Reforger\ArmaReforgerSteam.exe" -addonsDir C:\wt\mrs-qa1\Addons -profile C:\wt\mrs-qa1\.qa\runs\20261121-120000\profile\exp-mp-client1'
    Assert-QaEqual -Expected 'C:\wt\mrs-qa1' -Actual (Get-QaProcessWorktree -CommandLine $cmdMrs) -What 'Get-QaProcessWorktree preserves MRS-GC worktree extraction'

    Write-QaStep 'Self-test: worktree safety & MP window state'
    $safetyCfg = [pscustomobject]@{ requireSpaceFreePath = $true; forbiddenRootSubstrings = @('Documents\My Games\ArmaReforgerWorkbench\addons') }
    Assert-QaTrue -Condition (Test-QaWorktreeSafety -WorktreePath 'C:\wt\bgone-pr2-qa' -SafetyConfig $safetyCfg).Ok -What 'space-free C:\wt path accepted'
    Assert-QaTrue -Condition (-not (Test-QaWorktreeSafety -WorktreePath 'C:\my path\bgone' -SafetyConfig $safetyCfg).Ok) -What 'spaced path rejected'

    $now = Get-Date
    $wLoad = Get-QaMpWindowState -LaunchTime $now.AddSeconds(-400) -BotStartedAt $null -LoadTimeoutSec 360 -ScenarioTimeoutSec 180 -Now $now
    Assert-QaTrue -Condition ($wLoad.Phase -eq 'loading' -and $wLoad.Expired) -What 'MP load timeout expires cleanly'
    $wScen = Get-QaMpWindowState -LaunchTime $now.AddSeconds(-400) -BotStartedAt $now.AddSeconds(-60) -LoadTimeoutSec 360 -ScenarioTimeoutSec 180 -Now $now
    Assert-QaTrue -Condition ($wScen.Phase -eq 'scenario' -and (-not $wScen.Expired)) -What 'MP scenario timeout counts from bot start'

    Write-Host ''
    Write-Host ('Self-test summary: ' + $script:QaTestPassed + ' passed, ' + $script:QaTestFailed + ' failed.')
    if ($script:QaTestFailed -gt 0) { return 1 }
    return 0
}
