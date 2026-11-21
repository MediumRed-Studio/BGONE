# QaArtifacts.ps1 - run directory layout, log/report discovery, root .rdb staleness check, summary + pruning.
# PowerShell 5.1 ONLY.

function New-QaRunContext {
    param([string]$WorktreePath, [string]$Engine, [datetime]$StartTime)
    $stamp = $StartTime.ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 6)
    $full = Resolve-QaFullPath -Path $WorktreePath
    if ($full -ne '') { $WorktreePath = $full }
    $qaRoot = Join-Path $WorktreePath '.qa'
    $runsRoot = Join-Path $qaRoot 'runs'
    $runDir = Join-Path $runsRoot $stamp
    $profileDir = Join-Path (Join-Path $runDir 'profile') $Engine
    return [pscustomobject]@{
        QaRoot     = $qaRoot
        RunsRoot   = $runsRoot
        RunDir     = $runDir
        ProfileDir = $profileDir
        Stamp      = $stamp
        Engine     = $Engine
    }
}

function Find-QaConsoleLog {
    param(
        [string[]]$SearchRoots,
        [string]$FileName = 'console.log',
        [datetime]$Since,
        [string]$OwnershipToken,
        $Patterns
    )

    $candidates = New-Object System.Collections.ArrayList
    foreach ($root in @($SearchRoots)) {
        if ([string]::IsNullOrWhiteSpace($root)) { continue }
        $r = Expand-QaPath -Path $root
        if (-not (Test-Path -LiteralPath $r)) { continue }
        $found = Get-ChildItem -LiteralPath $r -Filter $FileName -Recurse -File -ErrorAction SilentlyContinue
        foreach ($f in @($found)) {
            if ($Since -and ($f.LastWriteTime -lt $Since)) { continue }
            [void]$candidates.Add($f)
        }
    }

    if ($candidates.Count -eq 0) { return $null }

    $ordered = @($candidates.ToArray() | Sort-Object -Property LastWriteTime -Descending)
    foreach ($f in $ordered) {
        if ([string]::IsNullOrWhiteSpace($OwnershipToken)) { return $f.FullName }
        $lines = Read-QaLines -Path $f.FullName
        if (Test-QaLogBelongsToRun -Lines $lines -Token $OwnershipToken -Patterns $Patterns) {
            return $f.FullName
        }
    }
    return $null
}

function Get-QaStageProfileDir {
    param($Context, [string]$Slot)
    return (Join-Path (Join-Path $Context.RunDir 'profile') $Slot)
}

function Find-QaAutotestLogDir {
    param([string]$ProfileDir, [datetime]$Since)
    if ([string]::IsNullOrWhiteSpace($ProfileDir)) { return $null }
    $logsRoot = Join-Path $ProfileDir 'logs'
    if (-not (Test-Path -LiteralPath $logsRoot)) { return $null }
    $dirs = @(Get-ChildItem -LiteralPath $logsRoot -Directory -Filter 'logs_*' -ErrorAction SilentlyContinue)
    if ($Since) { $dirs = @($dirs | Where-Object { $_.LastWriteTime -ge $Since.AddMinutes(-1) }) }
    if ($dirs.Count -eq 0) { return $null }
    return (@($dirs | Sort-Object -Property LastWriteTime -Descending)[0]).FullName
}

function Copy-QaAutotestArtifacts {
    param([string]$LogDir, [string]$RunDir, [string[]]$FileNames)
    $copied = New-Object System.Collections.ArrayList
    if ([string]::IsNullOrWhiteSpace($LogDir)) { return @() }
    if (-not (Test-Path -LiteralPath $LogDir)) { return @() }
    foreach ($n in @($FileNames)) {
        if ([string]::IsNullOrWhiteSpace($n)) { continue }
        $src = Join-Path $LogDir $n
        if (-not (Test-Path -LiteralPath $src)) { continue }
        $dstName = $n
        if ($n -eq 'console.log') { $dstName = 'autotest-console.log' }
        if ($n -eq 'error.log')   { $dstName = 'autotest-error.log' }
        try {
            Copy-Item -LiteralPath $src -Destination (Join-Path $RunDir $dstName) -Force -ErrorAction Stop
            [void]$copied.Add($dstName)
        } catch {
            continue
        }
    }
    return @($copied.ToArray())
}

function Find-QaTestReport {
    param(
        [string[]]$SearchRoots,
        [string[]]$FileNames,
        [string[]]$ExcludePatterns,
        [datetime]$Since
    )

    foreach ($name in @($FileNames)) {
        foreach ($root in @($SearchRoots)) {
            if ([string]::IsNullOrWhiteSpace($root)) { continue }
            $r = Expand-QaPath -Path $root
            if (-not (Test-Path -LiteralPath $r)) { continue }
            $found = @(Get-ChildItem -LiteralPath $r -Filter $name -Recurse -File -ErrorAction SilentlyContinue)
            $found = @($found | Where-Object { (-not $Since) -or ($_.LastWriteTime -ge $Since) })
            if ($found.Count -gt 0) {
                return (@($found | Sort-Object -Property LastWriteTime -Descending)[0]).FullName
            }
        }
    }

    foreach ($root in @($SearchRoots)) {
        if ([string]::IsNullOrWhiteSpace($root)) { continue }
        $r = Expand-QaPath -Path $root
        if (-not (Test-Path -LiteralPath $r)) { continue }
        $found = @(Get-ChildItem -LiteralPath $r -Filter '*.xml' -Recurse -File -ErrorAction SilentlyContinue)
        $found = @($found | Where-Object { (-not $Since) -or ($_.LastWriteTime -ge $Since) })
        foreach ($ex in @($ExcludePatterns)) {
            if ([string]::IsNullOrWhiteSpace($ex)) { continue }
            $found = @($found | Where-Object { $_.Name -notlike $ex })
        }
        if ($found.Count -gt 0) {
            return (@($found | Sort-Object -Property LastWriteTime -Descending)[0]).FullName
        }
    }
    return $null
}

function Test-QaWorktreeSafety {
    param([string]$WorktreePath, $SafetyConfig)

    $issues = New-Object System.Collections.ArrayList
    $requireSpaceFree = $true
    $forbidden = @()
    if ($null -ne $SafetyConfig) {
        $v = Get-QaProp -Object $SafetyConfig -Name 'requireSpaceFreePath' -Default $true
        $requireSpaceFree = [bool]$v
        $forbidden = @(Get-QaProp -Object $SafetyConfig -Name 'forbiddenRootSubstrings' -Default @())
    }

    if ($requireSpaceFree -and ($WorktreePath -match '\s')) {
        [void]$issues.Add("Worktree path contains a space: '" + $WorktreePath + "'. The engine's CLI parser splits on spaces and silently truncates paths. Use a space-free path such as C:\wt\<slot>.")
    }
    foreach ($f in $forbidden) {
        if ([string]::IsNullOrWhiteSpace($f)) { continue }
        if ($WorktreePath.IndexOf($f, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            [void]$issues.Add("Worktree lives inside Workbench's addon auto-discovery tree ('" + $f + "'). Create the worktree outside that tree, e.g. C:\wt\<slot>.")
        }
    }

    return [pscustomobject]@{
        Ok     = ($issues.Count -eq 0)
        Issues = @($issues.ToArray())
    }
}

function Set-QaRdbSkipWorktree {
    param([string]$WorktreePath, [string]$PathSpec = 'resourceDatabase.rdb')
    $flagged = 0
    try {
        $unmerged = @(& git -C $WorktreePath ls-files -u -- $PathSpec | ForEach-Object { ($_ -split "`t", 2)[1] } | Sort-Object -Unique)
        foreach ($f in $unmerged) {
            & git -C $WorktreePath update-index --no-skip-worktree -- $f 2>$null
            Write-QaWarn ($f + ' is unmerged: cleared skip-worktree.')
        }
        $tracked = @(& git -C $WorktreePath ls-files -v -- $PathSpec)
        foreach ($line in $tracked) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $tag = $line.Substring(0, 1)
            $path = $line.Substring(2)
            if ($tag -eq 'S' -or $unmerged -contains $path) { continue }
            & git -C $WorktreePath update-index --skip-worktree -- $path
            if ($LASTEXITCODE -eq 0) { $flagged++ }
        }
    } catch {
        Write-QaWarn ('Could not set skip-worktree on .rdb files: ' + $_.Exception.Message)
    }
    return $flagged
}

function Write-QaDiscardRdbDeprecated {
    Write-QaWarn '-DiscardRdb is deprecated and does nothing: regenerated .rdb files stay in the worktree (skip-worktree, never committable).'
}

function Get-QaRdbListedPaths {
    param([string]$RdbPath)
    $set = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    if ([string]::IsNullOrWhiteSpace($RdbPath) -or -not (Test-Path -LiteralPath $RdbPath)) { return ,$set }
    $bytes = [System.IO.File]::ReadAllBytes($RdbPath)
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $start = -1
    for ($i = 0; $i -le $bytes.Length; $i++) {
        $isText = ($i -lt $bytes.Length) -and ($bytes[$i] -ge 0x20)
        if ($isText) {
            if ($start -lt 0) { $start = $i }
            continue
        }
        if ($start -ge 0) {
            [void]$set.Add($utf8.GetString($bytes, $start, $i - $start))
            $start = -1
        }
    }
    return ,$set
}

# Every file with a .meta beside it in one addon that its resourceDatabase.rdb does not list,
# skipping .qa/ and .git/ directories (single-addon worktree root carries .qa/ and .git/).
function Get-QaRdbMissingResources {
    param([string]$AddonDir)
    $root = (Resolve-Path -LiteralPath $AddonDir).ProviderPath.TrimEnd('\')
    $listed = Get-QaRdbListedPaths -RdbPath (Join-Path $root 'resourceDatabase.rdb')
    $missing = New-Object System.Collections.ArrayList
    foreach ($meta in @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.meta' -ErrorAction SilentlyContinue)) {
        $relMeta = $meta.FullName.Substring($root.Length + 1)
        if ($relMeta -like '.qa\*' -or $relMeta -like '.git\*') { continue }
        $resource = $meta.FullName.Substring(0, $meta.FullName.Length - 5)
        if (-not (Test-Path -LiteralPath $resource -PathType Leaf)) { continue }
        $rel = $resource.Substring($root.Length + 1).Replace('\', '/')
        if (-not $listed.Contains($rel)) { [void]$missing.Add($rel) }
    }
    return @($missing.ToArray() | Sort-Object)
}

# Supports single-addon worktree root (<AddonsDir>\addon.gproj) as well as multi-addon directories.
function Get-QaStaleRdbAddons {
    param([string]$AddonsDir, [string[]]$AddonIds)
    $stale = New-Object System.Collections.ArrayList
    if ([string]::IsNullOrWhiteSpace($AddonsDir) -or -not (Test-Path -LiteralPath $AddonsDir)) { return @() }

    $rootGproj = Join-Path $AddonsDir 'addon.gproj'
    if (Test-Path -LiteralPath $rootGproj) {
        $raw = ''
        try { $raw = Get-Content -LiteralPath $rootGproj -Raw -ErrorAction Stop } catch { return @() }
        if ($raw -match 'ID\s+"(?<id>[^"]+)"') {
            $id = $Matches['id']
            if ((@($AddonIds).Count -eq 0) -or (@($AddonIds) -contains $id)) {
                $missing = @(Get-QaRdbMissingResources -AddonDir $AddonsDir)
                if ($missing.Count -gt 0) {
                    $deps = 0
                    if ($raw -match '(?s)Dependencies\s*\{(?<deps>[^}]*)\}') { $deps = ([regex]::Matches($Matches['deps'], '"[0-9A-Fa-f]{16}"')).Count }
                    [void]$stale.Add([pscustomobject]@{ Addon = $id; Missing = $missing; Dependencies = $deps })
                }
            }
        }
        return @($stale.ToArray())
    }

    foreach ($d in @(Get-ChildItem -LiteralPath $AddonsDir -Directory -ErrorAction SilentlyContinue)) {
        $gproj = Join-Path $d.FullName 'addon.gproj'
        if (-not (Test-Path -LiteralPath $gproj)) { continue }
        $raw = ''
        try { $raw = Get-Content -LiteralPath $gproj -Raw -ErrorAction Stop } catch { continue }
        if ($raw -notmatch 'ID\s+"(?<id>[^"]+)"') { continue }
        if (@($AddonIds) -notcontains $Matches['id']) { continue }
        $missing = @(Get-QaRdbMissingResources -AddonDir $d.FullName)
        if ($missing.Count -gt 0) {
            $deps = 0
            if ($raw -match '(?s)Dependencies\s*\{(?<deps>[^}]*)\}') { $deps = ([regex]::Matches($Matches['deps'], '"[0-9A-Fa-f]{16}"')).Count }
            [void]$stale.Add([pscustomobject]@{ Addon = $d.Name; Missing = $missing; Dependencies = $deps })
        }
    }
    return @($stale.ToArray() | Sort-Object -Property @{ Expression = 'Dependencies'; Descending = $true }, 'Addon')
}

function Update-QaStaleRdb {
    param(
        [string]$WorktreePath,
        [string[]]$AddonIds,
        [object[]]$Stale,
        [string]$Engine,
        [string]$QaRunPath,
        [int]$QueueTimeoutSec,
        [string]$ConfigPath = '',
        [switch]$AcceptEngineVersion
    )
    foreach ($s in @($Stale)) {
        if (@(Get-QaStaleRdbAddons -AddonsDir $WorktreePath -AddonIds $AddonIds | Where-Object { $_.Addon -eq $s.Addon }).Count -eq 0) { continue }
        Write-QaStep ('Regenerating resourceDatabase.rdb: compile-only qa-run on ' + $s.Addon)
        $qaArgs = @(
            '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', $QaRunPath,
            '-WorktreePath', $WorktreePath,
            '-Engine', $Engine,
            '-Module', $s.Addon,
            '-QueueTimeoutSec', [string]$QueueTimeoutSec
        )
        if (-not [string]::IsNullOrWhiteSpace($ConfigPath)) {
            $qaArgs += @('-ConfigPath', $ConfigPath)
        }
        if ($AcceptEngineVersion) {
            $qaArgs += '-AcceptEngineVersion'
        }
        & powershell.exe @qaArgs | Out-Host
        Write-QaInfo ('qa-run exit ' + $LASTEXITCODE)
    }
    return @(Get-QaStaleRdbAddons -AddonsDir $WorktreePath -AddonIds $AddonIds)
}

function Remove-QaOldRuns {
    param([string]$RunsRoot, [int]$Keep = 20, [int]$MinAgeHours = 24)
    if ($Keep -le 0) { return @() }
    if (-not (Test-Path -LiteralPath $RunsRoot)) { return @() }
    $dirs = @(Get-ChildItem -LiteralPath $RunsRoot -Directory -ErrorAction SilentlyContinue | Sort-Object -Property Name -Descending)
    if ($dirs.Count -le $Keep) { return @() }
    $removed = New-Object System.Collections.ArrayList
    $cutoff = (Get-Date).AddHours(-1 * $MinAgeHours)
    for ($i = $Keep; $i -lt $dirs.Count; $i++) {
        if ($dirs[$i].LastWriteTime -gt $cutoff) { continue }
        try {
            Remove-Item -LiteralPath $dirs[$i].FullName -Recurse -Force -ErrorAction Stop
            [void]$removed.Add($dirs[$i].Name)
        } catch { }
    }
    return @($removed.ToArray())
}

function Remove-QaArtifacts {
    param([string]$WorktreePath)
    $qaRoot = Join-Path $WorktreePath '.qa'
    if (-not (Test-Path -LiteralPath $qaRoot)) { return $false }
    Remove-Item -LiteralPath $qaRoot -Recurse -Force -ErrorAction Stop
    return $true
}

function Write-QaSummary {
    param(
        [string]$Path,
        $Header,
        [string[]]$Body
    )
    $lines = New-Object System.Collections.ArrayList
    [void]$lines.Add('BGONE worktree QA run summary')
    [void]$lines.Add('=============================')
    foreach ($k in $Header.Keys) {
        [void]$lines.Add(($k.PadRight(18) + ': ' + [string]$Header[$k]))
    }
    [void]$lines.Add('')
    foreach ($b in @($Body)) { [void]$lines.Add($b) }
    Write-QaTextFile -Path $Path -Lines @($lines.ToArray())
}
