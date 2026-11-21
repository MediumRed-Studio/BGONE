<#
.SYNOPSIS
    Regenerates tools/lint/vanilla-class-attributes.json - the checked-in manifest of vanilla
    Arma Reforger script classes that carry a class attribute list, used by
    BOUNDARY003_ModdedClassStripsVanillaAttributes.

.DESCRIPTION
    In Enforce Script, `modded class X` silently drops X's vanilla class attribute list.
    Where vanilla declared [BaseContainerProps()], dropping it makes the class invisible to
    configs ("unknown class" at load, no compile error). CI runners and contributors without
    the vanilla script source rely on this deterministic checked-in manifest.

    Targets Windows PowerShell 5.1 (pure ASCII, no pwsh-only syntax). Writes UTF-8 without BOM.

.PARAMETER VanillaSource
    Root of the vanilla script source (must contain a `scripts` directory).
    Defaults to Z:\Reforger Source 1.9.0.37.

.PARAMETER OutFile
    Output manifest path. Defaults to vanilla-class-attributes.json beside this script.

.EXAMPLE
    .\tools\lint\vanilla-attributes-refresh.ps1
.EXAMPLE
    .\tools\lint\vanilla-attributes-refresh.ps1 -VanillaSource "Z:\Reforger Source 1.9.0.37"
#>
[CmdletBinding()]
param(
    [string]$VanillaSource = 'Z:\Reforger Source 1.9.0.37',
    [string]$OutFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($OutFile)) {
    $OutFile = Join-Path $scriptRoot 'vanilla-class-attributes.json'
}

if (-not (Test-Path -LiteralPath $VanillaSource)) {
    throw ("Vanilla script source not found: " + $VanillaSource)
}
$scriptsDir = Join-Path $VanillaSource 'scripts'
if (-not (Test-Path -LiteralPath $scriptsDir)) {
    throw ("No 'scripts' directory under " + $VanillaSource)
}

# Match non-modded class declarations only (vanilla never mods).
$classRegex = [regex]'^\s*(?:sealed\s+|generic\s+)*class\s+([A-Za-z_][A-Za-z0-9_]*)'

$commit = ''
$commitSubject = ''
$commitDate = ''
$sourceVersion = ''

$leaf = Split-Path -Leaf $VanillaSource
$vm = [regex]::Match($leaf, '(\d+\.\d+\.\d+(?:\.\d+)?)')
if ($vm.Success) { $sourceVersion = $vm.Groups[1].Value }

if (Test-Path -LiteralPath (Join-Path $VanillaSource '.git')) {
    try {
        $commit        = (& git -C $VanillaSource rev-parse HEAD 2>$null | Select-Object -First 1)
        $commitSubject = (& git -C $VanillaSource log -1 --format=%s 2>$null | Select-Object -First 1)
        $commitDate    = (& git -C $VanillaSource log -1 --format=%cI 2>$null | Select-Object -First 1)
    }
    catch { }
    if ($null -eq $commit) { $commit = '' }
    if ($null -eq $commitSubject) { $commitSubject = '' }
    if ($null -eq $commitDate) { $commitDate = '' }
}
if ([string]::IsNullOrWhiteSpace($commitSubject) -and (-not [string]::IsNullOrWhiteSpace($sourceVersion))) {
    $commitSubject = 'Reforger ' + $sourceVersion
}

$files = @(Get-ChildItem -LiteralPath $scriptsDir -Recurse -File -Filter '*.c' -ErrorAction SilentlyContinue | Sort-Object FullName)
Write-Host ("Scanning " + $files.Count + " vanilla .c files under " + $scriptsDir)

$attributed = @{}
$classCount = 0

foreach ($f in $files) {
    $lines = @(Get-Content -LiteralPath $f.FullName -ErrorAction SilentlyContinue)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $m = $classRegex.Match($lines[$i])
        if (-not $m.Success) { continue }
        $classCount++

        # Walk back over blank lines and comments to the nearest preceding code line.
        $j = $i - 1
        while ($j -ge 0) {
            $t = $lines[$j].Trim()
            if (($t -eq '') -or $t.StartsWith('//') -or $t.StartsWith('*') -or $t.StartsWith('/*')) {
                $j--
                continue
            }
            break
        }
        if ($j -lt 0) { continue }
        if (-not $lines[$j].TrimEnd().EndsWith(']')) { continue }

        # Walk back to the line that opens the attribute list (may span multiple lines).
        $k = $j
        while (($k -ge 0) -and (-not $lines[$k].TrimStart().StartsWith('['))) { $k-- }
        if ($k -lt 0) { continue }

        $parts = @()
        for ($n = $k; $n -le $j; $n++) { $parts += $lines[$n].Trim() }
        $attrs = ($parts -join ' ')

        if (-not $attributed.ContainsKey($m.Groups[1].Value)) {
            $attributed[$m.Groups[1].Value] = $attrs
        }
    }
}

$sorted = New-Object psobject
foreach ($name in ($attributed.Keys | Sort-Object -CaseSensitive)) {
    Add-Member -InputObject $sorted -MemberType NoteProperty -Name $name -Value $attributed[$name]
}

$manifest = [pscustomobject]@{
    _readme     = 'GENERATED - do not hand-edit. Vanilla Arma Reforger script classes carrying class attributes, for BOUNDARY003_ModdedClassStripsVanillaAttributes. Refresh with tools/lint/vanilla-attributes-refresh.ps1 after a game version update.'
    _provenance = [pscustomobject]@{
        generator           = 'tools/lint/vanilla-attributes-refresh.ps1'
        generatedUtc        = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        sourceVersion       = $sourceVersion
        sourcePath          = $VanillaSource
        sourceCommit        = $commit
        sourceCommitSubject = $commitSubject
        sourceCommitDate    = $commitDate
        filesScanned        = $files.Count
        classesScanned      = $classCount
        attributedCount     = $attributed.Count
    }
    classes = $sorted
}

$json = ($manifest | ConvertTo-Json -Depth 6)
[System.IO.File]::WriteAllText($OutFile, $json, (New-Object System.Text.UTF8Encoding($false)))

Write-Host ("Wrote " + $OutFile)
Write-Host ("  classes scanned : " + $classCount)
Write-Host ("  attributed      : " + $attributed.Count)
Write-Host ("  source version  : " + $sourceVersion)
