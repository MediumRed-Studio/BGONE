# QaConfig.ps1 - engines.json loading, single-addon gproj + staging junction resolution,
# exe/gameDir resolution and command-line construction (PowerShell 5.1 ONLY).

function Get-QaConfig {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "QA config not found: $Path"
    }
    $raw = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop
    return ($raw | ConvertFrom-Json)
}

function Get-QaTargetEngine {
    param($Defaults)
    $target = [string](Get-QaProp -Object $Defaults -Name 'targetEngine' -Default '')
    if ([string]::IsNullOrWhiteSpace($target)) { return 'stable' }
    return $target
}

function Get-QaEngineConfig {
    param($Config, [string]$Engine)
    $engines = Get-QaProp -Object $Config -Name 'engines'
    if ($null -eq $engines) { throw "engines.json has no 'engines' section." }
    $eng = Get-QaProp -Object $engines -Name $Engine
    if ($null -eq $eng) {
        $names = @($engines.PSObject.Properties | Where-Object { $_.Name -notlike '_*' } | ForEach-Object { $_.Name })
        throw ("Unknown -Engine '" + $Engine + "'. Configured engines: " + ($names -join ', '))
    }
    return $eng
}

function Merge-QaConfigObject {
    param($Base, $Over)
    $merged = [ordered]@{}
    foreach ($p in $Base.PSObject.Properties) { $merged[$p.Name] = $p.Value }
    foreach ($p in $Over.PSObject.Properties) {
        $b = $merged[$p.Name]
        if (($b -is [System.Management.Automation.PSCustomObject]) -and ($p.Value -is [System.Management.Automation.PSCustomObject])) {
            $merged[$p.Name] = Merge-QaConfigObject -Base $b -Over $p.Value
        } else {
            $merged[$p.Name] = $p.Value
        }
    }
    return [pscustomobject]$merged
}

function Get-QaRigConfig {
    param($Config, [string]$Engine, [string]$Section)
    $own = Get-QaProp -Object (Get-QaEngineConfig -Config $Config -Engine $Engine) -Name $Section
    if ($null -eq $own) { return $null }
    $baseName = [string](Get-QaProp -Object $own -Name 'inherits' -Default '')
    if ([string]::IsNullOrWhiteSpace($baseName)) { return $own }
    $base = Get-QaProp -Object (Get-QaEngineConfig -Config $Config -Engine $baseName) -Name $Section
    if ($null -eq $base) {
        throw ("engines." + $Engine + "." + $Section + " inherits from '" + $baseName + "', which has no '" + $Section + "' section.")
    }
    if ($null -ne (Get-QaProp -Object $base -Name 'inherits')) {
        throw ("engines." + $Engine + "." + $Section + " inherits from engines." + $baseName + "." + $Section + ", which itself inherits. Point it at the section that carries the values.")
    }
    $merged = Merge-QaConfigObject -Base $base -Over $own
    $merged.PSObject.Properties.Remove('inherits')
    return $merged
}

$script:QaReservedEngineSections = @('autotest', 'plugin', 'mp', 'persist')

function Get-QaGateConfig {
    param($EngineConfig, [string]$Gate, [string]$EngineName)
    if ([string]::IsNullOrWhiteSpace($Gate) -or ($Gate -eq 'workbench')) { return $EngineConfig }
    if ($script:QaReservedEngineSections -contains $Gate) {
        throw ("'" + $Gate + "' is not a compile gate.")
    }
    $sub = Get-QaProp -Object $EngineConfig -Name $Gate
    if ($null -eq $sub) {
        $available = @('workbench')
        foreach ($p in $EngineConfig.PSObject.Properties) {
            if ($p.Name -like '_*') { continue }
            if ($script:QaReservedEngineSections -contains $p.Name) { continue }
            if ($p.Value -is [System.Management.Automation.PSCustomObject]) { $available += $p.Name }
        }
        throw ("Engine '" + $EngineName + "' has no '" + $Gate + "' compile gate configured. Available gates for this engine: " + ($available -join ', '))
    }
    return $sub
}

# Reads addon IDs from a single-addon root (<WorktreePath>\addon.gproj) or a multi-addon folder (<AddonsDir>\*\addon.gproj).
function Get-QaAddonIds {
    param([string]$AddonsDir)
    $ids = New-Object System.Collections.ArrayList
    if ([string]::IsNullOrWhiteSpace($AddonsDir)) { return @() }
    if (-not (Test-Path -LiteralPath $AddonsDir)) { return @() }

    $rootGproj = Join-Path $AddonsDir 'addon.gproj'
    if (Test-Path -LiteralPath $rootGproj) {
        try {
            $raw = Get-Content -LiteralPath $rootGproj -Raw -ErrorAction Stop
            if ($raw -match 'ID\s+"(?<id>[^"]+)"') { [void]$ids.Add($Matches['id']) }
        } catch { }
        return @($ids.ToArray())
    }

    $dirs = @(Get-ChildItem -LiteralPath $AddonsDir -Directory -ErrorAction SilentlyContinue)
    foreach ($d in $dirs) {
        $gproj = Join-Path $d.FullName 'addon.gproj'
        if (-not (Test-Path -LiteralPath $gproj)) { continue }
        $raw = ''
        try { $raw = Get-Content -LiteralPath $gproj -Raw -ErrorAction Stop } catch { continue }
        if ($raw -match 'ID\s+"(?<id>[^"]+)"') {
            [void]$ids.Add($Matches['id'])
        }
    }
    return @($ids.ToArray())
}

function Get-QaAddonGuid {
    param([string]$GprojPath)
    if ([string]::IsNullOrWhiteSpace($GprojPath) -or -not (Test-Path -LiteralPath $GprojPath)) { return '' }
    try {
        $raw = Get-Content -LiteralPath $GprojPath -Raw -ErrorAction Stop
        if ($raw -match 'GUID\s+"(?<guid>[0-9A-Fa-f]+)"') { return $Matches['guid'].ToUpperInvariant() }
    } catch { }
    return ''
}

# Deterministic external staging junction layout for a single-addon BGONE worktree:
#   C:\wt\.bgone-stage\<sha1-8-of-wt>\Addons\BGONE -> <WorktreePath>
# Lives OUTSIDE the worktree so rig_queue.py never sees a symlink inside the checkout,
# sibling worktrees in C:\wt are never scanned by -addonsDir, and Get-QaProcessWorktree
# matches \Addons cleanly. Pure computation (creates nothing on disk).
function Get-QaStageInfo {
    param(
        [string]$WorktreePath,
        [string]$AddonName = 'BGONE',
        [string]$StageBase = 'C:\wt\.bgone-stage'
    )
    $norm = (Resolve-QaFullPath -Path $WorktreePath).TrimEnd('\')
    if ([string]::IsNullOrWhiteSpace($norm)) { $norm = $WorktreePath.Replace('/', '\').TrimEnd('\') }
    $sha = [System.Security.Cryptography.SHA1]::Create()
    $hex = ''
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($norm.ToLowerInvariant())
        $hash = $sha.ComputeHash($bytes)
        $hex = ([System.BitConverter]::ToString($hash, 0, 4)).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
    $stageRoot = Join-Path $StageBase $hex
    $addonsDir = Join-Path $stageRoot 'Addons'
    $addonLink = Join-Path $addonsDir $AddonName
    $gprojPath = Join-Path $addonLink 'addon.gproj'
    return [pscustomobject]@{
        Hash      = $hex
        StageRoot = $stageRoot
        AddonsDir = $addonsDir
        AddonLink = $addonLink
        GprojPath = $gprojPath
        Target    = $norm
    }
}

# Idempotently ensures C:\wt\.bgone-stage\<sha1-8>\Addons\BGONE is an NTFS junction pointing
# to <WorktreePath>. Verifies target match before reusing; recreates if pointing elsewhere.
function Ensure-QaStageAddonsDir {
    param(
        [string]$WorktreePath,
        [string]$AddonName = 'BGONE',
        [string]$StageBase = 'C:\wt\.bgone-stage'
    )
    $info = Get-QaStageInfo -WorktreePath $WorktreePath -AddonName $AddonName -StageBase $StageBase
    if (-not (Test-Path -LiteralPath $info.AddonsDir)) {
        New-Item -ItemType Directory -Path $info.AddonsDir -Force | Out-Null
    }

    $needCreate = $true
    if (Test-Path -LiteralPath $info.AddonLink) {
        $item = Get-Item -LiteralPath $info.AddonLink -Force -ErrorAction Stop
        $isReparse = (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)
        $curTarget = ''
        if ($isReparse -and $item.PSObject.Properties['Target'] -and $item.Target) {
            $tVal = @($item.Target)[0]
            $curTarget = (Resolve-QaFullPath -Path ([string]$tVal)).TrimEnd('\')
        }
        if ($isReparse -and [string]::Equals($curTarget, $info.Target, [System.StringComparison]::OrdinalIgnoreCase)) {
            $needCreate = $false
        } else {
            if ($isReparse) {
                & cmd.exe /c rmdir $info.AddonLink 2>&1 | Out-Null
            } else {
                throw ("Refusing to overwrite non-junction directory at staging path: " + $info.AddonLink)
            }
            if (Test-Path -LiteralPath $info.AddonLink) {
                throw ("Could not remove stale staging junction: " + $info.AddonLink)
            }
        }
    }

    if ($needCreate) {
        $out = & cmd.exe /c mklink /J $info.AddonLink $info.Target 2>&1
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $info.GprojPath)) {
            throw ("Failed to create staging junction '" + $info.AddonLink + "' -> '" + $info.Target + "': " + ($out -join ' '))
        }
    }
    return $info
}

function Resolve-QaWorkbenchExe {
    param($EngineConfig, [string]$Override, [string]$EngineName)

    if (-not [string]::IsNullOrWhiteSpace($Override)) {
        $p = Expand-QaPath -Path $Override
        if (-not (Test-Path -LiteralPath $p)) {
            throw ("-WbExe path does not exist: " + $p)
        }
        return (Resolve-Path -LiteralPath $p).ProviderPath
    }

    $candidates = @(Get-QaProp -Object $EngineConfig -Name 'exeCandidates' -Default @())
    foreach ($c in $candidates) {
        $p = Expand-QaPath -Path $c
        if (Test-Path -LiteralPath $p) {
            return (Resolve-Path -LiteralPath $p).ProviderPath
        }
    }
    throw ("Could not locate an executable for engine '" + $EngineName + "'. Tried:" + [Environment]::NewLine +
           ("  " + ($candidates -join ([Environment]::NewLine + "  "))) + [Environment]::NewLine +
           "Pass an explicit path override, or fix engines.json -> engines." + $EngineName + ".exeCandidates.")
}

function Resolve-QaGameDir {
    param($EngineConfig, [string]$Override, [string]$EngineName)

    $relCheck = Get-QaProp -Object $EngineConfig -Name 'gameProjectRelativePath' -Default 'addons\data\ArmaReforger.gproj'

    if (-not [string]::IsNullOrWhiteSpace($Override)) {
        $p = Expand-QaPath -Path $Override
        if (-not (Test-Path -LiteralPath $p)) { throw ("-GameDir path does not exist: " + $p) }
        return (Resolve-Path -LiteralPath $p).ProviderPath
    }

    $candidates = @(Get-QaProp -Object $EngineConfig -Name 'gameDirCandidates' -Default @())
    foreach ($c in $candidates) {
        $p = Expand-QaPath -Path $c
        if ([string]::IsNullOrWhiteSpace($relCheck)) {
            if (Test-Path -LiteralPath $p) { return (Resolve-Path -LiteralPath $p).ProviderPath }
        } elseif (Test-Path -LiteralPath (Join-Path $p $relCheck)) {
            return (Resolve-Path -LiteralPath $p).ProviderPath
        }
    }
    throw ("Could not locate the Arma Reforger install dir for engine '" + $EngineName + "' (needed as the launch working directory). Tried:" + [Environment]::NewLine +
           ("  " + ($candidates -join ([Environment]::NewLine + "  "))) + [Environment]::NewLine +
           "Pass -GameDir <install-root>, or fix engines.json -> engines." + $EngineName + ".gameDirCandidates.")
}

function Expand-QaTokens {
    param([string[]]$Template, [hashtable]$Tokens)
    $out = New-Object System.Collections.ArrayList
    foreach ($item in @($Template)) {
        if ($null -eq $item) { continue }
        $s = [string]$item
        foreach ($k in $Tokens.Keys) {
            $s = $s.Replace($k, [string]$Tokens[$k])
        }
        [void]$out.Add($s)
    }
    return @($out.ToArray())
}

function Build-QaArgumentList {
    param(
        $EngineConfig,
        [hashtable]$Tokens,
        [bool]$Autotest = $false,
        [string[]]$AdditionalArgs = @()
    )
    $template = @(Get-QaProp -Object $EngineConfig -Name 'argTemplate' -Default @())
    $argList = New-Object System.Collections.ArrayList
    foreach ($a in (Expand-QaTokens -Template $template -Tokens $Tokens)) { [void]$argList.Add($a) }

    foreach ($a in (Expand-QaTokens -Template @(Get-QaProp -Object $EngineConfig -Name 'extraArgs' -Default @()) -Tokens $Tokens)) {
        [void]$argList.Add($a)
    }

    if ($Autotest) {
        foreach ($a in (Expand-QaTokens -Template @(Get-QaProp -Object $EngineConfig -Name 'autotestArgs' -Default @()) -Tokens $Tokens)) {
            [void]$argList.Add($a)
        }
    }

    foreach ($a in @($AdditionalArgs)) {
        if (-not [string]::IsNullOrWhiteSpace($a)) { [void]$argList.Add($a) }
    }

    return @($argList.ToArray())
}

function ConvertTo-QaQuotedArgs {
    param([string[]]$Arguments)
    $out = New-Object System.Collections.ArrayList
    foreach ($a in @($Arguments)) {
        if ($null -eq $a) { continue }
        $s = [string]$a
        if ($s -match '\s' -and -not ($s.StartsWith('"') -and $s.EndsWith('"'))) {
            [void]$out.Add('"' + $s + '"')
        } else {
            [void]$out.Add($s)
        }
    }
    return @($out.ToArray())
}

function Format-QaCommandLine {
    param([string]$Exe, [string[]]$Arguments, [string]$WorkingDirectory)
    $parts = New-Object System.Collections.ArrayList
    [void]$parts.Add(('& "' + $Exe + '"'))
    foreach ($a in (ConvertTo-QaQuotedArgs -Arguments $Arguments)) { [void]$parts.Add($a) }
    $cmd = ($parts.ToArray() -join ' ')
    if ($WorkingDirectory) {
        return ('# working directory: ' + $WorkingDirectory + [Environment]::NewLine + $cmd)
    }
    return $cmd
}

function Get-QaExeGameVersion {
    param([string]$Exe)
    if ([string]::IsNullOrWhiteSpace($Exe) -or -not (Test-Path -LiteralPath $Exe)) { return $null }
    try {
        $fv = ([string](Get-Item -LiteralPath $Exe).VersionInfo.FileVersion).Trim()
        if ($fv) { return $fv }
    } catch { }
    return $null
}

function Get-QaEngineVersionProblem {
    param([string]$Engine, [string]$ValidatedVersion, [string[]]$Exes, [hashtable]$Versions = @{})
    if ([string]::IsNullOrWhiteSpace($ValidatedVersion)) {
        return ('engines.' + $Engine + ' has no validatedVersion in tools/qa/engines.json. Set it to the installed exe FileVersion so future updates are flagged.')
    }
    $bad = New-Object System.Collections.ArrayList
    foreach ($e in @($Exes | Where-Object { $_ })) {
        $v = $null
        if ($Versions.ContainsKey($e)) { $v = [string]$Versions[$e] } else { $v = Get-QaExeGameVersion -Exe $e }
        if ($v -and $v -ne $ValidatedVersion) { [void]$bad.Add(([System.IO.Path]::GetFileName($e) + ' is ' + $v)) }
    }
    if ($bad.Count -eq 0) { return $null }
    return ('The ' + $Engine + ' engine version differs from validatedVersion (' + $ValidatedVersion + '): ' + ($bad.ToArray() -join '; ') +
        '. Re-check the retail server/client exe pair and update engines.' + $Engine + '.validatedVersion in tools/qa/engines.json. To run anyway once, pass -AcceptEngineVersion.')
}

function Get-QaProtocolChecksumHint {
    param([string]$Line)
    if ([string]::IsNullOrWhiteSpace($Line) -or $Line -notmatch 'script protocol checksum does not match') { return $null }
    return ('Refused on join: ' + $Line.Trim() + ' Client and server exes are a mismatched pair (Diag vs retail, or different builds). Use the matching retail pair on both sides.')
}
