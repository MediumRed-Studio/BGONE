# QaLogParse.ps1 - PURE log-parsing + outcome functions for BGONE QA runners (PowerShell 5.1 ONLY).

function Get-QaExitCodes {
    return [pscustomobject]@{
        Clean        = 0
        Compile      = 1
        Setup        = 2
        Hang         = 3
        Tests        = 4
        Plugin       = 5
        Timeout      = 6
        Inconclusive = 7
    }
}

function Get-QaDefaultPatterns {
    return [pscustomobject]@{
        CompileError          = 'SCRIPT\s+\(E\):\s*(?<body>.+?)\s*$'
        CompileWarning        = 'SCRIPT\s+\(W\):\s*(?<body>.+?)\s*$'
        Location              = '^@"(?<file>[^"]+),(?<line>\d+)"\s*:\s*(?<msg>.*)$'
        CompileStart          = 'SCRIPT\s+:\s*Compiling Game scripts\s*$'
        CompileDone           = 'PROFILING\s+:\s*Compiling Game scripts took:'
        WorkbenchStarted      = 'PROFILING\s+:\s*Workbench startup took:'
        CompileFailedSentinel = 'Can''t compile "Game" script module!'
        ModuleLoaded          = 'SCRIPT\s+:\s*Module:\s*Game;'
        CliParams             = 'CLI Params:\s*(?<args>.*)$'
        Hang                  = 'ENGINE\s+\(W\):\s*Application hangs'
        InitTook              = 'PROFILING\s+:\s*Workbench (Init Engine|startup) took:\s*(?<ms>[\d\.]+)\s*ms'
        AvailableAddonsHeader = 'ENGINE\s+:\s*(Available|Loaded) addons:'
        AddonEntry            = "gproj:\s*'(?<path>[^']+)'\s*guid:\s*'(?<guid>[0-9A-Fa-f]+)'(?<flags>.*)$"
        AnySeverity           = '^\s*[\d:\.]*\s*(?<category>[A-Z_]+)\s+\((?<sev>[EWF])\):\s*(?<body>.*)$'
        SetupError            = @(
            '\bENGINE\s+\(E\):',
            '\bRESOURCES\s+\(E\):',
            '\bINIT\s+\(E\):'
        )
        SetupErrorIgnore      = @(
            "Unknown keyword/data 'ClusteredTilingUserSettings'",
            "Unable to initialize a game/gamelib",
            "Steamworks: RequestCurrentStats failed"
        )
        VmException           = 'SCRIPT\s+\(E\):\s*Virtual Machine Exception'
        VmExceptionReason     = '^Reason:\s*(?<reason>.+?)\s*$'
        VmExceptionClass      = "^Class:\s*'(?<class>[^']*)'"
        VmExceptionFunction   = "^Function:\s*'(?<function>[^']*)'"
        VmExceptionSuiteFrame = '(?<suite>BGONE_[A-Za-z0-9_]+)\.c:(?<line>\d+)'
    }
}

function Get-QaVmExceptions {
    param([string[]]$Lines, $Patterns, [int]$BlockLines = 24)

    if ($null -eq $Patterns) { $Patterns = Get-QaDefaultPatterns }
    if ($null -eq $Lines) { $Lines = @() }

    $found = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        $line = $Lines[$i]
        if ($null -eq $line) { continue }
        if (-not ($line -match $Patterns.VmException)) { continue }

        $reason = ''
        $class  = ''
        $func   = ''
        $suites = New-Object System.Collections.ArrayList

        $last = $i + $BlockLines
        if ($last -ge $Lines.Count) { $last = $Lines.Count - 1 }
        for ($j = $i + 1; $j -le $last; $j++) {
            $b = $Lines[$j]
            if ($null -eq $b) { continue }
            if ($b -match $Patterns.VmException) { break }
            if (($reason -eq '') -and ($b -match $Patterns.VmExceptionReason)) { $reason = $Matches['reason']; continue }
            if (($class  -eq '') -and ($b -match $Patterns.VmExceptionClass))  { $class  = $Matches['class'];  continue }
            if (($func   -eq '') -and ($b -match $Patterns.VmExceptionFunction)) { $func = $Matches['function']; continue }
            if ($b -match $Patterns.VmExceptionSuiteFrame) {
                $s = $Matches['suite']
                if (-not $suites.Contains($s)) { [void]$suites.Add($s) }
            }
        }

        [void]$found.Add([pscustomobject]@{
            LineNumber = $i + 1
            Reason     = $reason
            Class      = $class
            Function   = $func
            Suites     = @($suites.ToArray())
        })
    }
    return @($found.ToArray())
}

function Get-QaVmExceptionSites {
    param($Exceptions)

    $sites = New-Object System.Collections.ArrayList
    foreach ($e in @($Exceptions)) {
        if ($null -eq $e) { continue }
        $key = $e.Reason + '|' + $e.Class + '|' + $e.Function
        $hit = $null
        foreach ($s in $sites) { if ($s.Key -eq $key) { $hit = $s; break } }
        if ($null -eq $hit) {
            $hit = [pscustomobject]@{
                Key      = $key
                Reason   = $e.Reason
                Class    = $e.Class
                Function = $e.Function
                Count    = 0
                Suites   = New-Object System.Collections.ArrayList
            }
            [void]$sites.Add($hit)
        }
        $hit.Count = $hit.Count + 1
        foreach ($s in @($e.Suites)) { if (-not $hit.Suites.Contains($s)) { [void]$hit.Suites.Add($s) } }
    }

    $out = New-Object System.Collections.ArrayList
    foreach ($s in $sites) {
        [void]$out.Add([pscustomobject]@{
            Reason   = $s.Reason
            Class    = $s.Class
            Function = $s.Function
            Count    = $s.Count
            Suites   = @($s.Suites.ToArray())
        })
    }
    return @($out.ToArray())
}

function Merge-QaPatterns {
    param($Overrides)
    $patterns = Get-QaDefaultPatterns
    if ($null -eq $Overrides) { return $patterns }
    foreach ($prop in $Overrides.PSObject.Properties) {
        if ($prop.Name -like '_*') { continue }
        if ($null -eq $prop.Value) { continue }
        $patterns | Add-Member -NotePropertyName $prop.Name -NotePropertyValue $prop.Value -Force
    }
    return $patterns
}

function Test-QaAnyMatch {
    param([string]$Text, $Patterns)
    if ([string]::IsNullOrEmpty($Text)) { return $false }
    foreach ($p in @($Patterns)) {
        if ([string]::IsNullOrWhiteSpace($p)) { continue }
        if ($Text -match $p) { return $true }
    }
    return $false
}

function Get-QaLogFindings {
    param(
        [string[]]$Lines,
        $Patterns
    )

    if ($null -eq $Patterns) { $Patterns = Get-QaDefaultPatterns }
    if ($null -eq $Lines) { $Lines = @() }

    $errors        = New-Object System.Collections.ArrayList
    $warnings      = New-Object System.Collections.ArrayList
    $setupErrors   = New-Object System.Collections.ArrayList
    $addons        = New-Object System.Collections.ArrayList

    $compileStarted   = $false
    $compileCompleted = $false
    $compileFailed    = $false
    $moduleLoaded     = $false
    $hangDetected     = $false
    $cliParams        = ''
    $initMs           = $null
    $inAddonBlock     = $false

    $lineNo = 0
    foreach ($line in $Lines) {
        $lineNo++
        if ($null -eq $line) { continue }

        if (-not $compileStarted -and ($line -match $Patterns.CompileStart)) { $compileStarted = $true }
        if (-not $compileCompleted -and ($line -match $Patterns.CompileDone)) { $compileCompleted = $true }
        if (-not $moduleLoaded -and ($line -match $Patterns.ModuleLoaded)) { $moduleLoaded = $true }
        if (-not $hangDetected -and ($line -match $Patterns.Hang)) { $hangDetected = $true }
        if ([string]::IsNullOrEmpty($cliParams) -and ($line -match $Patterns.CliParams)) {
            $cliParams = $Matches['args']
        }
        if (($null -eq $initMs) -and ($line -match $Patterns.InitTook)) {
            $initMs = [double]$Matches['ms']
        }

        if ($line -match $Patterns.AvailableAddonsHeader) { $inAddonBlock = $true; continue }
        if ($inAddonBlock) {
            if ($line -match $Patterns.AddonEntry) {
                $entryPath = $Matches['path']
                $entryGuid = $Matches['guid']
                $entryFlags = [string]$Matches['flags']
                $packed = $false
                if ($entryFlags.IndexOf('packed', [System.StringComparison]::OrdinalIgnoreCase) -ge 0) { $packed = $true }
                [void]$addons.Add([pscustomobject]@{
                    Path   = $entryPath
                    Guid   = $entryGuid
                    Packed = $packed
                })
                continue
            }
            $inAddonBlock = $false
        }

        if ($line -match $Patterns.CompileFailedSentinel) {
            $compileFailed = $true
            continue
        }
        if ($line -match $Patterns.CompileError) {
            $diag = ConvertTo-QaDiagnostic -Body $Matches['body'] -Severity 'E' -Raw $line -LineNumber $lineNo -Patterns $Patterns
            $diag | Add-Member -NotePropertyName 'AfterCompile' -NotePropertyValue $compileCompleted -Force
            [void]$errors.Add($diag)
            continue
        }
        if ($line -match $Patterns.CompileWarning) {
            [void]$warnings.Add((ConvertTo-QaDiagnostic -Body $Matches['body'] -Severity 'W' -Raw $line -LineNumber $lineNo -Patterns $Patterns))
            continue
        }

        if (Test-QaAnyMatch -Text $line -Patterns $Patterns.SetupError) {
            if (-not (Test-QaAnyMatch -Text $line -Patterns $Patterns.SetupErrorIgnore)) {
                [void]$setupErrors.Add([pscustomobject]@{
                    Raw        = $line.Trim()
                    LineNumber = $lineNo
                })
            }
        }
    }

    return [pscustomobject]@{
        Errors           = @($errors.ToArray())
        Warnings         = @($warnings.ToArray())
        SetupErrors      = @($setupErrors.ToArray())
        ResolvedAddons   = @($addons.ToArray())
        CompileStarted   = $compileStarted
        CompileCompleted = $compileCompleted
        CompileFailed    = $compileFailed
        ModuleLoaded     = $moduleLoaded
        HangDetected     = $hangDetected
        CliParams        = $cliParams
        InitMs           = $initMs
        LineCount        = $lineNo
    }
}

function ConvertTo-QaDiagnostic {
    param([string]$Body, [string]$Severity, [string]$Raw, [int]$LineNumber, $Patterns)
    $file = ''
    $srcLine = 0
    $msg = $Body
    if ($Body -match $Patterns.Location) {
        $file = $Matches['file']
        $srcLine = [int]$Matches['line']
        $msg = $Matches['msg']
    }
    return [pscustomobject]@{
        Severity   = $Severity
        File       = $file
        Line       = $srcLine
        Message    = $msg
        Raw        = $Raw.Trim()
        LineNumber = $LineNumber
    }
}

# Single-addon owner attribution for BGONE: resolves addon-relative script paths against <WorktreePath>\<rel>.
function Add-QaDiagnosticOwners {
    param(
        $Diagnostics,
        [string]$WorktreePath,
        [string]$AddonName = 'BGONE'
    )

    if ($null -eq $Diagnostics) { return @() }
    $cache = @{}
    $rootValid = (-not [string]::IsNullOrWhiteSpace($WorktreePath)) -and (Test-Path -LiteralPath $WorktreePath)

    $resolveOwner = {
        param([string]$Candidate)
        if ([string]::IsNullOrWhiteSpace($Candidate)) { return 'vanilla/unknown' }
        if ($cache.ContainsKey($Candidate)) { return $cache[$Candidate] }
        $found = 'vanilla/unknown'
        if ($rootValid) {
            $rel = $Candidate.Replace('/', '\').TrimStart('\')
            $probe = Join-Path $WorktreePath $rel
            if (Test-Path -LiteralPath $probe) { $found = $AddonName }
        }
        $cache[$Candidate] = $found
        return $found
    }

    foreach ($d in $Diagnostics) {
        $owner = 'unknown'
        $sawFile = $false
        if ($d.File) {
            $sawFile = $true
            $owner = & $resolveOwner ([string]$d.File)
        }
        if ($owner -ne $AddonName -and $d.Raw) {
            foreach ($m in ([regex]::Matches([string]$d.Raw, '(?<path>[\w\-/\\\.]+\.c)\b'))) {
                $sawFile = $true
                $cand = $m.Groups['path'].Value.TrimStart('.', '/', '\')
                $probeOwner = & $resolveOwner $cand
                if ($probeOwner -eq $AddonName) { $owner = $probeOwner; break }
            }
        }
        if ($owner -ne $AddonName) {
            if ($sawFile -and $rootValid) { $owner = 'vanilla' } else { $owner = 'unknown' }
        }
        $d | Add-Member -NotePropertyName 'Owner' -NotePropertyValue $owner -Force
        $d | Add-Member -NotePropertyName 'IsOurs' -NotePropertyValue ($owner -eq $AddonName) -Force
    }
    return $Diagnostics
}

function Get-QaUniqueDiagnostics {
    param($Diagnostics)
    if ($null -eq $Diagnostics) { return @() }
    $seen = @{}
    $out = New-Object System.Collections.ArrayList
    foreach ($d in @($Diagnostics | Sort-Object -Property LineNumber)) {
        $key = [string]$d.File + '|' + [string]$d.Line + '|' + [string]$d.Message
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        [void]$out.Add($d)
    }
    return @($out.ToArray())
}

function Test-QaLogBelongsToRun {
    param([string[]]$Lines, [string]$Token, $Patterns)
    if ([string]::IsNullOrWhiteSpace($Token)) { return $true }
    if ($null -eq $Patterns) { $Patterns = Get-QaDefaultPatterns }
    $needle = $Token.Replace('/', '\')
    foreach ($line in $Lines) {
        if ($null -eq $line) { continue }
        if ($line -match $Patterns.CliParams) {
            $cli = $Matches['args'].Replace('/', '\')
            return ($cli.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0)
        }
    }
    return $false
}

# Strict single-addon resolution check:
# Verifies BGONE (GUID 5F1EE615E7AE3106) resolves strictly from <WorktreePath>\addon.gproj OR its
# staging junction (<StageAddonLink>\addon.gproj), rejects any CWD-relative or foreign BGONE path,
# and verifies vanilla ArmaReforger (GUID 58D0FB3206B6F859) resolved.
function Test-QaAddonResolution {
    param(
        $Findings,
        [string]$WorktreePath,
        [string]$StageAddonLink = '',
        [string]$AddonGuid = '5F1EE615E7AE3106',
        [string]$VanillaGuid = '58D0FB3206B6F859'
    )

    $issues = New-Object System.Collections.ArrayList
    $resolved = @($Findings.ResolvedAddons)

    if ($resolved.Count -eq 0) {
        [void]$issues.Add("No 'Available addons:' / 'Loaded addons:' block found in the log - cannot verify addon resolution.")
        return [pscustomobject]@{ Ok = $false; Issues = @($issues.ToArray()); Matched = @(); Foreign = @() }
    }

    $allowedGprojs = New-Object System.Collections.ArrayList
    if (-not [string]::IsNullOrWhiteSpace($WorktreePath)) {
        $wtNorm = $WorktreePath.Replace('/', '\').TrimEnd('\')
        [void]$allowedGprojs.Add((Join-Path $wtNorm 'addon.gproj'))
    }
    if (-not [string]::IsNullOrWhiteSpace($StageAddonLink)) {
        $stNorm = $StageAddonLink.Replace('/', '\').TrimEnd('\')
        [void]$allowedGprojs.Add((Join-Path $stNorm 'addon.gproj'))
    }

    $matched = New-Object System.Collections.ArrayList
    $foreign = New-Object System.Collections.ArrayList
    $wantGuid = $AddonGuid.ToUpperInvariant()

    foreach ($a in $resolved) {
        if ([string]::IsNullOrWhiteSpace($a.Path)) { continue }
        $g = ([string]$a.Guid).ToUpperInvariant()
        if ($g -ne $wantGuid) { continue }

        $p = ([string]$a.Path).Replace('/', '\')
        $isRelative = (-not [System.IO.Path]::IsPathRooted($p))
        $isAllowed = $false
        if (-not $isRelative) {
            foreach ($okPath in $allowedGprojs) {
                if ([string]::Equals($p, $okPath, [System.StringComparison]::OrdinalIgnoreCase)) {
                    $isAllowed = $true
                    break
                }
            }
        }
        if ($isAllowed) {
            [void]$matched.Add($a)
        } else {
            [void]$foreign.Add($a)
        }
    }

    if ($matched.Count -eq 0) {
        [void]$issues.Add("BGONE addon ($AddonGuid) did not resolve from the target worktree or its staging junction.")
    }

    foreach ($f in $foreign) {
        if (-not [System.IO.Path]::IsPathRooted(([string]$f.Path).Replace('/', '\'))) {
            [void]$issues.Add("Addon $AddonGuid resolved through the CWD-relative './addons' slot (working directory not pinned): " + $f.Path)
        } else {
            [void]$issues.Add("Addon $AddonGuid resolved from OUTSIDE the target worktree/staging junction: " + $f.Path)
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($VanillaGuid)) {
        $hasVanilla = $false
        foreach ($a in $resolved) {
            if ($a.Guid -and ($a.Guid.ToUpperInvariant() -eq $VanillaGuid.ToUpperInvariant())) { $hasVanilla = $true; break }
        }
        if (-not $hasVanilla) {
            [void]$issues.Add("Vanilla game addon $VanillaGuid did not resolve (check the engine's gameDir / working directory).")
        }
    }

    return [pscustomobject]@{
        Ok      = ($issues.Count -eq 0)
        Issues  = @($issues.ToArray())
        Matched = @($matched.ToArray())
        Foreign = @($foreign.ToArray())
    }
}

function Get-QaBlockingErrors {
    param($Diagnostics)
    if ($null -eq $Diagnostics) { return @() }
    return @($Diagnostics | Where-Object {
        (-not ($_.PSObject.Properties['AfterCompile'] -and $_.AfterCompile)) -or
        (-not $_.PSObject.Properties['IsOurs']) -or
        $_.IsOurs -or
        ($_.PSObject.Properties['Owner'] -and ($_.Owner -eq 'unknown'))
    })
}

function Resolve-QaOutcome {
    param(
        $Findings,
        [bool]$LogFound = $true
    )

    $codes = Get-QaExitCodes

    if (-not $LogFound) {
        return [pscustomobject]@{
            ExitCode = $codes.Setup
            Reason   = 'No console.log was produced - Workbench never got far enough to log (check exe path, working directory and CLI flags).'
        }
    }

    if ($Findings.CompileFailed) {
        $unique = Get-QaUniqueDiagnostics -Diagnostics $Findings.Errors
        return [pscustomobject]@{
            ExitCode = $codes.Compile
            Reason   = ('Script compile FAILED (engine reported: Can''t compile "Game" script module!) with ' + $unique.Count + ' unique error(s).')
        }
    }

    if ((-not $Findings.CompileCompleted) -and ($Findings.SetupErrors.Count -gt 0)) {
        return [pscustomobject]@{
            ExitCode = $codes.Setup
            Reason   = ("Engine setup failed before the compile finished (" + $Findings.SetupErrors.Count + " engine error line(s)); first: " + $Findings.SetupErrors[0].Raw)
        }
    }

    $blocking = @(Get-QaBlockingErrors -Diagnostics $Findings.Errors)
    if ($blocking.Count -gt 0) {
        $unique = Get-QaUniqueDiagnostics -Diagnostics $blocking
        $runtimeAll = @($blocking | Where-Object { $_.PSObject.Properties['AfterCompile'] -and $_.AfterCompile })
        $phase = 'Script compile failed'
        if ($runtimeAll.Count -eq $blocking.Count) {
            $attributed = @($blocking | Where-Object { $_.PSObject.Properties['IsOurs'] -and $_.IsOurs })
            if ($attributed.Count -eq $blocking.Count) {
                $phase = 'Script runtime errors in BGONE'
            } else {
                $phase = 'Post-compile script errors that could not be attributed to vanilla (treated as blocking)'
            }
        }
        return [pscustomobject]@{
            ExitCode = $codes.Compile
            Reason   = ($phase + ': ' + $unique.Count + ' unique error(s) in ' + $blocking.Count + ' reported line(s).')
        }
    }

    if (-not $Findings.CompileCompleted) {
        if ($Findings.CompileStarted) {
            return [pscustomobject]@{
                ExitCode = $codes.Hang
                Reason   = 'Compile started but never produced a completion marker - treated as a hang.'
            }
        }
        return [pscustomobject]@{
            ExitCode = $codes.Hang
            Reason   = 'No compile-phase marker of any kind in the log - Workbench never reached script compilation.'
        }
    }

    $suppressed = @($Findings.Errors).Count
    if ($suppressed -gt 0) {
        return [pscustomobject]@{
            ExitCode = $codes.Clean
            Reason   = ('Compile clean - no blocking script errors (' + $suppressed + ' post-compile runtime line(s) in vanilla files reported above).')
        }
    }

    return [pscustomobject]@{
        ExitCode = $codes.Clean
        Reason   = 'Compile clean - no script errors.'
    }
}

function Format-QaDiagnostics {
    param($Diagnostics, [int]$Max = 50)
    $lines = New-Object System.Collections.ArrayList
    $unique = Get-QaUniqueDiagnostics -Diagnostics $Diagnostics
    $shown = 0
    foreach ($d in $unique) {
        if ($shown -ge $Max) {
            [void]$lines.Add(("  ... and " + ($unique.Count - $Max) + " more"))
            break
        }
        if ($d.File) {
            $ownerTag = ''
            if ($d.PSObject.Properties['Owner']) { $ownerTag = '  [' + $d.Owner + ']' }
            [void]$lines.Add(("  " + $d.File + ":" + $d.Line + ": " + $d.Message + $ownerTag))
        } else {
            [void]$lines.Add(("  " + $d.Raw))
        }
        $shown++
    }
    return @($lines.ToArray())
}

function Format-QaErrorsByFile {
    param($Diagnostics)
    $lines = New-Object System.Collections.ArrayList
    $unique = Get-QaUniqueDiagnostics -Diagnostics $Diagnostics
    $groups = $unique | Where-Object { $_.File } | Group-Object -Property File | Sort-Object -Property Count -Descending
    foreach ($g in $groups) {
        $ownerTag = ''
        $first = @($g.Group)[0]
        if ($first.PSObject.Properties['Owner']) { $ownerTag = '  [' + $first.Owner + ']' }
        [void]$lines.Add(("  " + $g.Count.ToString().PadLeft(4) + "  " + $g.Name + $ownerTag))
    }
    return @($lines.ToArray())
}

function Format-QaRunReport {
    param(
        $Findings,
        $Outcome,
        $Report = $null,
        $Resolution = $null,
        [string]$Enforce = 'warn',
        $MemoryStart = $null,
        $MemoryEnd = $null,
        [int]$MemoryWarnMb = 0
    )

    $out = New-Object System.Collections.ArrayList
    [void]$out.Add('')
    [void]$out.Add('--- COMPILE GATE ---------------------------------------------------------------')

    $started = 'no'; if ($Findings.CompileStarted) { $started = 'yes' }
    $done = 'no'; if ($Findings.CompileCompleted) { $done = 'yes' }
    $mod = 'no'; if ($Findings.ModuleLoaded) { $mod = 'yes' }

    [void]$out.Add(('  compile started: ' + $started + '   completed: ' + $done + '   Game module loaded: ' + $mod))
    if ($null -ne $Findings.InitMs) {
        [void]$out.Add(('  workbench init : ' + [math]::Round($Findings.InitMs / 1000.0, 1) + 's'))
    }
    $uniqueErrors = Get-QaUniqueDiagnostics -Diagnostics $Findings.Errors
    [void]$out.Add(('  script errors  : ' + $uniqueErrors.Count + ' unique (' + $Findings.Errors.Count + ' reported lines)'))
    $runtimeVanilla = @($Findings.Errors | Where-Object {
        $_.PSObject.Properties['AfterCompile'] -and $_.AfterCompile -and
        $_.PSObject.Properties['IsOurs'] -and (-not $_.IsOurs)
    })
    if ($runtimeVanilla.Count -gt 0) {
        [void]$out.Add(('    of which        : ' + (Get-QaUniqueDiagnostics -Diagnostics $runtimeVanilla).Count + ' unique post-compile runtime errors in vanilla files (non-blocking)'))
    }
    [void]$out.Add(('  script warnings: ' + (Get-QaUniqueDiagnostics -Diagnostics $Findings.Warnings).Count + ' unique'))
    [void]$out.Add(('  engine errors  : ' + $Findings.SetupErrors.Count))
    if ($Findings.HangDetected) {
        [void]$out.Add('  NOTE: the engine logged an "Application hangs" warning during this run.')
    }

    if ($Findings.SetupErrors.Count -gt 0) {
        [void]$out.Add('')
        [void]$out.Add('  ENGINE / INIT ERRORS:')
        $n = 0
        foreach ($e in $Findings.SetupErrors) {
            if ($n -ge 20) { [void]$out.Add(('    ... and ' + ($Findings.SetupErrors.Count - 20) + ' more')); break }
            [void]$out.Add(('    ' + $e.Raw))
            $n++
        }
    }

    $blockingErrors = @(Get-QaBlockingErrors -Diagnostics $Findings.Errors)
    if ($blockingErrors.Count -gt 0) {
        $ordered = @($blockingErrors | Sort-Object -Property LineNumber)
        $primary = $ordered[0]
        [void]$out.Add('')
        [void]$out.Add('  >> PRIMARY ERROR (fix this one first; later entries may be cascade noise):')
        if ($primary.File) {
            $ownerTag = ''
            if ($primary.PSObject.Properties['Owner']) { $ownerTag = '  [' + $primary.Owner + ']' }
            [void]$out.Add(('     ' + $primary.File + ':' + $primary.Line + ': ' + $primary.Message + $ownerTag))
        } else {
            [void]$out.Add(('     ' + $primary.Raw))
        }
        [void]$out.Add('')
        [void]$out.Add('  COMPILE ERRORS BY FILE:')
        foreach ($l in (Format-QaErrorsByFile -Diagnostics $Findings.Errors)) { [void]$out.Add($l) }
        [void]$out.Add('')
        [void]$out.Add('  COMPILE ERRORS (in log order):')
        foreach ($l in (Format-QaDiagnostics -Diagnostics $Findings.Errors -Max 50)) { [void]$out.Add($l) }
    }

    if ($null -ne $Resolution) {
        [void]$out.Add('')
        [void]$out.Add('--- ADDON RESOLUTION -----------------------------------------------------------')
        [void]$out.Add(('  resolved gproj entries: ' + @($Findings.ResolvedAddons).Count + '; matched BGONE: ' + @($Resolution.Matched).Count))
        if (-not $Resolution.Ok) {
            $label = 'WARN'
            if ($Enforce -eq 'fail') { $label = 'FAIL' }
            foreach ($i in $Resolution.Issues) { [void]$out.Add(('  ' + $label + ': ' + $i)) }
        } else {
            [void]$out.Add('  OK: BGONE resolved strictly from target worktree/staging junction, vanilla addon present.')
        }
    }

    if ($null -ne $Report) {
        [void]$out.Add('')
        [void]$out.Add('--- REPORT ---------------------------------------------------------------------')
        foreach ($l in (Format-QaTestTable -Report $Report)) { [void]$out.Add($l) }
    }

    if (($null -ne $MemoryStart) -or ($null -ne $MemoryEnd)) {
        [void]$out.Add('')
        [void]$out.Add('--- MACHINE --------------------------------------------------------------------')
        [void]$out.Add(('  free RAM start : ' + (Format-QaMemorySample -Sample $MemoryStart)))
        [void]$out.Add(('  free RAM end   : ' + (Format-QaMemorySample -Sample $MemoryEnd)))
        [void]$out.Add(('  free RAM delta : ' + (Format-QaMemoryDelta -Start $MemoryStart -End $MemoryEnd)))
        if ($null -ne $MemoryStart) {
            if (Test-QaMemoryLow -FreeMb $MemoryStart.FreeMb -ThresholdMb $MemoryWarnMb) {
                [void]$out.Add(('  LOW MEMORY AT LAUNCH (below ' + (Format-QaMemoryMb -Mb $MemoryWarnMb) + '): a FAILURE here is low-confidence.'))
            }
        }
    }

    [void]$out.Add('')
    [void]$out.Add('--- VERDICT --------------------------------------------------------------------')
    [void]$out.Add(('  exit ' + $Outcome.ExitCode + ': ' + $Outcome.Reason))
    return @($out.ToArray())
}

function Test-QaTeardownDiagnostic {
    param(
        $Diagnostic,
        [int]$VerdictLineNumber,
        [string[]]$Messages = @(),
        [string[]]$VmSites = @(),
        $VmExceptions = @()
    )

    if ($VerdictLineNumber -le 0) { return $false }
    $lineNo = [int]$Diagnostic.LineNumber
    if ($lineNo -le $VerdictLineNumber) { return $false }

    $text = [string]$Diagnostic.Raw
    if ($text -match 'Virtual Machine Exception') {
        $vm = @($VmExceptions | Where-Object { $_.LineNumber -eq $lineNo }) | Select-Object -First 1
        if ($null -eq $vm) { return $false }
        return (@($VmSites) -contains ($vm.Class + '.' + $vm.Function))
    }

    foreach ($m in @($Messages)) {
        if ((-not [string]::IsNullOrEmpty($m)) -and $text.Contains($m)) { return $true }
    }
    return $false
}
