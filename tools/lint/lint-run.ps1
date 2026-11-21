<#
.SYNOPSIS
    Standalone static linter for MediumRed-Studio/BGONE (Windows PowerShell 5.1).

.DESCRIPTION
    Zero external dependencies. Runs Enforce Script, Reforger resource/GUID, UI layout,
    Diag isolation, vanilla attribute inheritance, and Markdown encoding rules.

    Exit codes:
      0 - zero error findings (warnings/info do not fail)
      1 - one or more error findings, or -SelfTest assertion failure

.PARAMETER Worktree
    Repository root to lint. Defaults to two levels above this script.

.PARAMETER Config
    Path to lint-rules.json. Defaults to lint-rules.json beside this script.

.PARAMETER SelfTest
    Run positive and negative fixture assertions under tools/lint/selftest/.

.EXAMPLE
    .\tools\lint\lint-run.ps1 -Worktree C:\wt\bgone-pr1-gov
.EXAMPLE
    .\tools\lint\lint-run.ps1 -SelfTest
#>
[CmdletBinding()]
param(
    [string]$Worktree,
    [string]$Config,
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:LintRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($Worktree)) {
    $Worktree = (Resolve-Path -LiteralPath (Join-Path $script:LintRoot '..\..')).Path
}
if ([string]::IsNullOrWhiteSpace($Config)) {
    $Config = Join-Path $script:LintRoot 'lint-rules.json'
}

# ---- Helpers (standalone, PowerShell 5.1 pure ASCII) ------------------------------------------

function Get-LintProp {
    param($Object, [string]$Name, $Default = $null)
    if ($null -eq $Object) { return $Default }
    if ($Object -is [System.Collections.IDictionary]) {
        if ($Object.Contains($Name)) { return $Object[$Name] }
        return $Default
    }
    $prop = $Object.PSObject.Properties[$Name]
    if ($null -eq $prop) { return $Default }
    return $prop.Value
}

function Write-LintStep {
    param([string]$Text)
    Write-Host ("=== " + $Text + " ===")
}

function Write-LintInfo {
    param([string]$Text)
    Write-Host ("  [info] " + $Text)
}

function Get-LintRelPath {
    param([string]$File, [string]$BaseRoot)
    $rel = $File
    if ((-not [string]::IsNullOrEmpty($BaseRoot)) -and $rel.StartsWith($BaseRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        $rel = $rel.Substring($BaseRoot.Length)
    }
    return ($rel -replace '\\', '/').TrimStart('/')
}

function Get-LintUniquePaths {
    param([string[]]$Paths)
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    $result = New-Object System.Collections.ArrayList
    foreach ($p in $Paths) {
        if (-not [string]::IsNullOrWhiteSpace($p) -and $seen.Add($p)) {
            [void]$result.Add($p)
        }
    }
    return @($result)
}

function Test-LintFileAllowed {
    param($RuleCfg, [string]$RelPath)
    foreach ($entry in @(Get-LintProp -Object $RuleCfg -Name 'allowFiles' -Default @())) {
        $pattern = $entry
        if ($entry -isnot [string]) { $pattern = Get-LintProp -Object $entry -Name 'file' -Default '' }
        if ([string]::IsNullOrWhiteSpace($pattern)) { continue }
        if ($RelPath -like ('*' + ($pattern -replace '\\', '/') + '*')) { return $true }
    }
    return $false
}

# ---- Per-run mutable state --------------------------------------------------------------------

function Reset-LintState {
    param([string]$Root)
    $script:WorktreeRoot      = $Root
    $script:Findings          = New-Object System.Collections.ArrayList
    $script:GuidOwners        = @{}   # GUID -> ArrayList of .meta full paths
    $script:GuidResourcePath  = @{}   # GUID -> normalized registered path from .meta Name
    $script:GuidAllowSeen     = @{}   # "file|ref" -> count
    $script:GuidBaselineSeen  = @{}   # "file|ref" -> count
    $script:VanillaAttrCache  = @{}   # manifest path -> parsed object
    $script:ModdedClassesSeen = @{}   # className -> $true
    $script:UiMenuBindings    = @{}   # className -> @{ Preset; LayoutRel; ConfFile; ConfLine }
    $script:UiLayoutNames     = @{}   # layoutFileLower -> @{ File; Names; VisibleLines; AlignHits }
    $script:UiClassLookups    = @{}   # className -> @( @{ Name; File; Line } )
    $script:UiRootReceivers   = @()
    $script:UiDynamicLookups  = 0
    $script:UiNonRootLookups  = 0
}

function Add-LintFinding {
    param(
        [string]$File,
        [int]$Line,
        [string]$Rule,
        [string]$Severity,
        [string]$Message,
        [string]$Detail
    )
    [void]$script:Findings.Add([pscustomobject]@{
        File     = $File
        Line     = $Line
        Rule     = $Rule
        Severity = $Severity
        Message  = $Message
        Detail   = $Detail
    })
}

# ---- Comment & string stripping ---------------------------------------------------------------

function Get-LintCommentStripped {
    param([string[]]$Lines, [bool]$IsScript)

    $result = New-Object System.Collections.ArrayList
    $inBlock = $false

    foreach ($line in $Lines) {
        $code = $line
        if ($IsScript) {
            if ($inBlock) {
                $end = $code.IndexOf('*/')
                if ($end -lt 0) { [void]$result.Add(''); continue }
                $code = $code.Substring($end + 2)
                $inBlock = $false
            }
            $start = $code.IndexOf('/*')
            while ($start -ge 0) {
                $end = $code.IndexOf('*/', $start + 2)
                if ($end -lt 0) {
                    $code = $code.Substring(0, $start)
                    $inBlock = $true
                    break
                }
                $code = $code.Remove($start, $end - $start + 2)
                $start = $code.IndexOf('/*')
            }
        }
        # Strip // comments while skipping // inside double-quoted string literals.
        $inStr = $false
        $esc = $false
        $cut = -1
        for ($c = 0; $c -lt $code.Length; $c++) {
            $ch = $code[$c]
            if ($esc) { $esc = $false; continue }
            if ($inStr) {
                if ($ch -eq '\') { $esc = $true }
                elseif ($ch -eq '"') { $inStr = $false }
                continue
            }
            if ($ch -eq '"') { $inStr = $true; continue }
            if (($ch -eq '/') -and (($c + 1) -lt $code.Length) -and ($code[$c + 1] -eq '/')) {
                $cut = $c
                break
            }
        }
        if ($cut -ge 0) { $code = $code.Substring(0, $cut) }
        [void]$result.Add($code)
    }

    return @($result)
}

function Get-LintBlankStrings {
    param([string]$Text)
    return [regex]::Replace($Text, '"(?:[^"\\\r\n]|\\.)*"', '""')
}

# ---- Glob expansion ---------------------------------------------------------------------------

function Get-LintGlobFiles {
    param([string]$Root, [string]$Glob)

    $segments = $Glob -split '/'
    $dirs = @($Root)

    for ($i = 0; $i -lt ($segments.Count - 1); $i++) {
        $seg = $segments[$i]
        $next = New-Object System.Collections.ArrayList
        if ($seg -eq '**') {
            foreach ($d in $dirs) {
                if (-not (Test-Path -LiteralPath $d)) { continue }
                [void]$next.Add($d)
                foreach ($sub in @(Get-ChildItem -LiteralPath $d -Directory -Recurse -ErrorAction SilentlyContinue)) {
                    [void]$next.Add($sub.FullName)
                }
            }
        }
        else {
            foreach ($d in $dirs) {
                if (-not (Test-Path -LiteralPath $d)) { continue }
                if ($seg.IndexOf('*') -ge 0) {
                    foreach ($sub in @(Get-ChildItem -LiteralPath $d -Directory -Filter $seg -ErrorAction SilentlyContinue)) {
                        [void]$next.Add($sub.FullName)
                    }
                }
                else {
                    $cand = Join-Path $d $seg
                    if (Test-Path -LiteralPath $cand -PathType Container) { [void]$next.Add($cand) }
                }
            }
        }
        $dirs = @(Get-LintUniquePaths -Paths @($next))
        if ($dirs.Count -eq 0) { return @() }
    }

    $leaf = $segments[$segments.Count - 1]
    $files = New-Object System.Collections.ArrayList
    foreach ($d in $dirs) {
        if (-not (Test-Path -LiteralPath $d)) { continue }
        foreach ($f in @(Get-ChildItem -LiteralPath $d -File -Filter $leaf -ErrorAction SilentlyContinue)) {
            [void]$files.Add($f.FullName)
        }
    }
    return @(Get-LintUniquePaths -Paths @($files))
}

function Get-LintTargets {
    param([string]$Root, $ScanCfg)

    $globs = @(Get-LintProp -Object $ScanCfg -Name 'fileGlobs' -Default @())
    $excludes = @(Get-LintProp -Object $ScanCfg -Name 'excludePathSubstrings' -Default @())

    $all = New-Object System.Collections.ArrayList
    foreach ($g in $globs) {
        foreach ($f in @(Get-LintGlobFiles -Root $Root -Glob $g)) {
            $skip = $false
            foreach ($ex in $excludes) {
                if ($f.IndexOf($ex, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) { $skip = $true; break }
            }
            if (-not $skip) { [void]$all.Add($f) }
        }
    }
    return @(Get-LintUniquePaths -Paths @($all) | Sort-Object)
}

# ---- GUID001: .meta index & local reference resolution ----------------------------------------

$script:MetaNameRegex = [regex]'Name\s+"\{([0-9A-Fa-f]{16})\}([^"]*)"'
$script:GuidRefRegex  = [regex]'\{([0-9A-Fa-f]{16})\}'

function Initialize-LintMetaIndex {
    param([string]$Root, $RuleCfg, [bool]$IsSelfTest)

    $excludeTop = @(Get-LintProp -Object $RuleCfg -Name 'excludeMetaTopDirs' -Default @('.git', '.qa', 'tools'))
    if ($IsSelfTest) { $excludeTop = @('.git') }

    $metas = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Filter '*.meta' -ErrorAction SilentlyContinue | Sort-Object FullName)
    foreach ($mf in $metas) {
        $rel = Get-LintRelPath -File $mf.FullName -BaseRoot $Root
        $top = $rel.Split('/')[0]
        if ($excludeTop -contains $top) { continue }

        $text = [System.IO.File]::ReadAllText($mf.FullName)
        $m = $script:MetaNameRegex.Match($text)
        if (-not $m.Success) { continue }

        $guid = $m.Groups[1].Value.ToUpper()
        $regPath = ($m.Groups[2].Value.Trim() -replace '\\', '/')

        if (-not $script:GuidOwners.ContainsKey($guid)) {
            $script:GuidOwners[$guid] = New-Object System.Collections.ArrayList
        }
        [void]$script:GuidOwners[$guid].Add($mf.FullName)

        if (-not $script:GuidResourcePath.ContainsKey($guid)) {
            $script:GuidResourcePath[$guid] = $regPath
        }
    }
}

function Invoke-LintRuleGuidResolution {
    param([string]$File, [string[]]$CodeLines, $RuleCfg)

    $severity = [string](Get-LintProp -Object $RuleCfg -Name 'severity' -Default 'error')
    $localPattern = [string](Get-LintProp -Object $RuleCfg -Name 'localPathPattern' -Default '(?i)BGONE')
    $allow = @(Get-LintProp -Object $RuleCfg -Name 'allowReferences' -Default @())
    $baseline = @(Get-LintProp -Object $RuleCfg -Name 'baseline' -Default @())

    $allowKeys = @()
    foreach ($entry in $allow) {
        $ef = [string](Get-LintProp -Object $entry -Name 'file' -Default '')
        $er = [string](Get-LintProp -Object $entry -Name 'ref' -Default '')
        if (($ef -ne '') -and ($er -ne '')) { $allowKeys += ($ef + '|' + $er) }
    }
    $baselineKeys = @()
    foreach ($entry in $baseline) {
        $ef = [string](Get-LintProp -Object $entry -Name 'file' -Default '')
        $er = [string](Get-LintProp -Object $entry -Name 'ref' -Default '')
        if (($ef -ne '') -and ($er -ne '')) { $baselineKeys += ($ef + '|' + $er) }
    }

    $fileRel = Get-LintRelPath -File $File -BaseRoot $script:WorktreeRoot

    for ($i = 0; $i -lt $CodeLines.Count; $i++) {
        $line = $CodeLines[$i]
        if ($line.IndexOf('{') -lt 0) { continue }

        foreach ($m in $script:GuidRefRegex.Matches($line)) {
            $after = $line.Substring($m.Index + $m.Length)
            # Only inspect resource refs of the form "{GUID}Path/To/File" (skip bare component/widget IDs "{GUID}").
            $rest = ($after -split '\s')[0].TrimEnd('"', "'", ';', ')', ',')
            $brace = $rest.IndexOf('{')
            if ($brace -ge 0) { $rest = $rest.Substring(0, $brace).TrimEnd('"', "'", ';', ')', ',') }
            if ([string]::IsNullOrWhiteSpace($rest)) { continue }
            if ($rest.IndexOf('/') -lt 0 -and $rest.IndexOf('.') -lt 0) { continue }

            $guid = $m.Groups[1].Value.ToUpper()
            $known = $script:GuidOwners.ContainsKey($guid)
            $restNorm = ($rest.Trim() -replace '\\', '/')
            $diskExists = Test-Path -LiteralPath (Join-Path $script:WorktreeRoot ($restNorm -replace '/', '\')) -PathType Leaf
            $isLocalPath = $diskExists -or ($restNorm -match $localPattern)

            if ((-not $known) -and (-not $isLocalPath)) { continue }

            $seenKey = ($fileRel + '|{' + $guid + '}' + $restNorm)
            if ($allowKeys -contains $seenKey) {
                if (-not $script:GuidAllowSeen.ContainsKey($seenKey)) { $script:GuidAllowSeen[$seenKey] = 0 }
                $script:GuidAllowSeen[$seenKey]++
                continue
            }

            $isBaselined = ($baselineKeys -contains $seenKey)

            if (-not $known) {
                if ($isBaselined) {
                    if (-not $script:GuidBaselineSeen.ContainsKey($seenKey)) { $script:GuidBaselineSeen[$seenKey] = 0 }
                    $script:GuidBaselineSeen[$seenKey]++
                    Write-LintInfo ("baselined known finding (GUID001): dangling ref " + $seenKey)
                    continue
                }
                Add-LintFinding -File $File -Line ($i + 1) -Rule 'GUID001_LocalRefMustResolve' -Severity $severity `
                    -Message ("local reference {" + $guid + "}" + $restNorm + " resolves to no worktree .meta") `
                    -Detail "  either the target was deleted/renamed or the GUID is mistyped."
                continue
            }

            $owners = $script:GuidOwners[$guid]
            if ($owners.Count -gt 1) {
                $ownerNames = @()
                foreach ($mp in $owners) { $ownerNames += (Get-LintRelPath -File $mp -BaseRoot $script:WorktreeRoot) }
                Add-LintFinding -File $File -Line ($i + 1) -Rule 'GUID001_LocalRefMustResolve' -Severity $severity `
                    -Message ("GUID {" + $guid + "} is declared by " + $owners.Count + " .meta files (" + ($ownerNames -join ', ') + ")") `
                    -Detail "  give each resource a unique 16-hex GUID."
                continue
            }

            $registered = [string]$script:GuidResourcePath[$guid]
            if ([string]::IsNullOrWhiteSpace($registered)) {
                Add-LintFinding -File $File -Line ($i + 1) -Rule 'GUID001_LocalRefMustResolve' -Severity $severity `
                    -Message (".meta Name for {" + $guid + "} has an empty path") `
                    -Detail ("  referenced here as {" + $guid + "}" + $restNorm)
                continue
            }

            if (-not $restNorm.Equals($registered, [System.StringComparison]::Ordinal)) {
                if ($isBaselined) {
                    if (-not $script:GuidBaselineSeen.ContainsKey($seenKey)) { $script:GuidBaselineSeen[$seenKey] = 0 }
                    $script:GuidBaselineSeen[$seenKey]++
                    Write-LintInfo ("baselined known finding (GUID001): path mismatch " + $seenKey + " vs meta '" + $registered + "'")
                    continue
                }
                Add-LintFinding -File $File -Line ($i + 1) -Rule 'GUID001_LocalRefMustResolve' -Severity $severity `
                    -Message ("reference path '" + $restNorm + "' does not match registered .meta path '" + $registered + "' for {" + $guid + "}") `
                    -Detail "  fix either the reference or the .meta Name field (case-sensitive comparison)."
            }
        }
    }
}

function Complete-LintRuleGuidResolution {
    param($RuleCfg, [bool]$IsSelfTest)

    $severity = [string](Get-LintProp -Object $RuleCfg -Name 'severity' -Default 'error')

    # Always report duplicate .meta GUIDs even if no file currently references them.
    foreach ($guid in ($script:GuidOwners.Keys | Sort-Object)) {
        $owners = $script:GuidOwners[$guid]
        if ($owners.Count -gt 1) {
            $ownerNames = @()
            foreach ($mp in $owners) { $ownerNames += (Get-LintRelPath -File $mp -BaseRoot $script:WorktreeRoot) }
            Add-LintFinding -File $owners[0] -Line 1 -Rule 'GUID001_LocalRefMustResolve' -Severity $severity `
                -Message ("duplicate .meta GUID {" + $guid + "} declared by " + $owners.Count + " files (" + ($ownerNames -join ', ') + ")") `
                -Detail "  each resource .meta must carry a unique 16-hex GUID."
        }
    }

    if ($IsSelfTest) { return }

    foreach ($entry in @(Get-LintProp -Object $RuleCfg -Name 'allowReferences' -Default @())) {
        $key = ([string](Get-LintProp -Object $entry -Name 'file' -Default '') + '|' + [string](Get-LintProp -Object $entry -Name 'ref' -Default ''))
        if (-not $script:GuidAllowSeen.ContainsKey($key)) {
            Add-LintFinding -File $script:LintConfigFile -Line 1 -Rule 'GUID001_LocalRefMustResolve' -Severity 'warn' `
                -Message ("stale GUID001 allowReferences entry: '" + $key + "' matched no reference") `
                -Detail "  remove this entry from lint-rules.json."
        }
    }

    foreach ($entry in @(Get-LintProp -Object $RuleCfg -Name 'baseline' -Default @())) {
        $key = ([string](Get-LintProp -Object $entry -Name 'file' -Default '') + '|' + [string](Get-LintProp -Object $entry -Name 'ref' -Default ''))
        if (-not $script:GuidBaselineSeen.ContainsKey($key)) {
            Add-LintFinding -File $script:LintConfigFile -Line 1 -Rule 'GUID001_LocalRefMustResolve' -Severity 'warn' `
                -Message ("stale GUID001 baseline entry: '" + $key + "' no longer violates") `
                -Detail "  the upstream ref/meta was fixed; remove this entry from GUID001 baseline in lint-rules.json."
        }
    }
}

# ---- MP001: server RPC identity derivation ----------------------------------------------------

$script:RpcAttrRegex       = [regex]'\[RplRpc\s*\(([^)]*)\)\]'
$script:RpcSigRegex        = [regex]'^\s*(?:protected|private|internal)?\s*[A-Za-z_][A-Za-z0-9_]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*$'
$script:IdentityParamRegex = [regex]'(?i)^(?:sender|actor|caller)?_?(?:player_?id|player_?uid|pid|user_?id)$'

function Invoke-LintRuleIdentityDerivation {
    param([string]$File, [string[]]$Lines, [string[]]$CodeLines, $RuleCfg)

    $severity = [string](Get-LintProp -Object $RuleCfg -Name 'severity' -Default 'error')
    $allow = @(Get-LintProp -Object $RuleCfg -Name 'allowHandlers' -Default @())
    $base = [System.IO.Path]::GetFileName($File)

    for ($i = 0; $i -lt $CodeLines.Count; $i++) {
        $m = $script:RpcAttrRegex.Match($CodeLines[$i])
        if (-not $m.Success) { continue }
        $attr = $m.Groups[1].Value
        if ($attr -notmatch 'RplRcver\.\s*Server') { continue }

        $sigIdx = $i + 1
        while ($sigIdx -lt $CodeLines.Count) {
            if ($CodeLines[$sigIdx].Trim() -eq '') { $sigIdx++; continue }
            break
        }
        if ($sigIdx -ge $CodeLines.Count) { continue }

        $sm = $script:RpcSigRegex.Match($CodeLines[$sigIdx])
        if (-not $sm.Success) { continue }
        $handler = $sm.Groups[1].Value
        $params  = $sm.Groups[2].Value

        if ($allow -contains ($base + '::' + $handler)) { continue }

        # Explicit MP001-EXEMPT marker in raw doc comment above [RplRpc].
        $exempt = $false
        $j = $i - 1
        while ($j -ge 0) {
            $t = $Lines[$j].Trim()
            if ($t -eq '' -or $t.StartsWith('//') -or $t.StartsWith('*') -or $t.StartsWith('/*')) {
                if ($t -match 'MP001-EXEMPT') { $exempt = $true; break }
                $j--
                continue
            }
            break
        }
        if ($exempt) { continue }

        $identityParam = $null
        foreach ($p in ($params -split ',')) {
            $p = $p.Trim()
            if ($p -eq '') { continue }
            $name = $null
            foreach ($tok in ($p -split '\s+')) {
                $tok = $tok.Trim('*', '&', '(', ')', '<', '>')
                if ($tok -match '^[A-Za-z_][A-Za-z0-9_]*$') { $name = $tok }
            }
            if (($null -ne $name) -and $script:IdentityParamRegex.IsMatch($name)) {
                $identityParam = $name
                break
            }
        }
        if ($null -eq $identityParam) { continue }

        $depth = 0
        $started = $false
        $body = ''
        $k = $sigIdx
        while ($k -lt $CodeLines.Count) {
            $t = Get-LintBlankStrings -Text $CodeLines[$k]
            $opens = ([regex]::Matches($t, '\{')).Count
            $closes = ([regex]::Matches($t, '\}')).Count
            if (-not $started) {
                if ($opens -gt 0) {
                    $started = $true
                    $depth = $opens - $closes
                    $brace = $t.IndexOf('{')
                    $body += $t.Substring($brace + 1) + "`n"
                    if ($depth -le 0) { break }
                }
            }
            else {
                $body += $t + "`n"
                $depth += $opens - $closes
                if ($depth -le 0) { break }
            }
            $k++
        }

        if ($body -match 'GetPlayerId\s*\(') { continue }

        Add-LintFinding -File $File -Line ($sigIdx + 1) -Rule 'MP001_IdentityDerivedNotWire' -Severity $severity `
            -Message ("server RPC handler '" + $handler + "' accepts acting identity param '" + $identityParam + "' without deriving GetPlayerId()") `
            -Detail ("  derive identity on the server from PlayerController.GetPlayerId(), or add MP001-EXEMPT / allowHandlers entry (" + $base + "::" + $handler + ") with justification.")
    }
}

# ---- BOUNDARY003: modded class strips vanilla attributes --------------------------------------

$script:ModdedDeclRegex  = [regex]'^\s*modded\s+(class|enum)\s+([A-Za-z_][A-Za-z0-9_]*)'
$script:AttrListEndRegex = [regex]'\]\s*$'

function Get-LintVanillaAttrManifest {
    param($RuleCfg)

    $rel = [string](Get-LintProp -Object $RuleCfg -Name 'vanillaManifest' -Default 'tools/lint/vanilla-class-attributes.json')
    $path = $rel
    if (-not [System.IO.Path]::IsPathRooted($path)) {
        $path = Join-Path $script:WorktreeRoot ($rel -replace '/', '\')
    }

    if ($script:VanillaAttrCache.ContainsKey($path)) { return $script:VanillaAttrCache[$path] }

    $entry = [pscustomobject]@{ Path = $path; Classes = $null; Provenance = $null; Count = 0; Error = $null }
    if (-not (Test-Path -LiteralPath $path)) {
        $entry.Error = "vanilla attribute manifest not found at " + $path
    }
    else {
        try {
            $json = (Get-Content -LiteralPath $path -Raw -ErrorAction Stop | ConvertFrom-Json)
            $classes = Get-LintProp -Object $json -Name 'classes'
            if ($null -eq $classes) {
                $entry.Error = "vanilla attribute manifest at " + $path + " has no 'classes' section"
            }
            else {
                $count = @($classes.PSObject.Properties).Count
                if ($count -eq 0) {
                    $entry.Error = "vanilla attribute manifest at " + $path + " lists no classes"
                }
                else {
                    $entry.Classes = $classes
                    $entry.Count = $count
                    $entry.Provenance = Get-LintProp -Object $json -Name '_provenance'
                }
            }
        }
        catch {
            $entry.Error = "vanilla attribute manifest at " + $path + " could not be parsed: " + $_.Exception.Message
        }
    }

    $script:VanillaAttrCache[$path] = $entry
    return $entry
}

function Test-LintDeclHasInheritedAttribute {
    param([string[]]$CodeLines, [int]$Index)

    $j = $Index - 1
    while (($j -ge 0) -and ([string]::IsNullOrWhiteSpace($CodeLines[$j]))) { $j-- }
    if ($j -lt 0) { return $false }
    if (-not $script:AttrListEndRegex.IsMatch($CodeLines[$j])) { return $false }

    $k = $j
    while (($k -ge 0) -and (-not $CodeLines[$k].TrimStart().StartsWith('['))) { $k-- }
    if ($k -lt 0) { return $false }

    $parts = @()
    for ($n = $k; $n -le $j; $n++) { $parts += $CodeLines[$n].Trim() }
    return (($parts -join ' ') -match 'InheritedAttribute')
}

function Invoke-LintRuleInheritedAttribute {
    param([string]$File, [string[]]$CodeLines, $RuleCfg)

    $severity = [string](Get-LintProp -Object $RuleCfg -Name 'severity' -Default 'error')
    $exempt = @(Get-LintProp -Object $RuleCfg -Name 'allowStrippedDeclarations' -Default @())

    $mf = Get-LintVanillaAttrManifest -RuleCfg $RuleCfg
    if ($null -ne $mf.Error) { return }

    for ($i = 0; $i -lt $CodeLines.Count; $i++) {
        $m = $script:ModdedDeclRegex.Match($CodeLines[$i])
        if (-not $m.Success) { continue }
        if ($m.Groups[1].Value -ne 'class') { continue }

        $name = $m.Groups[2].Value
        $script:ModdedClassesSeen[$name] = $true

        $attrs = Get-LintProp -Object $mf.Classes -Name $name
        if ([string]::IsNullOrWhiteSpace($attrs)) { continue }

        if (Test-LintDeclHasInheritedAttribute -CodeLines $CodeLines -Index $i) { continue }

        $isExempt = $false
        foreach ($e in $exempt) {
            if ((Get-LintProp -Object $e -Name 'declaration' -Default '') -eq $name) { $isExempt = $true; break }
        }
        if ($isExempt) { continue }

        Add-LintFinding -File $File -Line ($i + 1) -Rule 'BOUNDARY003_ModdedClassStripsVanillaAttributes' -Severity $severity `
            -Message ("modded class '" + $name + "' drops vanilla class attributes " + $attrs) `
            -Detail "  add [InheritedAttribute()] directly above the modded class declaration to preserve vanilla attributes."
    }
}

function Complete-LintRuleInheritedAttribute {
    param($RuleCfg, [bool]$IsSelfTest)

    $refresh = 'refresh with: .\tools\lint\vanilla-attributes-refresh.ps1 -VanillaSource "Z:\Reforger Source 1.9.0.37"'
    $mf = Get-LintVanillaAttrManifest -RuleCfg $RuleCfg

    if ($null -ne $mf.Error) {
        Add-LintFinding -File $mf.Path -Line 1 -Rule 'BOUNDARY003_ModdedClassStripsVanillaAttributes' -Severity 'error' `
            -Message ("BOUNDARY003 cannot run: " + $mf.Error) `
            -Detail ("  " + $refresh)
        return
    }

    $staleDays = [int](Get-LintProp -Object $RuleCfg -Name 'staleAfterDays' -Default 180)
    $generated = [string](Get-LintProp -Object $mf.Provenance -Name 'generatedUtc' -Default '')
    if (($staleDays -gt 0) -and (-not [string]::IsNullOrWhiteSpace($generated))) {
        $gen = [datetime]::MinValue
        if ([datetime]::TryParse($generated, [ref]$gen)) {
            $age = [int]((Get-Date).ToUniversalTime() - $gen.ToUniversalTime()).TotalDays
            if ($age -gt $staleDays) {
                Add-LintFinding -File $mf.Path -Line 1 -Rule 'BOUNDARY003_ModdedClassStripsVanillaAttributes' -Severity 'warn' `
                    -Message ("vanilla attribute manifest is " + $age + " days old (limit " + $staleDays + ")") `
                    -Detail ("  " + $refresh)
            }
        }
    }

    if ($IsSelfTest) { return }
    foreach ($e in @(Get-LintProp -Object $RuleCfg -Name 'allowStrippedDeclarations' -Default @())) {
        $decl = [string](Get-LintProp -Object $e -Name 'declaration' -Default '')
        if ([string]::IsNullOrWhiteSpace($decl)) { continue }
        if ($script:ModdedClassesSeen.ContainsKey($decl)) { continue }
        Add-LintFinding -File $script:LintConfigFile -Line 1 -Rule 'BOUNDARY003_ModdedClassStripsVanillaAttributes' -Severity 'warn' `
            -Message ("stale BOUNDARY003 allowStrippedDeclarations entry: '" + $decl + "' is not modded") `
            -Detail "  remove the stale exemption from lint-rules.json."
    }
}

# ---- UI001: widget name contract & fatal Reforger layout keywords -----------------------------

function Invoke-LintLayoutKeywordTraps {
    param([string]$File, [string[]]$Lines, $RuleCfg)

    $severity = [string](Get-LintProp -Object $RuleCfg -Name 'severity' -Default 'error')
    $slotStack = New-Object 'System.Collections.Generic.List[object]'
    $depth = 0

    for ($i = 0; $i -lt $Lines.Count; $i++) {
        $line = $Lines[$i]
        $ln = $i + 1

        $m = [regex]::Match($line, '\bSlot\s+(\w+)')
        $opensHere = ([regex]::Matches($line, '\{')).Count
        $closesHere = ([regex]::Matches($line, '\}')).Count

        if ($m.Success -and $opensHere -gt 0) {
            [void]$slotStack.Add(@{ Class = $m.Groups[1].Value; Depth = $depth })
        }

        if ($line -match '\bSlot\s+OverlaySlot\b') {
            Add-LintFinding -File $File -Line $ln -Rule 'UI001_WidgetNameContract' -Severity $severity `
                -Message "'OverlaySlot' is not a valid slot class in this Reforger build" `
                -Detail "  OverlayWidget children take LayoutSlot."
        }

        $sm = [regex]::Match($line, '\b(SizeX|SizeY)\b')
        if ($sm.Success -and $slotStack.Count -gt 0) {
            $inner = $slotStack[$slotStack.Count - 1]
            if ($inner.Class -eq 'LayoutSlot') {
                Add-LintFinding -File $File -Line $ln -Rule 'UI001_WidgetNameContract' -Severity $severity `
                    -Message ($sm.Groups[1].Value + " inside a LayoutSlot truncates the layout at this byte") `
                    -Detail "  SizeX/SizeY are FrameWidgetSlot-only; remove them from LayoutSlot."
            }
        }

        $depth += $opensHere - $closesHere
        while (($slotStack.Count -gt 0) -and ($slotStack[$slotStack.Count - 1].Depth -ge $depth)) {
            $slotStack.RemoveAt($slotStack.Count - 1)
        }
    }
}

function Invoke-LintRuleWidgetContract {
    param([string]$File, [string[]]$Lines, [string[]]$CodeLines, $RuleCfg)

    if ($script:UiRootReceivers.Count -eq 0) {
        $script:UiRootReceivers = @(Get-LintProp -Object $RuleCfg -Name 'rootReceivers' -Default @('m_wRoot'))
    }
    $presetGlob = [string](Get-LintProp -Object $RuleCfg -Name 'menuPresetConfigGlob' -Default '*chimeraMenus.conf')

    if ($File -like $presetGlob) {
        $preset = ''
        $layout = ''
        for ($i = 0; $i -lt $Lines.Count; $i++) {
            $l = $Lines[$i]
            $m = [regex]::Match($l, '^\s*MenuPreset\s+(\w+)')
            if ($m.Success) { $preset = $m.Groups[1].Value; $layout = ''; continue }
            $m = [regex]::Match($l, '^\s*Layout\s+"\{[0-9A-Fa-f]+\}([^"]+)"')
            if ($m.Success) { $layout = $m.Groups[1].Value; continue }
            $m = [regex]::Match($l, '^\s*Class\s+"(\w+)"')
            if (-not $m.Success) { continue }

            $cls = $m.Groups[1].Value
            if ([string]::IsNullOrWhiteSpace($layout)) { continue }
            $script:UiMenuBindings[$cls] = @{
                Preset = $preset; LayoutRel = $layout; ConfFile = $File; ConfLine = ($i + 1)
            }
        }
        return
    }

    if ($File -like '*.layout') {
        $text = ($CodeLines -join "`n")
        $names = New-Object 'System.Collections.Generic.HashSet[string]'
        foreach ($m in ([regex]'\bName\s+"([^"]+)"').Matches($text)) {
            [void]$names.Add($m.Groups[1].Value)
        }

        $visibleHits = @()
        $alignHits = @()
        for ($i = 0; $i -lt $CodeLines.Count; $i++) {
            if ($CodeLines[$i] -match '\bVisible\b\s+\S') { $visibleHits += ($i + 1) }
            foreach ($t in ([regex]'"Text\s+(?:Horizontal|Vertical)\s+Align"').Matches($CodeLines[$i])) {
                $alignHits += @{ Line = ($i + 1); Keyword = $t.Value }
            }
        }

        $key = (Split-Path -Leaf $File).ToLower()
        $script:UiLayoutNames[$key] = @{
            File = $File; Names = $names; VisibleLines = $visibleHits; AlignHits = $alignHits
        }

        Invoke-LintLayoutKeywordTraps -File $File -Lines $CodeLines -RuleCfg $RuleCfg
        return
    }

    if ($File -notlike '*.c') { return }

    $cur = ''
    for ($i = 0; $i -lt $CodeLines.Count; $i++) {
        $l = $CodeLines[$i]
        $m = [regex]::Match($l, '^\s*(?:modded\s+)?class\s+(\w+)')
        if ($m.Success) { $cur = $m.Groups[1].Value }
        if ($l -notmatch 'FindAnyWidget') { continue }

        foreach ($f in ([regex]'(\w+)\.FindAnyWidget\s*\(\s*("?)([^")]*)\2\s*\)').Matches($l)) {
            $receiver = $f.Groups[1].Value
            if ($f.Groups[2].Value -ne '"') {
                $script:UiDynamicLookups++
                continue
            }
            if ($script:UiRootReceivers -notcontains $receiver) { $script:UiNonRootLookups++; continue }
            if ([string]::IsNullOrWhiteSpace($cur)) { continue }
            if (-not $script:UiClassLookups.ContainsKey($cur)) { $script:UiClassLookups[$cur] = @() }
            $script:UiClassLookups[$cur] += @{ Name = $f.Groups[3].Value; File = $File; Line = ($i + 1) }
        }
    }
}

function Complete-LintRuleWidgetContract {
    param($RuleCfg, [bool]$IsSelfTest)

    $severity = [string](Get-LintProp -Object $RuleCfg -Name 'severity' -Default 'error')
    $baseline = @(Get-LintProp -Object $RuleCfg -Name 'baseline' -Default @())
    $baselineHit = New-Object 'System.Collections.Generic.HashSet[string]'
    $menuLayoutKeys = New-Object 'System.Collections.Generic.HashSet[string]'
    $unresolvedBindings = 0
    $checkedLookups = 0

    foreach ($cls in $script:UiMenuBindings.Keys) {
        $b = $script:UiMenuBindings[$cls]
        $key = (Split-Path -Leaf $b.LayoutRel).ToLower()

        if (-not $script:UiLayoutNames.ContainsKey($key)) {
            $unresolvedBindings++
            if (-not $IsSelfTest) {
                Write-LintInfo ("UI001: menu preset '" + $b.Preset + "' binds " + $cls + " to '" + $b.LayoutRel + "' (external/vanilla layout; widget contract unchecked)")
            }
            continue
        }

        [void]$menuLayoutKeys.Add($key)
        $layout = $script:UiLayoutNames[$key]
        if (-not $script:UiClassLookups.ContainsKey($cls)) { continue }

        foreach ($lookup in $script:UiClassLookups[$cls]) {
            $checkedLookups++
            if ($layout.Names.Contains($lookup.Name)) { continue }

            $bkey = $cls + '|' + $lookup.Name
            if ($baseline -contains $bkey) {
                [void]$baselineHit.Add($bkey)
                Write-LintInfo ("baselined known finding (UI001): " + $bkey)
                continue
            }

            Add-LintFinding -File $lookup.File -Line $lookup.Line -Rule 'UI001_WidgetNameContract' -Severity $severity `
                -Message ("FindAnyWidget(""" + $lookup.Name + """) has no matching widget in " + (Split-Path -Leaf $layout.File)) `
                -Detail ("  " + $cls + " is bound to that layout by menu preset '" + $b.Preset + "'.")
        }
    }

    foreach ($key in $script:UiLayoutNames.Keys) {
        $layout = $script:UiLayoutNames[$key]
        $isMenu = $menuLayoutKeys.Contains($key)

        foreach ($ln in $layout.VisibleLines) {
            if ($isMenu) {
                Add-LintFinding -File $layout.File -Line $ln -Rule 'UI001_WidgetNameContract' -Severity $severity `
                    -Message "the 'Visible' keyword is fatal in a menu layout" `
                    -Detail "  delete 'Visible' from the layout and call SetVisible() from script."
            }
            else {
                Add-LintFinding -File $layout.File -Line $ln -Rule 'UI001_WidgetNameContract' -Severity 'warn' `
                    -Message "the 'Visible' keyword is rejected by Reforger and does nothing in a non-menu layout" `
                    -Detail "  delete 'Visible' and call SetVisible() from script."
            }
        }

        foreach ($hit in $layout.AlignHits) {
            if ($isMenu) {
                Add-LintFinding -File $layout.File -Line $hit.Line -Rule 'UI001_WidgetNameContract' -Severity $severity `
                    -Message ("layout keyword " + $hit.Keyword + " is fatal in a menu layout") `
                    -Detail "  centre via OverlayWidgetClass + LayoutSlot { HorizontalAlign 1 VerticalAlign 1 }."
            }
            else {
                Add-LintFinding -File $layout.File -Line $hit.Line -Rule 'UI001_WidgetNameContract' -Severity 'warn' `
                    -Message ("layout keyword " + $hit.Keyword + " is rejected by Reforger") `
                    -Detail "  centre via OverlayWidgetClass + LayoutSlot { HorizontalAlign 1 VerticalAlign 1 }."
            }
        }
    }

    if (-not $IsSelfTest) {
        foreach ($bkey in $baseline) {
            if ($baselineHit.Contains($bkey)) { continue }
            Add-LintFinding -File $script:LintConfigFile -Line 1 -Rule 'UI001_WidgetNameContract' -Severity 'warn' `
                -Message ("stale UI001 baseline entry: '" + $bkey + "'") `
                -Detail "  remove the resolved entry from UI001 baseline in lint-rules.json."
        }
        Write-LintInfo ("UI001 coverage: " + $checkedLookups + " root-receiver literal lookup(s) checked across " + $menuLayoutKeys.Count + " local menu layout(s); " + $script:UiDynamicLookups + " dynamic, " + $script:UiNonRootLookups + " non-root, " + $unresolvedBindings + " external/vanilla binding(s).")
    }
}

# ---- DOCS001 / DOCS002 / DOCS003: Markdown encoding integrity ---------------------------------

$script:DocsCp1252Continuation = '[\u0080-\u00FF\u0152\u0153\u0160\u0161\u0178\u017D\u017E\u0192\u02C6\u02DC\u2013\u2014\u2018\u2019\u201A\u201C\u201D\u201E\u2020\u2021\u2022\u2026\u2030\u2039\u203A\u20AC\u2122]'
$script:DocsMojibakeRegex        = [regex]('[\u00C2\u00C3\u00E2]' + $script:DocsCp1252Continuation + '{1,2}')
$script:DocsTransliterationRegex = [regex]'\?\?'

function Get-LintDocsCodePoints {
    param([string]$Value)
    $parts = @()
    foreach ($ch in $Value.ToCharArray()) { $parts += ('U+{0:X4}' -f [int]$ch) }
    return ($parts -join ' ')
}

function Invoke-LintRuleDocsEncoding {
    param([string]$File, $Rules)

    $bytes = [System.IO.File]::ReadAllBytes($File)
    $utf8 = New-Object System.Text.UTF8Encoding($false, $false)
    $text = $utf8.GetString($bytes)

    $hasBom = ($bytes.Length -ge 3) -and ($bytes[0] -eq 0xEF) -and ($bytes[1] -eq 0xBB) -and ($bytes[2] -eq 0xBF)
    if (($text.Length -gt 0) -and ($text[0] -eq [char]0xFEFF)) { $text = $text.Substring(1) }
    $lines = @($text -split "`r?`n")
    $rel = Get-LintRelPath -File $File -BaseRoot $script:WorktreeRoot

    $patternRules = @(
        @{ Name = 'DOCS001_MojibakeSequence'; Regex = $script:DocsMojibakeRegex; CodePoints = $true
           Message = 'mojibake: UTF-8 read as cp1252 and re-encoded'
           Detail  = '  recover the original UTF-8 text rather than hand-retyping or deleting characters.' },
        @{ Name = 'DOCS002_TransliterationDamage'; Regex = $script:DocsTransliterationRegex; CodePoints = $false
           Message = 'transliteration damage: doubled question mark'
           Detail  = '  signature of lossy legacy-codepage transliteration; restore the original characters.' }
    )

    foreach ($pr in $patternRules) {
        $cfg = Get-LintProp -Object $Rules -Name $pr.Name
        if ($null -eq $cfg) { continue }
        if (-not (Get-LintProp -Object $cfg -Name 'enabled' -Default $true)) { continue }
        if (Test-LintFileAllowed -RuleCfg $cfg -RelPath $rel) { continue }

        $hits = New-Object System.Collections.ArrayList
        for ($i = 0; $i -lt $lines.Count; $i++) {
            foreach ($m in $pr.Regex.Matches($lines[$i])) {
                [void]$hits.Add([pscustomobject]@{ Line = ($i + 1); Value = $m.Value })
            }
        }
        if ($hits.Count -eq 0) { continue }

        $examples = @()
        $limit = [Math]::Min(2, $hits.Count - 1)
        for ($hIdx = 0; $hIdx -le $limit; $hIdx++) {
            $h = $hits[$hIdx]
            if ($pr.CodePoints) { $examples += ('line ' + $h.Line + ': ' + (Get-LintDocsCodePoints -Value $h.Value)) }
            else { $examples += ('line ' + $h.Line + ': ' + $h.Value) }
        }

        Add-LintFinding -File $File -Line $hits[0].Line -Rule $pr.Name `
            -Severity ([string](Get-LintProp -Object $cfg -Name 'severity' -Default 'error')) `
            -Message ($pr.Message + ' - ' + $hits.Count + ' occurrence(s)') `
            -Detail ($pr.Detail + "`n  first " + $examples.Count + ': ' + ($examples -join '; '))
    }

    $bomCfg = Get-LintProp -Object $Rules -Name 'DOCS003_Utf8Bom'
    if (($null -ne $bomCfg) -and (Get-LintProp -Object $bomCfg -Name 'enabled' -Default $true) -and $hasBom `
        -and (-not (Test-LintFileAllowed -RuleCfg $bomCfg -RelPath $rel))) {
        Add-LintFinding -File $File -Line 1 -Rule 'DOCS003_Utf8Bom' `
            -Severity ([string](Get-LintProp -Object $bomCfg -Name 'severity' -Default 'error')) `
            -Message 'file starts with a UTF-8 BOM (EF BB BF)' `
            -Detail '  re-save as UTF-8 without BOM.'
    }
}

# ---- ENF001 / ENF002 / ENF003 / ENF004: Enforce Script pre-compile guards ---------------------

$script:EnfArrayCompoundRegex = [regex]'\]\s*(?:\+=|-=|\*=|/=)'
$script:EnfTernaryRegex       = [regex]'\?[^;\{\}]*:'

function Invoke-LintRuleEnforceGuards {
    param([string]$File, [string[]]$CodeLines, $Rules)

    $rel = Get-LintRelPath -File $File -BaseRoot $script:WorktreeRoot

    $enf1 = Get-LintProp -Object $Rules -Name 'ENF001_StackedAttributes'
    $run1 = ($null -ne $enf1) -and (Get-LintProp -Object $enf1 -Name 'enabled' -Default $true) -and (-not (Test-LintFileAllowed -RuleCfg $enf1 -RelPath $rel))
    $sev1 = [string](Get-LintProp -Object $enf1 -Name 'severity' -Default 'error')

    $enf2 = Get-LintProp -Object $Rules -Name 'ENF002_ArrayCompoundAssign'
    $run2 = ($null -ne $enf2) -and (Get-LintProp -Object $enf2 -Name 'enabled' -Default $true) -and (-not (Test-LintFileAllowed -RuleCfg $enf2 -RelPath $rel))
    $sev2 = [string](Get-LintProp -Object $enf2 -Name 'severity' -Default 'error')

    $enf3 = Get-LintProp -Object $Rules -Name 'ENF003_NoTernary'
    $run3 = ($null -ne $enf3) -and (Get-LintProp -Object $enf3 -Name 'enabled' -Default $true) -and (-not (Test-LintFileAllowed -RuleCfg $enf3 -RelPath $rel))
    $sev3 = [string](Get-LintProp -Object $enf3 -Name 'severity' -Default 'error')

    $prevWasCompleteAttr = $false
    $prevAttrLine = 0

    for ($i = 0; $i -lt $CodeLines.Count; $i++) {
        $blanked = Get-LintBlankStrings -Text $CodeLines[$i]
        $trimmed = $blanked.Trim()

        if ($run1) {
            if ($trimmed -ne '') {
                $startsBracket = $trimmed.StartsWith('[')
                $endsBracket   = $trimmed.EndsWith(']')
                if ($startsBracket -and $prevWasCompleteAttr) {
                    Add-LintFinding -File $File -Line ($i + 1) -Rule 'ENF001_StackedAttributes' -Severity $sev1 `
                        -Message ("stacked attribute decorator after attribute on line " + $prevAttrLine) `
                        -Detail "  Enforce Script rejects stacked [...] decorators (such as [Attribute] + [RplProp]). Use a single decorator or split config and replicated fields."
                }
                if ($startsBracket -and $endsBracket) {
                    $prevWasCompleteAttr = $true
                    $prevAttrLine = $i + 1
                }
                else {
                    $prevWasCompleteAttr = $false
                }
            }
        }

        if ($run2 -and $script:EnfArrayCompoundRegex.IsMatch($blanked)) {
            Add-LintFinding -File $File -Line ($i + 1) -Rule 'ENF002_ArrayCompoundAssign' -Severity $sev2 `
                -Message "compound assignment on array element accessor is rejected by Enforce Script" `
                -Detail "  expand 'arr[i] += x' to 'arr[i] = arr[i] + x;'."
        }

        if ($run3 -and $script:EnfTernaryRegex.IsMatch($blanked)) {
            Add-LintFinding -File $File -Line ($i + 1) -Rule 'ENF003_NoTernary' -Severity $sev3 `
                -Message "ternary operator (?:) is not supported in Enforce Script" `
                -Detail "  replace with an explicit if/else statement."
        }
    }
}

function Complete-LintRuleScriptsLowercase {
    param([string]$Root, $RuleCfg)

    $severity = [string](Get-LintProp -Object $RuleCfg -Name 'severity' -Default 'error')
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return }

    foreach ($dirPath in [System.IO.Directory]::GetDirectories($Root)) {
        $leaf = [System.IO.Path]::GetFileName($dirPath)
        if (($leaf -ieq 'scripts') -and ($leaf -cne 'scripts')) {
            Add-LintFinding -File $dirPath -Line 1 -Rule 'ENF004_ScriptsLowercase' -Severity $severity `
                -Message ("script directory '" + $leaf + "' must be lowercase 'scripts'") `
                -Detail "  case-sensitive Linux dedicated servers fail to load scripts from 'Scripts/'."
        }
    }
}

# ---- DIAG001: shipped code must not reference Diag symbols ------------------------------------

$script:DiagKeywordWords = @('static', 'ref', 'protected', 'private', 'public', 'void', 'const', 'owned', 'out', 'inout', 'notnull', 'autoptr', 'proto', 'native', 'event', 'volatile', 'sealed', 'class', 'enum', 'typedef', 'modded', 'override', 'new', 'return')

function Get-LintDiagLastIdent {
    param([string]$Fragment)
    $m = [regex]::Matches($Fragment, '[A-Za-z_]\w*')
    if ($m.Count -eq 0) { return $null }
    return $m[$m.Count - 1].Value
}

function Get-LintDiagMemberNames {
    param([string]$Stmt, [string]$ClassName)

    $s = [regex]::Replace($Stmt, '\[[^\]]*\]', ' ').Trim()
    if ($s -eq '') { return @() }
    if ($s -match '\boverride\b') { return @() }
    if ($s.StartsWith('~')) { return @() }

    $paren = $s.IndexOf('(')
    $eq = $s.IndexOf('=')
    $names = @()
    if (($paren -ge 0) -and (($eq -lt 0) -or ($paren -lt $eq))) {
        $name = Get-LintDiagLastIdent -Fragment $s.Substring(0, $paren)
        if ($name -and ($name -ne $ClassName) -and ($script:DiagKeywordWords -notcontains $name)) { $names += $name }
        return $names
    }

    $decl = $s
    if ($eq -ge 0) { $decl = $s.Substring(0, $eq) }
    $decl = [regex]::Replace($decl, '<[^<>]*>', ' ')
    $decl = [regex]::Replace($decl, '<[^<>]*>', ' ')
    $first = $true
    foreach ($piece in $decl.Split(',')) {
        $piece = [regex]::Replace($piece, '\[[^\]]*\]', ' ')
        $name = Get-LintDiagLastIdent -Fragment $piece
        if ($first -and $name) {
            $words = [regex]::Matches($piece, '[A-Za-z_]\w*')
            if ($words.Count -lt 2) { $name = $null }
        }
        $first = $false
        if ($name -and ($script:DiagKeywordWords -notcontains $name)) { $names += $name }
    }
    return $names
}

function Get-LintDiagSymbols {
    param([string]$File)

    $lines = [regex]::Split([System.IO.File]::ReadAllText($File), '\r?\n')
    $code = Get-LintCommentStripped -Lines $lines -IsScript $true
    $kept = New-Object System.Collections.ArrayList
    foreach ($cl in $code) {
        if ($cl.TrimStart().StartsWith('#')) { [void]$kept.Add(''); continue }
        [void]$kept.Add($cl)
    }
    $text = Get-LintBlankStrings -Text ($kept -join "`n")

    $defs = New-Object System.Collections.ArrayList
    $members = New-Object System.Collections.ArrayList
    $cur = New-Object System.Text.StringBuilder
    $depth = 0
    $ctxKind = ''
    $ctxModded = $false
    $ctxName = ''

    $classRx = [regex]'^\s*(?:\[[^\]]*\]\s*)*(modded\s+)?(?:sealed\s+)?(class|enum)\s+(\w+)'
    $typedefRx = [regex]'^\s*typedef\s+.*?(\w+)\s*$'

    foreach ($ch in $text.ToCharArray()) {
        if ($ch -eq '{') {
            if ($depth -eq 0) {
                $hdr = $cur.ToString()
                $m = $classRx.Match($hdr)
                if ($m.Success) {
                    $ctxKind = $m.Groups[2].Value
                    $ctxModded = $m.Groups[1].Success
                    $ctxName = $m.Groups[3].Value
                    if (-not $ctxModded) { [void]$defs.Add(@{ Name = $ctxName; Kind = $ctxKind }) }
                }
                else {
                    $ctxKind = 'other'; $ctxModded = $false; $ctxName = ''
                    $p = $hdr.IndexOf('(')
                    if ($p -gt 0) {
                        $fn = Get-LintDiagLastIdent -Fragment $hdr.Substring(0, $p)
                        if ($fn -and ($script:DiagKeywordWords -notcontains $fn)) { [void]$defs.Add(@{ Name = $fn; Kind = 'function' }) }
                    }
                }
            }
            elseif (($depth -eq 1) -and ($ctxKind -eq 'class') -and $ctxModded) {
                foreach ($n in (Get-LintDiagMemberNames -Stmt $cur.ToString() -ClassName $ctxName)) {
                    [void]$members.Add(@{ Name = $n; Class = $ctxName })
                }
            }
            $depth++
            [void]$cur.Clear()
            continue
        }
        if ($ch -eq '}') {
            if ($depth -gt 0) { $depth-- }
            [void]$cur.Clear()
            continue
        }
        if ($depth -ge 2) { continue }
        if ($ch -eq ';') {
            $stmt = $cur.ToString()
            if ($depth -eq 1) {
                if (($ctxKind -eq 'class') -and $ctxModded) {
                    foreach ($n in (Get-LintDiagMemberNames -Stmt $stmt -ClassName $ctxName)) {
                        [void]$members.Add(@{ Name = $n; Class = $ctxName })
                    }
                }
            }
            elseif ($depth -eq 0) {
                $tm = $typedefRx.Match($stmt)
                if ($tm.Success) { [void]$defs.Add(@{ Name = $tm.Groups[1].Value; Kind = 'typedef' }) }
                else {
                    $p = $stmt.IndexOf('(')
                    $e = $stmt.IndexOf('=')
                    $cut = -1
                    if ($p -ge 0) { $cut = $p }
                    if (($e -ge 0) -and (($cut -lt 0) -or ($e -lt $cut))) { $cut = $e }
                    if ($cut -lt 0) { $cut = $stmt.Length }
                    $gn = Get-LintDiagLastIdent -Fragment $stmt.Substring(0, $cut)
                    $wc = [regex]::Matches($stmt.Substring(0, $cut), '[A-Za-z_]\w*').Count
                    if ($gn -and ($wc -ge 2) -and ($script:DiagKeywordWords -notcontains $gn)) {
                        [void]$defs.Add(@{ Name = $gn; Kind = 'global' })
                    }
                }
            }
            [void]$cur.Clear()
            continue
        }
        [void]$cur.Append($ch)
    }

    return @{ Defs = $defs; Members = $members }
}

function Complete-LintRuleDiagIsolation {
    param([string]$ScanRoot, $RuleCfg, [bool]$IsSelfTest)

    $severity = [string](Get-LintProp -Object $RuleCfg -Name 'severity' -Default 'error')
    $diagSeg  = [string](Get-LintProp -Object $RuleCfg -Name 'diagFolderSegment' -Default '/scripts/Game/Diag/')
    $excludeTop = @(Get-LintProp -Object $RuleCfg -Name 'excludeTopDirs' -Default @('.git', '.qa', '.agents', '.claude', '.agent', 'tools'))
    if ($IsSelfTest) { $excludeTop = @('.git') }
    $exts  = @(Get-LintProp -Object $RuleCfg -Name 'scanExtensions' -Default @('.c', '.et', '.conf', '.layout', '.ct', '.layer'))
    $allow = @(Get-LintProp -Object $RuleCfg -Name 'allowReferences' -Default @())

    if (-not (Test-Path -LiteralPath $ScanRoot)) { return }
    $rootFull = (Resolve-Path -LiteralPath $ScanRoot).Path

    $diagFiles = New-Object System.Collections.ArrayList
    $shipped   = New-Object System.Collections.ArrayList

    foreach ($f in @(Get-ChildItem -LiteralPath $rootFull -Recurse -File -ErrorAction SilentlyContinue)) {
        if ($exts -notcontains $f.Extension.ToLower()) { continue }
        $rel = Get-LintRelPath -File $f.FullName -BaseRoot $rootFull
        $top = $rel.Split('/')[0]
        if ($excludeTop -contains $top) { continue }

        if (('/' + $rel) -like ('*' + $diagSeg + '*')) {
            if ($f.Extension -ieq '.c') { [void]$diagFiles.Add(@{ Full = $f.FullName; Rel = $rel }) }
        }
        else {
            [void]$shipped.Add(@{ Full = $f.FullName; Rel = $rel })
        }
    }

    $symbols = @{}
    foreach ($d in $diagFiles) {
        $parsed = Get-LintDiagSymbols -File $d.Full
        foreach ($def in $parsed.Defs) {
            if (-not $symbols.ContainsKey($def.Name)) { $symbols[$def.Name] = ($def.Kind + " defined in " + $d.Rel) }
        }
        foreach ($mem in $parsed.Members) {
            if (-not $symbols.ContainsKey($mem.Name)) { $symbols[$mem.Name] = ("member added to modded " + $mem.Class + " in " + $d.Rel) }
        }
    }
    if ($symbols.Count -eq 0) { return }

    $alt = (($symbols.Keys | Sort-Object { $_.Length } -Descending | ForEach-Object { [regex]::Escape($_) }) -join '|')
    $rx = New-Object System.Text.RegularExpressions.Regex(('(?<![A-Za-z0-9_])(?:' + $alt + ')(?![A-Za-z0-9_])'), [System.Text.RegularExpressions.RegexOptions]::Compiled)

    foreach ($s in $shipped) {
        $raw = [System.IO.File]::ReadAllText($s.Full)
        if (-not $rx.IsMatch($raw)) { continue }
        $lines = [regex]::Split($raw, '\r?\n')
        $code = $lines
        if ($s.Full -like '*.c') { $code = Get-LintCommentStripped -Lines $lines -IsScript $true }

        $seen = @{}
        for ($i = 0; $i -lt $code.Count; $i++) {
            foreach ($m in $rx.Matches($code[$i])) {
                $sym = $m.Value
                if ($seen.ContainsKey($sym)) { continue }
                $seen[$sym] = $true
                $allowed = $false
                foreach ($a in $allow) {
                    if (((Get-LintProp -Object $a -Name 'file' -Default '') -eq $s.Rel) -and ((Get-LintProp -Object $a -Name 'symbol' -Default '') -eq $sym)) {
                        $allowed = $true
                        break
                    }
                }
                if ($allowed) { continue }
                Add-LintFinding -File $s.Full -Line ($i + 1) -Rule 'DIAG001_ShippedCodeReferencesDiag' -Severity $severity `
                    -Message ("shipped file references Diag symbol '" + $sym + "' (" + $symbols[$sym] + ")") `
                    -Detail "  scripts/Game/Diag/ is stripped on main; move shared symbols out of Diag or remove the reference."
            }
        }
    }
}

# ---- Per-file dispatcher ----------------------------------------------------------------------

function Invoke-LintFile {
    param([string]$File, $Rules)

    if ($File -like '*.md') {
        Invoke-LintRuleDocsEncoding -File $File -Rules $Rules
        return
    }

    $isScript = ($File -like '*.c')
    $raw = [System.IO.File]::ReadAllText($File)
    $lines = [regex]::Split($raw, '\r?\n')
    $codeLines = Get-LintCommentStripped -Lines $lines -IsScript $isScript

    $guidRule = Get-LintProp -Object $Rules -Name 'GUID001_LocalRefMustResolve'
    if (($null -ne $guidRule) -and (Get-LintProp -Object $guidRule -Name 'enabled' -Default $true)) {
        if (($File -like '*.c') -or ($File -like '*.layout') -or ($File -like '*.et') -or ($File -like '*.conf') -or ($File -like '*.layer')) {
            Invoke-LintRuleGuidResolution -File $File -CodeLines $codeLines -RuleCfg $guidRule
        }
    }

    $uiRule = Get-LintProp -Object $Rules -Name 'UI001_WidgetNameContract'
    if (($null -ne $uiRule) -and (Get-LintProp -Object $uiRule -Name 'enabled' -Default $true)) {
        if (($File -like '*.c') -or ($File -like '*.layout') -or ($File -like '*.conf')) {
            Invoke-LintRuleWidgetContract -File $File -Lines $lines -CodeLines $codeLines -RuleCfg $uiRule
        }
    }

    if ($isScript) {
        $mpRule = Get-LintProp -Object $Rules -Name 'MP001_IdentityDerivedNotWire'
        if (($null -ne $mpRule) -and (Get-LintProp -Object $mpRule -Name 'enabled' -Default $true)) {
            Invoke-LintRuleIdentityDerivation -File $File -Lines $lines -CodeLines $codeLines -RuleCfg $mpRule
        }

        $b3Rule = Get-LintProp -Object $Rules -Name 'BOUNDARY003_ModdedClassStripsVanillaAttributes'
        if (($null -ne $b3Rule) -and (Get-LintProp -Object $b3Rule -Name 'enabled' -Default $true)) {
            Invoke-LintRuleInheritedAttribute -File $File -CodeLines $codeLines -RuleCfg $b3Rule
        }

        Invoke-LintRuleEnforceGuards -File $File -CodeLines $codeLines -Rules $Rules
    }
}

# ---- SelfTest harness -------------------------------------------------------------------------

function Invoke-LintSelfTest {
    param($Rules)

    $selfRoot = Join-Path $script:LintRoot 'selftest'
    if (-not (Test-Path -LiteralPath $selfRoot)) {
        throw ("SelfTest fixture directory not found: " + $selfRoot)
    }

    Write-LintStep "BGONE lint self-test"
    $passed = 0
    $failed = 0

    function Assert-LintCase {
        param([string]$Name, [bool]$Condition, [string]$Detail)
        if ($Condition) {
            Write-Host ("  [PASS] " + $Name)
            $script:SelfPassed++
        }
        else {
            Write-Host ("  [FAIL] " + $Name + " - " + $Detail)
            $script:SelfFailed++
        }
    }
    $script:SelfPassed = 0
    $script:SelfFailed = 0

    # --- 1. DIAG001 positive & negative ---
    $diagRule = Get-LintProp -Object $Rules -Name 'DIAG001_ShippedCodeReferencesDiag'
    Reset-LintState -Root (Join-Path $selfRoot 'diag\bad')
    Complete-LintRuleDiagIsolation -ScanRoot (Join-Path $selfRoot 'diag\bad') -RuleCfg $diagRule -IsSelfTest $true
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'DIAG001_ShippedCodeReferencesDiag' -and $_.Severity -eq 'error' })
    Assert-LintCase 'DIAG001 bad (shipped refs Diag class & modded member)' ($hits.Count -ge 2) ("expected >=2 findings, got " + $hits.Count)

    Reset-LintState -Root (Join-Path $selfRoot 'diag\clean')
    Complete-LintRuleDiagIsolation -ScanRoot (Join-Path $selfRoot 'diag\clean') -RuleCfg $diagRule -IsSelfTest $true
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'DIAG001_ShippedCodeReferencesDiag' })
    Assert-LintCase 'DIAG001 clean (no shipped refs to Diag)' ($hits.Count -eq 0) ("expected 0 findings, got " + $hits.Count)

    # --- 2. MP001 positive & negative ---
    $mpRule = Get-LintProp -Object $Rules -Name 'MP001_IdentityDerivedNotWire'
    Reset-LintState -Root $selfRoot
    Invoke-LintFile -File (Join-Path $selfRoot 'mp001\bad_rpc.c') -Rules $Rules
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'MP001_IdentityDerivedNotWire' -and $_.Severity -eq 'error' })
    Assert-LintCase 'MP001 bad (wire playerId without GetPlayerId)' ($hits.Count -eq 1) ("expected 1 finding, got " + $hits.Count)

    Reset-LintState -Root $selfRoot
    Invoke-LintFile -File (Join-Path $selfRoot 'mp001\clean_rpc.c') -Rules $Rules
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'MP001_IdentityDerivedNotWire' })
    Assert-LintCase 'MP001 clean (derived GetPlayerId, exempt, targetPlayerId)' ($hits.Count -eq 0) ("expected 0 findings, got " + $hits.Count)

    # --- 3. BOUNDARY003 positive, negative, and missing-manifest error ---
    $b3Rule = Get-LintProp -Object $Rules -Name 'BOUNDARY003_ModdedClassStripsVanillaAttributes'
    $manifestFull = Join-Path $Worktree ((Get-LintProp -Object $b3Rule -Name 'vanillaManifest' -Default 'tools/lint/vanilla-class-attributes.json') -replace '/', '\')
    $b3Local = [pscustomobject]@{
        enabled                   = $true
        severity                  = 'error'
        vanillaManifest           = $manifestFull
        staleAfterDays            = 180
        allowStrippedDeclarations = @()
    }
    Reset-LintState -Root $selfRoot
    $raw = [System.IO.File]::ReadAllText((Join-Path $selfRoot 'boundary003\bad_modded.c'))
    $code = Get-LintCommentStripped -Lines ([regex]::Split($raw, '\r?\n')) -IsScript $true
    Invoke-LintRuleInheritedAttribute -File (Join-Path $selfRoot 'boundary003\bad_modded.c') -CodeLines $code -RuleCfg $b3Local
    Complete-LintRuleInheritedAttribute -RuleCfg $b3Local -IsSelfTest $true
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'BOUNDARY003_ModdedClassStripsVanillaAttributes' -and $_.Severity -eq 'error' })
    Assert-LintCase 'BOUNDARY003 bad (modded attributed vanilla class without InheritedAttribute)' ($hits.Count -eq 1) ("expected 1 finding, got " + $hits.Count)

    Reset-LintState -Root $selfRoot
    $raw = [System.IO.File]::ReadAllText((Join-Path $selfRoot 'boundary003\clean_modded.c'))
    $code = Get-LintCommentStripped -Lines ([regex]::Split($raw, '\r?\n')) -IsScript $true
    Invoke-LintRuleInheritedAttribute -File (Join-Path $selfRoot 'boundary003\clean_modded.c') -CodeLines $code -RuleCfg $b3Local
    Complete-LintRuleInheritedAttribute -RuleCfg $b3Local -IsSelfTest $true
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'BOUNDARY003_ModdedClassStripsVanillaAttributes' -and $_.Severity -eq 'error' })
    Assert-LintCase 'BOUNDARY003 clean (InheritedAttribute + unattributed vanilla class)' ($hits.Count -eq 0) ("expected 0 findings, got " + $hits.Count)

    $b3Missing = [pscustomobject]@{
        enabled                   = $true
        severity                  = 'error'
        vanillaManifest           = (Join-Path $selfRoot 'boundary003\does_not_exist.json')
        staleAfterDays            = 180
        allowStrippedDeclarations = @()
    }
    Reset-LintState -Root $selfRoot
    Complete-LintRuleInheritedAttribute -RuleCfg $b3Missing -IsSelfTest $true
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'BOUNDARY003_ModdedClassStripsVanillaAttributes' -and $_.Severity -eq 'error' })
    Assert-LintCase 'BOUNDARY003 missing manifest fails loudly' ($hits.Count -eq 1) ("expected 1 error, got " + $hits.Count)

    # --- 4. GUID001 positive & negative ---
    $guidSelfCfg = [pscustomobject]@{
        enabled            = $true
        severity           = 'error'
        localPathPattern   = '(?i)BGONE'
        excludeMetaTopDirs = @('.git')
        baseline           = @()
        allowReferences    = @()
    }
    $guidBadRoot = Join-Path $selfRoot 'guid001\bad'
    Reset-LintState -Root $guidBadRoot
    Initialize-LintMetaIndex -Root $guidBadRoot -RuleCfg $guidSelfCfg -IsSelfTest $true
    $raw = [System.IO.File]::ReadAllText((Join-Path $guidBadRoot 'refs.conf'))
    Invoke-LintRuleGuidResolution -File (Join-Path $guidBadRoot 'refs.conf') -CodeLines ([regex]::Split($raw, '\r?\n')) -RuleCfg $guidSelfCfg
    Complete-LintRuleGuidResolution -RuleCfg $guidSelfCfg -IsSelfTest $true
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'GUID001_LocalRefMustResolve' -and $_.Severity -eq 'error' })
    Assert-LintCase 'GUID001 bad (duplicate GUID, dangling local ref, path mismatch)' ($hits.Count -ge 3) ("expected >=3 findings, got " + $hits.Count)

    $guidCleanRoot = Join-Path $selfRoot 'guid001\clean'
    Reset-LintState -Root $guidCleanRoot
    Initialize-LintMetaIndex -Root $guidCleanRoot -RuleCfg $guidSelfCfg -IsSelfTest $true
    $raw = [System.IO.File]::ReadAllText((Join-Path $guidCleanRoot 'refs.conf'))
    Invoke-LintRuleGuidResolution -File (Join-Path $guidCleanRoot 'refs.conf') -CodeLines ([regex]::Split($raw, '\r?\n')) -RuleCfg $guidSelfCfg
    Complete-LintRuleGuidResolution -RuleCfg $guidSelfCfg -IsSelfTest $true
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'GUID001_LocalRefMustResolve' })
    Assert-LintCase 'GUID001 clean (resolved local ref + vanilla ref skipped)' ($hits.Count -eq 0) ("expected 0 findings, got " + $hits.Count)

    # --- 5. UI001 positive & negative ---
    $uiRule = Get-LintProp -Object $Rules -Name 'UI001_WidgetNameContract'
    $uiBadRoot = Join-Path $selfRoot 'ui001\bad'
    Reset-LintState -Root $uiBadRoot
    foreach ($f in @(Get-ChildItem -LiteralPath $uiBadRoot -File | Sort-Object FullName)) {
        Invoke-LintFile -File $f.FullName -Rules $Rules
    }
    Complete-LintRuleWidgetContract -RuleCfg $uiRule -IsSelfTest $true
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'UI001_WidgetNameContract' -and $_.Severity -eq 'error' })
    Assert-LintCase 'UI001 bad (missing widget, OverlaySlot, SizeX in LayoutSlot, Visible, Text Align)' ($hits.Count -ge 5) ("expected >=5 findings, got " + $hits.Count)

    $uiCleanRoot = Join-Path $selfRoot 'ui001\clean'
    Reset-LintState -Root $uiCleanRoot
    foreach ($f in @(Get-ChildItem -LiteralPath $uiCleanRoot -File | Sort-Object FullName)) {
        Invoke-LintFile -File $f.FullName -Rules $Rules
    }
    Complete-LintRuleWidgetContract -RuleCfg $uiRule -IsSelfTest $true
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'UI001_WidgetNameContract' })
    Assert-LintCase 'UI001 clean (matching widget + clean layout)' ($hits.Count -eq 0) ("expected 0 findings, got " + $hits.Count)

    # --- 6. DOCS001 / DOCS002 / DOCS003 positive & negative ---
    Reset-LintState -Root $selfRoot
    Invoke-LintRuleDocsEncoding -File (Join-Path $selfRoot 'docs\bad_mojibake.md') -Rules $Rules
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'DOCS001_MojibakeSequence' -and $_.Severity -eq 'error' })
    Assert-LintCase 'DOCS001 bad (mojibake sequence)' ($hits.Count -eq 1) ("expected 1 finding, got " + $hits.Count)

    Reset-LintState -Root $selfRoot
    Invoke-LintRuleDocsEncoding -File (Join-Path $selfRoot 'docs\bad_translit.md') -Rules $Rules
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'DOCS002_TransliterationDamage' -and $_.Severity -eq 'error' })
    Assert-LintCase 'DOCS002 bad (doubled question mark)' ($hits.Count -eq 1) ("expected 1 finding, got " + $hits.Count)

    Reset-LintState -Root $selfRoot
    Invoke-LintRuleDocsEncoding -File (Join-Path $selfRoot 'docs\bad_bom.md') -Rules $Rules
    $hits = @($script:Findings | Where-Object { $_.Rule -eq 'DOCS003_Utf8Bom' -and $_.Severity -eq 'error' })
    Assert-LintCase 'DOCS003 bad (UTF-8 BOM)' ($hits.Count -eq 1) ("expected 1 finding, got " + $hits.Count)

    Reset-LintState -Root $selfRoot
    Invoke-LintRuleDocsEncoding -File (Join-Path $selfRoot 'docs\clean_utf8.md') -Rules $Rules
    $hits = @($script:Findings | Where-Object { $_.Rule -like 'DOCS00*' })
    Assert-LintCase 'DOCS001-003 clean (legitimate UTF-8, single ?, no BOM)' ($hits.Count -eq 0) ("expected 0 findings, got " + $hits.Count)

    # --- 7. ENF001 / ENF002 / ENF003 / ENF004 positive & negative ---
    Reset-LintState -Root $selfRoot
    Invoke-LintFile -File (Join-Path $selfRoot 'enforce\bad_enforce.c') -Rules $Rules
    $h1 = @($script:Findings | Where-Object { $_.Rule -eq 'ENF001_StackedAttributes' -and $_.Severity -eq 'error' })
    $h2 = @($script:Findings | Where-Object { $_.Rule -eq 'ENF002_ArrayCompoundAssign' -and $_.Severity -eq 'error' })
    $h3 = @($script:Findings | Where-Object { $_.Rule -eq 'ENF003_NoTernary' -and $_.Severity -eq 'error' })
    Assert-LintCase 'ENF001 bad (stacked attributes)' ($h1.Count -eq 1) ("expected 1, got " + $h1.Count)
    Assert-LintCase 'ENF002 bad (array compound assign)' ($h2.Count -eq 1) ("expected 1, got " + $h2.Count)
    Assert-LintCase 'ENF003 bad (ternary operator)' ($h3.Count -eq 1) ("expected 1, got " + $h3.Count)

    Reset-LintState -Root $selfRoot
    Invoke-LintFile -File (Join-Path $selfRoot 'enforce\clean_enforce.c') -Rules $Rules
    $hClean = @($script:Findings | Where-Object { $_.Rule -like 'ENF00*' })
    Assert-LintCase 'ENF001-003 clean (comments/strings ignored, valid Enforce)' ($hClean.Count -eq 0) ("expected 0, got " + $hClean.Count)

    $enf4Rule = Get-LintProp -Object $Rules -Name 'ENF004_ScriptsLowercase'
    Reset-LintState -Root (Join-Path $selfRoot 'enf004\bad')
    Complete-LintRuleScriptsLowercase -Root (Join-Path $selfRoot 'enf004\bad') -RuleCfg $enf4Rule
    $h4Bad = @($script:Findings | Where-Object { $_.Rule -eq 'ENF004_ScriptsLowercase' -and $_.Severity -eq 'error' })
    Assert-LintCase 'ENF004 bad (uppercase Scripts directory)' ($h4Bad.Count -eq 1) ("expected 1, got " + $h4Bad.Count)

    Reset-LintState -Root (Join-Path $selfRoot 'enf004\clean')
    Complete-LintRuleScriptsLowercase -Root (Join-Path $selfRoot 'enf004\clean') -RuleCfg $enf4Rule
    $h4Clean = @($script:Findings | Where-Object { $_.Rule -eq 'ENF004_ScriptsLowercase' })
    Assert-LintCase 'ENF004 clean (lowercase scripts directory)' ($h4Clean.Count -eq 0) ("expected 0, got " + $h4Clean.Count)

    Write-Host ("SelfTest summary: " + $script:SelfPassed + " passed, " + $script:SelfFailed + " failed")
    if ($script:SelfFailed -gt 0) { exit 1 }
    exit 0
}

# ---- Main entry -------------------------------------------------------------------------------

if (-not (Test-Path -LiteralPath $Config)) {
    throw ("Lint config not found: " + $Config)
}
$script:LintConfigFile = (Resolve-Path -LiteralPath $Config).Path
$cfg = (Get-Content -LiteralPath $script:LintConfigFile -Raw | ConvertFrom-Json)
$scanCfg = Get-LintProp -Object $cfg -Name 'scan'
$rules   = Get-LintProp -Object $cfg -Name 'rules'
if ($null -eq $rules) { throw "lint-rules.json has no 'rules' section." }

if (-not (Test-Path -LiteralPath $Worktree)) {
    throw ("Worktree not found: " + $Worktree)
}
$Worktree = (Resolve-Path -LiteralPath $Worktree).Path

if ($SelfTest) {
    Invoke-LintSelfTest -Rules $rules
}

Reset-LintState -Root $Worktree

Write-LintStep "BGONE static lint"
Write-LintInfo ("worktree : " + $Worktree)
Write-LintInfo ("config   : " + $script:LintConfigFile)

$guidRule = Get-LintProp -Object $rules -Name 'GUID001_LocalRefMustResolve'
if (($null -ne $guidRule) -and (Get-LintProp -Object $guidRule -Name 'enabled' -Default $true)) {
    Initialize-LintMetaIndex -Root $Worktree -RuleCfg $guidRule -IsSelfTest $false
}

$targets = @(Get-LintTargets -Root $Worktree -ScanCfg $scanCfg)
Write-LintInfo ("files    : " + $targets.Count)

foreach ($f in $targets) {
    Invoke-LintFile -File $f -Rules $rules
}

if (($null -ne $guidRule) -and (Get-LintProp -Object $guidRule -Name 'enabled' -Default $true)) {
    Complete-LintRuleGuidResolution -RuleCfg $guidRule -IsSelfTest $false
}

$b3Rule = Get-LintProp -Object $rules -Name 'BOUNDARY003_ModdedClassStripsVanillaAttributes'
if (($null -ne $b3Rule) -and (Get-LintProp -Object $b3Rule -Name 'enabled' -Default $true)) {
    Complete-LintRuleInheritedAttribute -RuleCfg $b3Rule -IsSelfTest $false
}

$uiRule = Get-LintProp -Object $rules -Name 'UI001_WidgetNameContract'
if (($null -ne $uiRule) -and (Get-LintProp -Object $uiRule -Name 'enabled' -Default $true)) {
    Complete-LintRuleWidgetContract -RuleCfg $uiRule -IsSelfTest $false
}

$diagRule = Get-LintProp -Object $rules -Name 'DIAG001_ShippedCodeReferencesDiag'
if (($null -ne $diagRule) -and (Get-LintProp -Object $diagRule -Name 'enabled' -Default $true)) {
    Complete-LintRuleDiagIsolation -ScanRoot $Worktree -RuleCfg $diagRule -IsSelfTest $false
}

$enf4Rule = Get-LintProp -Object $rules -Name 'ENF004_ScriptsLowercase'
if (($null -ne $enf4Rule) -and (Get-LintProp -Object $enf4Rule -Name 'enabled' -Default $true)) {
    Complete-LintRuleScriptsLowercase -Root $Worktree -RuleCfg $enf4Rule
}

$errors = 0
$warns  = 0
foreach ($finding in $script:Findings) {
    $rel = Get-LintRelPath -File $finding.File -BaseRoot $Worktree
    $tag = $finding.Severity.ToUpper()
    Write-Host ("[" + $tag + "] " + $finding.Rule + " " + $rel + ":" + $finding.Line + " - " + $finding.Message)
    if (-not [string]::IsNullOrWhiteSpace($finding.Detail)) {
        Write-Host $finding.Detail
    }
    if ($finding.Severity -eq 'error') { $errors++ }
    elseif ($finding.Severity -eq 'warn') { $warns++ }
}

Write-Host ("Lint summary: " + $errors + " error(s), " + $warns + " warning(s), " + $targets.Count + " file(s) scanned")
if ($errors -gt 0) { exit 1 }
exit 0
