# QaCommon.ps1 - shared helpers for BGONE QA runners (PowerShell 5.1 ONLY).
# No &&/||, no ternary, no null-coalescing, no -AsHashtable.
# Every file write passes an explicit -Encoding.

function Write-QaStep {
    param([string]$Message)
    Write-Host ""
    Write-Host ("==> " + $Message) -ForegroundColor Cyan
}

function Write-QaInfo {
    param([string]$Message)
    Write-Host ("    " + $Message)
}

function Write-QaWarn {
    param([string]$Message)
    Write-Host ("    WARN: " + $Message) -ForegroundColor Yellow
}

function Write-QaError {
    param([string]$Message)
    Write-Host ("    ERROR: " + $Message) -ForegroundColor Red
}

function Invoke-QaCleanupStage {
    param([string]$Name, [scriptblock]$Action, $Errors)
    try {
        & $Action
    } catch {
        if ($null -ne $Errors) { [void]$Errors.Add($Name) }
        Write-QaWarn ('Cleanup stage "' + $Name + '" failed: ' + $_.Exception.Message + ' - carrying on with the rest of the teardown.')
    }
}

function Get-QaOrDefault {
    param($Value, $Fallback)
    if ($null -eq $Value) { return $Fallback }
    if ($Value -is [string]) {
        if ([string]::IsNullOrWhiteSpace($Value)) { return $Fallback }
        return $Value
    }
    if ($Value -is [int]) {
        if ($Value -le 0) { return $Fallback }
        return $Value
    }
    return $Value
}

function Get-QaProp {
    param($Object, [string]$Name, $Default = $null)
    if ($null -eq $Object) { return $Default }
    $prop = $Object.PSObject.Properties[$Name]
    if ($null -eq $prop) { return $Default }
    if ($null -eq $prop.Value) { return $Default }
    return $prop.Value
}

function Resolve-QaFullPath {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { return '' }
    try {
        $base = (Get-Location -PSProvider FileSystem).ProviderPath
        return [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($base, $Path)).TrimEnd([char]92, [char]47)
    } catch { return '' }
}

function Expand-QaPath {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { return $Path }
    return [System.Environment]::ExpandEnvironmentVariables($Path)
}

function New-QaDirectory {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
    return (Resolve-Path -LiteralPath $Path).ProviderPath
}

function Write-QaTextFile {
    param([string]$Path, [string[]]$Lines)
    $parent = Split-Path -Parent $Path
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    Set-Content -Path $Path -Value $Lines -Encoding utf8
}

function Read-QaLines {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return @() }
    $lines = Get-Content -LiteralPath $Path -ReadCount 0 -ErrorAction SilentlyContinue
    if ($null -eq $lines) { return @() }
    return @($lines)
}

function Add-QaGitExclude {
    param([string]$RepoPath, [string]$Rule = '.qa/')

    $gitDir = $null
    try {
        $out = & git -C $RepoPath rev-parse --git-common-dir
        if ($LASTEXITCODE -eq 0 -and $out) { $gitDir = ($out | Select-Object -First 1).Trim() }
    } catch {
        $gitDir = $null
    }
    if (-not $gitDir) { return $false }

    if (-not [System.IO.Path]::IsPathRooted($gitDir)) {
        $gitDir = Join-Path $RepoPath $gitDir
    }
    $excludePath = Join-Path $gitDir 'info\exclude'
    $infoDir = Split-Path -Parent $excludePath
    if (-not (Test-Path -LiteralPath $infoDir)) {
        New-Item -ItemType Directory -Path $infoDir -Force | Out-Null
    }

    if (Test-Path -LiteralPath $excludePath) {
        $existing = Read-QaLines -Path $excludePath
        foreach ($line in $existing) {
            if ($line.Trim() -eq $Rule) { return $false }
        }
    }
    Add-Content -Path $excludePath -Value $Rule -Encoding ascii
    return $true
}

function Get-QaGitStamp {
    param([string]$WorktreePath)
    $commit = ''
    $dirty = New-Object System.Collections.ArrayList
    try {
        $commit = [string](& git -C $WorktreePath rev-parse HEAD 2>$null)
        foreach ($line in @(& git -C $WorktreePath status --porcelain 2>$null)) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $path = $line.Substring(3).Trim('"')
            if ($path -like '*resourceDatabase.rdb' -or $path -like '.qa/*' -or $path -like '.qa\*') { continue }
            [void]$dirty.Add($path)
        }
    } catch { }
    $commit = $commit.Trim()
    $short = $commit
    if ($short.Length -gt 8) { $short = $short.Substring(0, 8) }
    $text = $short
    if (-not $commit) { $text = 'unknown (not a git checkout)' }
    elseif ($dirty.Count -gt 0) { $text = $short + ' plus ' + $dirty.Count + ' uncommitted file(s)' }
    return [pscustomobject]@{ Commit = $commit; Short = $short; Dirty = @($dirty.ToArray()); Text = $text }
}

function New-QaLogClock {
    param([string]$LogPath, [datetime]$Fallback)
    $start = $Fallback
    $folder = ''
    if ($LogPath) { $folder = Split-Path -Leaf (Split-Path -Parent $LogPath) }
    if ($folder -match '^logs_(\d{4}-\d{2}-\d{2})_(\d{2})-(\d{2})-(\d{2})$') {
        $start = [datetime]::ParseExact(($Matches[1] + ' ' + $Matches[2] + ':' + $Matches[3] + ':' + $Matches[4]), 'yyyy-MM-dd HH:mm:ss', [System.Globalization.CultureInfo]::InvariantCulture)
    }
    return [pscustomobject]@{ Date = $start.Date; Last = $start }
}

function Get-QaLogClockTime {
    param($Clock, [string]$TimeOfDay)
    $tod = [timespan]::Parse($TimeOfDay, [System.Globalization.CultureInfo]::InvariantCulture)
    $time = $Clock.Date.Add($tod)
    if ($time -lt $Clock.Last.AddHours(-1)) {
        $Clock.Date = $Clock.Date.AddDays(1)
        $time = $Clock.Date.Add($tod)
    }
    if ($time -gt $Clock.Last) { $Clock.Last = $time }
    return $time
}
