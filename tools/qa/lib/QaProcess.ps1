# QaProcess.ps1 - Workbench process lifecycle for qa-run.ps1 (issue #697).
#
# Two hard-won constraints drive this file:
#  * Steam-launched executables bootstrap: the PID Start-Process hands back can exit within seconds
#    while Steam respawns the real process. So the launched PID is only a seed - the authoritative
#    handle is re-resolved by matching the unique per-run profile path in Win32_Process.CommandLine.
#  * Workbench is not expected to self-exit. Termination is CloseMainWindow (so the buffered log
#    tail is flushed) -> grace period -> taskkill /T /F (PS 5.1's Stop-Process has no tree kill).
#    All parsing happens post-mortem.

# Windows command-line splitting (CommandLineToArgvW rules): whitespace separates unless quoted; a quote toggles quoting;
# 2n backslashes before a quote are n backslashes and the quote toggles, 2n+1 are n backslashes and a literal quote.
function ConvertFrom-QaCommandLine {
    param([string]$CommandLine)
    if ([string]::IsNullOrEmpty($CommandLine)) { return @() }
    $out = New-Object System.Collections.ArrayList
    $sb = New-Object System.Text.StringBuilder
    $quoted = $false
    $started = $false
    $i = 0
    $n = $CommandLine.Length
    while ($i -lt $n) {
        $c = $CommandLine[$i]
        if ($c -eq [char]92) {
            $j = $i
            while (($j -lt $n) -and ($CommandLine[$j] -eq [char]92)) { $j++ }
            $count = $j - $i
            $started = $true
            if (($j -lt $n) -and ($CommandLine[$j] -eq [char]34)) {
                [void]$sb.Append([char]92, [int][math]::Floor($count / 2))
                if (($count % 2) -eq 1) { [void]$sb.Append([char]34); $i = $j + 1 } else { $i = $j }
            } else {
                [void]$sb.Append([char]92, $count)
                $i = $j
            }
            continue
        }
        if ($c -eq [char]34) { $quoted = -not $quoted; $started = $true; $i++; continue }
        if ([char]::IsWhiteSpace($c) -and (-not $quoted)) {
            if ($started) { [void]$out.Add($sb.ToString()); [void]$sb.Clear(); $started = $false }
            $i++
            continue
        }
        [void]$sb.Append($c)
        $started = $true
        $i++
    }
    if ($started) { [void]$out.Add($sb.ToString()) }
    return @($out.ToArray())
}

# A fully qualified path (drive letter + separator, or UNC \\server\share) as one canonical string (full path, no trailing
# separator), or '' if it is not one: relative, drive-relative (C:foo) and root-relative (\foo) paths all give ''.
function ConvertTo-QaFullPath {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { return '' }
    try {
        if (-not [System.IO.Path]::IsPathRooted($Path)) { return '' }
        # Fully qualified only: IsPathRooted also accepts '\foo' (current drive) and 'C:foo' (current directory on C:), which
        # GetFullPath would resolve against the OBSERVER's cwd - not that of the process that was started with them.
        if ($Path -notmatch '^(?:[A-Za-z]:[\\/]|[\\/]{2}[^\\/]+[\\/][^\\/])') { return '' }
        return [System.IO.Path]::GetFullPath($Path).TrimEnd([char]92, [char]47)
    } catch { return '' }
}

# Profile tokens as they are recorded: blank dropped, each one absolute (Resolve-QaFullPath).
function ConvertTo-QaTokenList {
    param([string[]]$Tokens)
    $out = New-Object System.Collections.ArrayList
    foreach ($t in @($Tokens)) {
        if ([string]::IsNullOrWhiteSpace($t)) { continue }
        $full = Resolve-QaFullPath -Path $t
        if ($full -eq '') { $full = $t }
        [void]$out.Add($full)
    }
    return @($out.ToArray())
}

# The raw -profile values of an engine command line (as written: possibly relative). The one place profile arguments are read.
function Get-QaCommandLineProfileValues {
    param([string]$CommandLine)
    $a = @(ConvertFrom-QaCommandLine -CommandLine $CommandLine)
    $found = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $a.Count; $i++) {
        $v = ''
        if ($a[$i] -match '^[-/]profile$') { if ($i + 1 -lt $a.Count) { $v = $a[$i + 1] } }
        elseif ($a[$i] -match '^[-/]profile[=:](.+)$') { $v = $Matches[1] }
        if ($v -ne '') { [void]$found.Add($v) }
    }
    return @($found.ToArray())
}

# Every -profile value of an engine command line that is an absolute path, as canonical full paths. A run's engines are
# recognised by what they were STARTED WITH - the parsed -profile argument - never by text that merely appears in the line.
function Get-QaCommandLineProfiles {
    param([string]$CommandLine)
    return @(Get-QaCommandLineProfileValues -CommandLine $CommandLine | ForEach-Object { ConvertTo-QaFullPath -Path $_ } | Where-Object { $_ -ne '' })
}

# Processes in a snapshot whose ownership CANNOT be decided: they carry a -profile that is not an absolute path, and the
# directory it is relative to is the process's own (not available to us). Every run this tooling starts passes an absolute
# profile, so such a process is not provably anyone's - and not provably absent either. Absence checks treat it as Unknown (the
# refusal names it); nothing is ever killed on this basis.
function Get-QaSnapshotUndecided {
    param($Snapshot)
    $out = New-Object System.Collections.ArrayList
    if ($null -eq $Snapshot) { return @() }
    foreach ($p in @($Snapshot.Processes)) {
        if ($null -eq $p -or -not $p.CommandLine) { continue }
        foreach ($v in @(Get-QaCommandLineProfileValues -CommandLine $p.CommandLine)) {
            if ((ConvertTo-QaFullPath -Path $v) -eq '') { [void]$out.Add($p); break }
        }
    }
    return @($out.ToArray())
}

# Does this command line start an engine whose profile IS the token (a run's profile directory) or lies UNDER it (the token
# is a run directory)? Compared as canonical paths, case-insensitively, on directory boundaries: `...\stable` never owns
# `...\stable-mp-server`, `...\stable other`, `...\stable+other` or `...\stable\..\other`.
function Test-QaProfileOwned {
    param([string]$CommandLine, [string]$Token, [string[]]$Profiles = $null)
    $root = ConvertTo-QaFullPath -Path $Token
    if ($root -eq '') { return $false }
    if ($null -eq $Profiles) { $Profiles = @(Get-QaCommandLineProfiles -CommandLine $CommandLine) }
    foreach ($p in @($Profiles)) {
        if ($p.Equals($root, [System.StringComparison]::OrdinalIgnoreCase) -or $p.StartsWith($root + [char]92, [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}

function Get-QaWorkbenchProcesses {
    param(
        [string]$NamePrefix = 'ArmaReforgerWorkbench',
        # A plain SUBSTRING of the command line (a fragment such as '\.qa\'); NOT an ownership test.
        [string]$CommandLineContains,
        # Ownership: only engines started with a -profile that is, or lies under, this directory (Test-QaProfileOwned).
        [string]$OwnedByProfile
    )
    $all = @()
    try {
        $all = @(Get-CimInstance -ClassName Win32_Process -ErrorAction Stop)
    } catch {
        return @()
    }
    $match = @($all | Where-Object { $_.Name -like ($NamePrefix + '*') })
    if (-not [string]::IsNullOrWhiteSpace($CommandLineContains)) {
        $needle = $CommandLineContains
        $match = @($match | Where-Object {
            $_.CommandLine -and ($_.CommandLine.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0)
        })
    }
    if (-not [string]::IsNullOrWhiteSpace($OwnedByProfile)) {
        $owner = $OwnedByProfile
        $match = @($match | Where-Object { Test-QaProfileOwned -CommandLine $_.CommandLine -Token $owner })
    }
    return @($match)
}


# ONE filtered, fault-aware look at the engine processes (#1388). Get-QaWorkbenchProcesses enumerates EVERY process and
# turns a failed query into an empty list, which is fine for "find something to wait for" but not for "prove nothing of
# mine is still running": here the name filter is applied by the query, and a query that fails says Ok = $false instead of
# looking like an empty machine. Callers that decide anything irreversible (release a lock, reclaim one) take ONE snapshot
# and match every token against it in memory (Select-QaSnapshotByToken).
function Get-QaEngineSnapshot {
    param([string]$NamePrefix = 'ArmaReforger')
    try {
        $rows = @(Get-CimInstance -ClassName Win32_Process -Filter ("Name LIKE '" + $NamePrefix + "%'") -ErrorAction Stop)
    } catch {
        return [pscustomobject]@{ Ok = $false; Processes = @(); Error = $_.Exception.Message }
    }
    return [pscustomobject]@{ Ok = $true; Processes = $rows; Error = '' }
}

# The snapshot's processes owned by any of Tokens (a run's unique profile paths): started with a -profile that is, or lies
# under, one of them. Each process's command line is parsed once.
function Select-QaSnapshotByToken {
    param($Snapshot, [string[]]$Tokens)
    $found = New-Object System.Collections.ArrayList
    if ($null -eq $Snapshot) { return @() }
    $roots = @($Tokens | ForEach-Object { ConvertTo-QaFullPath -Path $_ } | Where-Object { $_ -ne '' })
    if ($roots.Count -eq 0) { return @() }
    foreach ($p in @($Snapshot.Processes)) {
        if ($null -eq $p -or -not $p.CommandLine) { continue }
        $profiles = @(Get-QaCommandLineProfiles -CommandLine $p.CommandLine)
        if ($profiles.Count -eq 0) { continue }
        foreach ($root in $roots) {
            if (Test-QaProfileOwned -CommandLine $p.CommandLine -Token $root -Profiles $profiles) { [void]$found.Add($p); break }
        }
    }
    return @($found.ToArray())
}

# ------------------------------------------------------------------------------------------------
# SCREEN CLAIM (#719)
#
# A bot that plays with the real mouse needs the game window to stay in front for the whole run. Any rig that opens a
# window meanwhile (a Workbench compile gate, an autotest client, a server console) steals the focus, and the game stops
# reading the mouse. So while a mouse-driven run holds the screen it writes a claim file, and every launch below waits
# for the claim to clear first. The claim names the holder's process: a claim whose holder is gone is ignored.
# The run that made the claim launches its own processes with MRS_QA_SCREEN_CLAIM set to the claim id, so it does not
# wait on itself.
# ------------------------------------------------------------------------------------------------
function Get-QaScreenClaimPath {
    $dir = Join-Path $env:LOCALAPPDATA 'MRS-QA'
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    return (Join-Path $dir 'screen-claim.json')
}

function Set-QaScreenClaim {
    param([string]$Id, [string]$Reason)
    $claim = [pscustomobject]@{ id = $Id; pid = $PID; reason = $Reason; since = (Get-Date).ToString('s') }
    [System.IO.File]::WriteAllText((Get-QaScreenClaimPath), ($claim | ConvertTo-Json -Compress))
}

function Clear-QaScreenClaim {
    param([string]$Id)
    $path = Get-QaScreenClaimPath
    $c = Get-QaScreenClaim
    if ($c -and $c.id -eq $Id) { Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue }
}

# The live claim, or $null (no file, unreadable, or its holder process has exited).
function Get-QaScreenClaim {
    $path = Get-QaScreenClaimPath
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    try { $c = [System.IO.File]::ReadAllText($path) | ConvertFrom-Json } catch { return $null }
    if ($null -eq $c -or -not (Get-Process -Id ([int]$c.pid) -ErrorAction SilentlyContinue)) { return $null }
    return $c
}

function Wait-QaScreenFree {
    param([int]$TimeoutSec = 3600)
    $c = Get-QaScreenClaim
    if ($null -eq $c) { return $true }
    if ($env:MRS_QA_SCREEN_CLAIM -and $env:MRS_QA_SCREEN_CLAIM -eq $c.id) { return $true }
    Write-Host ('    The screen is in use by a mouse-driven bot run (' + $c.reason + '); waiting before opening a window.')
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 10
        if ($null -eq (Get-QaScreenClaim)) { return $true }
    }
    return $false
}

function Start-QaWorkbench {
    param(
        [string]$Exe,
        [string[]]$Arguments,
        [string]$WorkingDirectory,
        # 'Hidden' for a window nobody needs to see (the dedicated server's console), so it never takes the front.
        [string]$WindowStyle = ''
    )
    if (-not (Wait-QaScreenFree)) { throw 'The screen stayed claimed by a mouse-driven bot run for an hour; not opening a window over it.' }
    $splat = @{
        FilePath     = $Exe
        PassThru     = $true
        ErrorAction  = 'Stop'
    }
    # Pre-quote: PS 5.1's Start-Process does not quote array elements, and our paths contain spaces.
    $quoted = ConvertTo-QaQuotedArgs -Arguments $Arguments
    if (@($quoted).Count -gt 0) { $splat['ArgumentList'] = $quoted }
    if ($WorkingDirectory) { $splat['WorkingDirectory'] = $WorkingDirectory }
    if ($WindowStyle) { $splat['WindowStyle'] = $WindowStyle }
    return (Start-Process @splat)
}

# Re-resolve the real PID(s) after the Steam bootstrap settles. "Seed PID exited but a matching
# process exists" is the normal respawn case, not a finished run.
function Resolve-QaWorkbenchPids {
    param(
        [int]$SeedPid,
        [string]$Token,
        [string]$NamePrefix = 'ArmaReforgerWorkbench',
        [int]$TimeoutSec = 20,
        [int]$PollSec = 2
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $best = @()
    while ((Get-Date) -lt $deadline) {
        $procs = Get-QaWorkbenchProcesses -NamePrefix $NamePrefix -OwnedByProfile $Token
        if (@($procs).Count -gt 0) {
            $best = @($procs | ForEach-Object { [int]$_.ProcessId })
            break
        }
        Start-Sleep -Seconds $PollSec
    }
    if ($best.Count -eq 0 -and $SeedPid -gt 0) {
        $seedAlive = $false
        try {
            $p = Get-Process -Id $SeedPid -ErrorAction Stop
            if ($p) { $seedAlive = $true }
        } catch {
            $seedAlive = $false
        }
        if ($seedAlive) { $best = @($SeedPid) }
    }
    return @($best)
}

function Test-QaAnyPidAlive {
    param([int[]]$Pids)
    foreach ($p in @($Pids)) {
        try {
            $proc = Get-Process -Id $p -ErrorAction Stop
            if ($proc) { return $true }
        } catch {
            continue
        }
    }
    return $false
}

#! #719 N2/ADV24-01: bind a number to the ACTUAL process object and KEEP that binding open. Get-QaProcessIdentity (and
#! .NET's own StartTime) reads the creation time through a handle that is released as soon as the query returns, so
#! between that check and a separately launched native consumer (taskkill /PID) the recorded instance can exit, Windows
#! can free its process object once the last retaining handle closes, and the number can be handed to an unrelated
#! process - which the consumer would then act on. Windows' PROCESS_INFORMATION contract only recycles a PID once ALL
#! handles are closed, so holding one retaining handle (Process.Handle, which .NET keeps until the object is disposed)
#! makes that reuse impossible for the whole operation. Returns the pinned Process, or $null when no handle could be
#! opened - the caller REFUSES to act on the number rather than trusting a bare lookup.
function Get-QaPinnedProcess {
    param([int]$ProcessId)
    if ($ProcessId -le 0) { return $null }
    $proc = $null
    try {
        $proc = Get-Process -Id $ProcessId -ErrorAction Stop
        $null = $proc.Handle      # force the handle OPEN and retain it; PID reuse needs every handle closed
        if ($proc.HasExited) { $proc.Dispose(); return $null }
        return $proc
    } catch {
        if ($proc) { try { $proc.Dispose() } catch { } }
        return $null
    }
}

#! #719 N2/ADV24-01: the creation identity of a process we HOLD a handle to. Read from the pinned object, so a live
#! number that was recycled between two lookups cannot masquerade as the pinned instance. '' means gone or unreadable,
#! which every caller treats as "not ours".
function Get-QaPinnedIdentity {
    param($PinnedProcess)
    if (-not $PinnedProcess) { return '' }
    try {
        if ($PinnedProcess.HasExited) { return '' }
        if (-not $PinnedProcess.StartTime) { return '' }
        return $PinnedProcess.StartTime.ToUniversalTime().ToString('o')
    } catch {
        return ''
    }
}

#! #719 U1/REG-01: what may be done with THIS number at THIS moment? 'gone' when nothing holds it, 'ours' when the
#! live instance is the one whose creation identity was recorded, and 'foreign' when a different process holds the
#! number (or the identity cannot be read at all). A live PID alone proves only that SOMETHING owns the number:
#! Windows frees a PID the moment its last handle closes, so a poll that asks only "is this PID alive" can act on an
#! unrelated successor. IdentityBound $false is for the legacy callers that recorded no identity and have nothing to
#! compare against - it keeps their numeric behaviour rather than pretending to a proof they do not hold.
#! #719 N2/ADV24-01: when the caller holds a PINNED process (Get-QaPinnedProcess), liveness AND identity come from that
#! retained handle rather than from a fresh lookup of the number. IdentityProbe stays authoritative when supplied: it is
#! the self-test seam that models the OS, and it must be able to report a change the pinned object cannot show.
function Get-QaPidActionState {
    param(
        [int]$ProcessId,
        [string]$ExpectedIdentity,
        [bool]$IdentityBound = $true,
        [scriptblock]$IdentityProbe = $null,
        $PinnedProcess = $null
    )
    if ($ProcessId -le 0) { return 'gone' }
    $actual = ''
    if ($IdentityBound -and $IdentityProbe) {
        if (-not (Test-QaPidAlive -ProcessId $ProcessId)) { return 'gone' }
        $actual = [string](& $IdentityProbe $ProcessId)
    } elseif ($PinnedProcess) {
        $actual = Get-QaPinnedIdentity -PinnedProcess $PinnedProcess
        if ([string]::IsNullOrEmpty($actual)) { return 'gone' }
    } else {
        if (-not (Test-QaPidAlive -ProcessId $ProcessId)) { return 'gone' }
        if (-not $IdentityBound) { return 'ours' }
        $actual = Get-QaProcessIdentity -ProcessId $ProcessId
    }
    if (-not $IdentityBound) { return 'ours' }
    if ([string]::IsNullOrEmpty($ExpectedIdentity)) { return 'foreign' }
    if ([string]::IsNullOrEmpty($actual)) { return 'foreign' }
    if ($actual -ne $ExpectedIdentity) { return 'foreign' }
    return 'ours'
}

# Graceful first (flushes the buffered log tail), then a hard tree kill.
#
# #719 U1/REG-01: a PID is not ownership, and the entry check is not the whole operation. When the caller hands over
# the creation identities it recorded, EVERY destructive boundary revalidates the number against the recorded
# INSTANCE before acting - the close request, the grace-loop liveness poll (which asks "is OUR instance alive", never
# "is the number alive"), and the final escalation. An instance that changed under us - the recorded process exited
# and Windows handed its number to an unrelated process - is REFUSED at the boundary that noticed it, never closed
# and never killed, and is named in Replaced/RefusedAlive. Without identities the numeric behaviour is kept: those
# legacy callers hold no identity (orphan reaping re-resolves by command-line token; qa-run and persist-run recorded
# none), and this must not be turned into broader process matching.
#
# #719 N2/ADV24-01: re-checking is not enough when the destructive step is a SEPARATE native consumer. The escalation
# starts taskkill.exe with a bare number, and a fresh identity lookup (or .NET's StartTime query) releases its handle
# as soon as it returns, so between the last check and the consumer's own target lookup the recorded instance can exit,
# its process object be freed and its number be handed to an unrelated process that taskkill would then terminate. For
# the identity-bound path this operation therefore PINS each number to the real process object (Get-QaPinnedProcess)
# BEFORE its identity is compared and HOLDS that pin - a retained handle makes PID reuse impossible - until the native
# kill has returned; it is disposed afterwards. A number whose handle cannot be obtained is REFUSED, never acted on.
# The numeric legacy callers (no identities) keep their unchanged behaviour: they have no instance to pin.
# IdentityProbe/CloseAction/KillAction/HandleBinder exist ONLY so the self-test can drive an exit/replacement inside the
# grace period deterministically; production callers pass none of them.
function Stop-QaWorkbench {
    param(
        [int[]]$Pids,
        [string[]]$Identities = @(),
        [int]$GraceSec = 10,
        [scriptblock]$IdentityProbe = $null,
        [scriptblock]$CloseAction = $null,
        [scriptblock]$KillAction = $null,
        [scriptblock]$HandleBinder = $null
    )

    # Pair every PID with the identity recorded for it. Unpaired/blank identities are kept as '' so the state
    # helper refuses them instead of trusting the number.
    $ids = @($Identities)
    $bound = ($ids.Count -gt 0)
    $expect = @{}
    $targets = New-Object System.Collections.ArrayList
    $i = 0
    foreach ($p in @($Pids)) {
        $ip = 0
        try { $ip = [int]$p } catch { $ip = 0 }
        $i++
        if ($ip -le 0) { continue }
        if ($expect.ContainsKey($ip)) { continue }
        $want = ''
        if ($i -le $ids.Count) { $want = [string]$ids[$i - 1] }
        $expect[$ip] = $want
        [void]$targets.Add($ip)
    }
    $targets = @($targets.ToArray())

    $stopped = New-Object System.Collections.ArrayList
    $killed = New-Object System.Collections.ArrayList
    $refused = New-Object System.Collections.ArrayList
    $replaced = New-Object System.Collections.ArrayList
    $gone = New-Object System.Collections.ArrayList
    $result = [pscustomobject]@{
        Graceful     = $false
        Killed       = @()
        Attempted    = @($targets)
        Stopped      = @()
        RefusedAlive = @()
        Replaced     = @()
        Gone         = @()
    }
    if ($targets.Count -eq 0) { $result.Graceful = $true; return $result }

    # #719 N2/ADV24-01: pin every identity-bound target to the real process object BEFORE any comparison, and keep the
    # pins until the escalation has returned (disposed in the finally below). Nothing is pinned when the caller carried
    # no identities: the numeric path has no instance to bind and is deliberately left as it was.
    $pinned = @{}
    if ($bound) {
        foreach ($p in $targets) {
            $proc = $null
            try {
                if ($HandleBinder) { $proc = & $HandleBinder $p } else { $proc = Get-QaPinnedProcess -ProcessId $p }
            } catch { $proc = $null }
            if ($proc) { $pinned[$p] = $proc }
        }
    }

    try {

        # Boundary 1 - the close request. Only an instance we can still prove is ours is asked to close.
        foreach ($p in $targets) {
            $pin = $null
            if ($pinned.ContainsKey($p)) { $pin = $pinned[$p] }
            if ($bound -and -not $pin) {
                # No handle could be obtained for this number: a live occupant we cannot bind is refused and named, never
                # closed and never killed on the strength of its number alone.
                if (Test-QaPidAlive -ProcessId $p) { [void]$refused.Add($p) } else { [void]$gone.Add($p) }
                continue
            }
            $state = Get-QaPidActionState -ProcessId $p -ExpectedIdentity $expect[$p] -IdentityBound $bound -IdentityProbe $IdentityProbe -PinnedProcess $pin
            if ($state -eq 'gone') { [void]$gone.Add($p); continue }
            if ($state -eq 'foreign') { [void]$refused.Add($p); [void]$replaced.Add($p); continue }
            try {
                if ($CloseAction) { & $CloseAction $p $pin } else {
                    if ($pin) { [void]$pin.CloseMainWindow() } else {
                        $proc = Get-Process -Id $p -ErrorAction Stop
                        [void]$proc.CloseMainWindow()
                    }
                }
                [void]$stopped.Add($p)
            } catch {
                [void]$gone.Add($p)
            }
        }

        # Boundary 2 - the grace period. The question is not "is this number alive" but "is OUR instance still alive",
        # so an instance that exits and is replaced mid-wait does not hold the graceful path open for a successor.
        $graceful = $false
        $deadline = (Get-Date).AddSeconds($GraceSec)
        while ((Get-Date) -lt $deadline) {
            $oursLeft = $false
            foreach ($p in @($stopped)) {
                $pin = $null
                if ($pinned.ContainsKey($p)) { $pin = $pinned[$p] }
                $state = Get-QaPidActionState -ProcessId $p -ExpectedIdentity $expect[$p] -IdentityBound $bound -IdentityProbe $IdentityProbe -PinnedProcess $pin
                if ($state -eq 'ours') { $oursLeft = $true; continue }
                if ($state -eq 'foreign') {
                    if (-not ($replaced -contains $p)) { [void]$replaced.Add($p) }
                    if (-not ($refused -contains $p)) { [void]$refused.Add($p) }
                }
            }
            if (-not $oursLeft) { $graceful = $true; break }
            Start-Sleep -Seconds 1
        }
        $result.Graceful = $graceful

        # Boundary 3 - escalation. Revalidate AGAIN at the kill itself: a number that changed hands during the grace
        # period belongs to a process that was never ours, and is left alone. The pin is STILL HELD here, so the number
        # cannot be recycled between this check and the native consumer's target lookup.
        if (-not $graceful) {
            foreach ($p in @($stopped)) {
                $pin = $null
                if ($pinned.ContainsKey($p)) { $pin = $pinned[$p] }
                $state = Get-QaPidActionState -ProcessId $p -ExpectedIdentity $expect[$p] -IdentityBound $bound -IdentityProbe $IdentityProbe -PinnedProcess $pin
                if ($state -eq 'gone') { continue }
                if ($state -eq 'foreign') {
                    if (-not ($replaced -contains $p)) { [void]$replaced.Add($p) }
                    if (-not ($refused -contains $p)) { [void]$refused.Add($p) }
                    continue
                }
                try {
                    # taskkill /T kills the whole tree; PS 5.1's Stop-Process cannot.
                    if ($KillAction) { & $KillAction $p $pin } else { & taskkill.exe /PID $p /T /F | Out-Null }
                    [void]$killed.Add($p)
                } catch {
                    continue
                }
            }
        }

        $result.Stopped = @($stopped.ToArray())
        $result.Killed = @($killed.ToArray())
        $result.RefusedAlive = @($refused.ToArray())
        $result.Replaced = @($replaced.ToArray())
        $result.Gone = @($gone.ToArray())
        return $result

    } finally {
        # Release the pins only AFTER the native kill has returned: while one is held the number cannot be recycled.
        foreach ($k in @($pinned.Keys)) {
            try { $pinned[$k].Dispose() } catch { }
        }
    }
}

# Completion model (no autotest dependency): finish as soon as a completion marker appears, or the
# log goes quiet for QuietSec after the compile phase was seen, or the process exits, or we time out.
function Wait-QaRunCompletion {
    param(
        [int[]]$Pids,
        [string[]]$LogSearchRoots,
        [string]$LogFileName = 'console.log',
        [string]$OwnershipToken,
        [string[]]$ReportFileNames,
        [string[]]$ReportExcludePatterns,
        [datetime]$StartTime,
        [int]$TimeoutSec = 2400,
        [int]$QuietSec = 60,
        # The shorter quiet window once the log shows BOTH the finished compile and Workbench's own startup
        # line. QuietSec covered the whole time Workbench sat idle after both, which was most of a minute of
        # every run. 0 = keep QuietSec throughout.
        [int]$SettledQuietSec = 0,
        [int]$PollSec = 2,
        [bool]$WatchForReport = $false,
        $Patterns,
        [scriptblock]$OnProgress
    )

    $deadline = $StartTime.AddSeconds($TimeoutSec)
    $lastSize = -1
    $lastChange = Get-Date
    $logPath = $null
    $reportPath = $null
    $compileSeen = $false
    $settled = $false
    $reason = 'timeout'

    while ($true) {
        Start-Sleep -Seconds $PollSec
        $now = Get-Date

        if ($null -eq $logPath) {
            $logPath = Find-QaConsoleLog -SearchRoots $LogSearchRoots -FileName $LogFileName -Since $StartTime -OwnershipToken $OwnershipToken -Patterns $Patterns
            if ($logPath) { $lastChange = $now }
        }

        if ($logPath -and (Test-Path -LiteralPath $logPath)) {
            $item = Get-Item -LiteralPath $logPath -ErrorAction SilentlyContinue
            if ($item) {
                if ($item.Length -ne $lastSize) {
                    $lastSize = $item.Length
                    $lastChange = $now
                }
            }
            if ((-not $compileSeen) -or (($SettledQuietSec -gt 0) -and (-not $settled))) {
                $lines = Read-QaLines -Path $logPath
                $probe = Get-QaLogFindings -Lines $lines -Patterns $Patterns
                if ($probe.CompileStarted -or $probe.CompileCompleted) { $compileSeen = $true }
                $startedPattern = Get-QaProp -Object $Patterns -Name 'WorkbenchStarted' -Default ''
                if ($probe.CompileCompleted -and $startedPattern -and (@($lines | Where-Object { $_ -match $startedPattern }).Count -gt 0)) {
                    $settled = $true
                }
            }
        }

        # Only a real autotest run should treat "a report appeared" as completion; otherwise a stale
        # XML lying around the profile would end the wait before the compile even started.
        if ($WatchForReport -and ($null -eq $reportPath)) {
            $reportPath = Find-QaTestReport -SearchRoots $LogSearchRoots -FileNames $ReportFileNames -ExcludePatterns $ReportExcludePatterns -Since $StartTime
            if ($reportPath) {
                $reason = 'report'
                break
            }
        }

        # With no resolved PID we cannot tell "exited" from "never adopted", so process liveness is
        # not a completion signal - fall through to quiescence/timeout instead of finishing instantly.
        if (@($Pids).Count -gt 0) {
            if (-not (Test-QaAnyPidAlive -Pids $Pids)) {
                $reason = 'exited'
                break
            }
        }

        if ($compileSeen) {
            $quietFor = ($now - $lastChange).TotalSeconds
            $window = $QuietSec
            if ($settled -and ($SettledQuietSec -gt 0) -and ($SettledQuietSec -lt $QuietSec)) { $window = $SettledQuietSec }
            if ($quietFor -ge $window) {
                $reason = 'quiescent'
                break
            }
        }

        if ($now -ge $deadline) {
            $reason = 'timeout'
            break
        }

        if ($OnProgress) {
            $elapsed = [int]($now - $StartTime).TotalSeconds
            & $OnProgress $elapsed $compileSeen $logPath
        }
    }

    return [pscustomobject]@{
        Reason      = $reason
        LogPath     = $logPath
        ReportPath  = $reportPath
        CompileSeen = $compileSeen
        ElapsedSec  = [int]((Get-Date) - $StartTime).TotalSeconds
    }
}

# ------------------------------------------------------------------------------------------------
# AUTOTEST completion model - deliberately the OPPOSITE of the compile gate's.
#
# VERIFIED 2026-08-04 by live runs: the GAME process launched with -autotest EXITS ON ITS OWN with
# code 0 when the run completes (smoke suite ~10 s, full group ~130 s). So natural exit is the
# NORMAL path here and the timeout is only a backstop - the reverse of Workbench, which never exits.
#
# Three exit shapes have to be tolerated:
#   'exited'            - we saw the process live, then it went away. The good case.
#   'exited-unobserved' - it finished faster than the adoption poll (a smoke suite can be ~10 s) and
#                         we never caught it alive. Still a completion; the report decides.
#   'timeout'           - a distinct failure. The caller kills it and reports a hang, NOT a test
#                         failure, because "did not exit" tells us nothing about the assertions.
# ------------------------------------------------------------------------------------------------
function Wait-QaProcessExit {
    param(
        [int]$SeedPid,
        [string]$Token,
        [string]$NamePrefix,
        [datetime]$StartTime,
        [int]$TimeoutSec = 900,
        [int]$PollSec = 2,
        [int]$AdoptSec = 20,
        [scriptblock]$OnProgress
    )

    $deadline = $StartTime.AddSeconds($TimeoutSec)
    $sawLive = $false
    $pids = @()
    $reason = 'timeout'

    while ($true) {
        $now = Get-Date
        $live = @(Get-QaWorkbenchProcesses -NamePrefix $NamePrefix -OwnedByProfile $Token)

        if ($live.Count -gt 0) {
            $sawLive = $true
            $pids = @($live | ForEach-Object { [int]$_.ProcessId })
        } elseif ($sawLive) {
            $reason = 'exited'
            $pids = @()
            break
        } else {
            # Never adopted. Either the Steam bootstrap has not settled yet, or the run was so short
            # it was already gone by the first poll. Only the seed PID can tell those apart.
            $seedAlive = $false
            if ($SeedPid -gt 0) {
                try {
                    $p = Get-Process -Id $SeedPid -ErrorAction Stop
                    if ($p) { $seedAlive = $true }
                } catch {
                    $seedAlive = $false
                }
            }
            $elapsed = ($now - $StartTime).TotalSeconds
            if ((-not $seedAlive) -and ($elapsed -ge $AdoptSec)) {
                $reason = 'exited-unobserved'
                break
            }
        }

        if ($now -ge $deadline) {
            $reason = 'timeout'
            break
        }

        if ($OnProgress) {
            & $OnProgress ([int]($now - $StartTime).TotalSeconds) $sawLive
        }
        Start-Sleep -Seconds $PollSec
    }

    return [pscustomobject]@{
        Reason     = $reason
        Pids       = @($pids)
        SawLive    = $sawLive
        ElapsedSec = [int]((Get-Date) - $StartTime).TotalSeconds
    }
}

# ------------------------------------------------------------------------------------------------
# HARNESS-vs-INTERACTIVE discriminator (#986).
#
# The compile gate's queue matched on PROCESS NAME PREFIX alone, so it could not tell another agent's
# headless run from the user's own editor session. MEASURED 2026-08-20: an interactive
# "World Editor - ...MRS_Conflict_Eden.ent*" session (unsaved changes) sat open for 129 minutes and
# silently stalled every queued compile run on the machine. This is the same flaw Wait-QaMpIdle was
# already fixed for (#954 review) - the MP path was scoped to what is genuinely contended; the COMPILE
# path never was.
#
# The discriminator is the -profile ARGUMENT VALUE, not the presence of -profile and not the window
# title:
#  * Every harness launch (compile gate, plugin stage, autotest stage, server gate) is templated with
#    "-profile {PROFILE}", and {PROFILE} is always <worktree>\.qa\runs\<stamp>\profile\<slot> -
#    New-QaRunContext / Get-QaStageProfileDir can produce nothing else. So the ".qa\runs\" segment is
#    the harness's own signature, present in EVERY harness launch and absent from a Steam-launched one
#    (verified 2026-08-20: interactive PID 9408 carried no -profile argument at all).
#  * "-profile is present" alone is too weak: a user hand-launching with their own profile directory
#    would be misread as a harness run and silently waited on forever.
#  * "the profile is under THIS worktree" is too strong: other agents' runs live in other worktrees and
#    those are exactly the runs the queue must still wait for.
#  * The window title is NOT usable. A -wbModule launch still creates a Workbench window, so both
#    shapes have titles; only the command line separates them.
#
# Paths are normalised to backslashes first: WorktreePath can be passed with forward slashes (the M3
# audit did exactly that: -Worktree C:/wt/mrs-m3audit) and Join-Path preserves them.
# ------------------------------------------------------------------------------------------------
function Test-QaHarnessCommandLine {
    param([string]$CommandLine)
    if ([string]::IsNullOrWhiteSpace($CommandLine)) { return $false }
    $norm = $CommandLine.Replace('/', '\')
    if ($norm.IndexOf('-profile', [System.StringComparison]::OrdinalIgnoreCase) -lt 0) { return $false }
    # soak-run keeps its profiles outside any worktree (%LOCALAPPDATA%\MRS_QA_Soak\runs, #1388), and is just as much a
    # harness run: the other rigs must see it, wait for it, and not mistake it for somebody's own session.
    if ($norm.IndexOf('\MRS_QA_Soak\runs\', [System.StringComparison]::OrdinalIgnoreCase) -ge 0) { return $true }
    return ($norm.IndexOf('\.qa\runs\', [System.StringComparison]::OrdinalIgnoreCase) -ge 0)
}

# A one-line human description of a process, for refusal messages. The window title is the part the
# user actually recognises ("World Editor - ...MRS_Conflict_Eden.ent*"), so it is worth the extra
# Get-Process call - but it is presentation only, never the discriminator.
function Get-QaProcessDescription {
    param($Process)
    if ($null -eq $Process) { return '(unknown process)' }
    $procPid = [int]$Process.ProcessId
    $text = 'PID ' + $procPid + '  ' + $Process.Name
    try {
        $po = Get-Process -Id $procPid -ErrorAction Stop
        if ($po -and -not [string]::IsNullOrWhiteSpace($po.MainWindowTitle)) {
            $text = $text + '  window: "' + $po.MainWindowTitle + '"'
        }
    } catch {
        # The process may have exited between the CIM query and here; the PID + name still identify it.
    }
    return $text
}

# Split a set of matched processes into the harness's own and everything else. Returned as two arrays
# rather than a filtered list because the CALLER treats them differently: harness runs are waited for,
# interactive ones are refused (see the qa-run concurrency guard).
function Split-QaWorkbenchProcesses {
    param($Processes)
    $harness = New-Object System.Collections.ArrayList
    $interactive = New-Object System.Collections.ArrayList
    foreach ($p in @($Processes)) {
        if ($null -eq $p) { continue }
        if (Test-QaHarnessCommandLine -CommandLine $p.CommandLine) {
            [void]$harness.Add($p)
        } else {
            [void]$interactive.Add($p)
        }
    }
    return [pscustomobject]@{
        Harness     = @($harness.ToArray())
        Interactive = @($interactive.ToArray())
    }
}

# Queued mode: wait until no other Workbench instance is running.
#
# -ScopeToHarness narrows the wait to harness-launched instances (the discriminator above). Callers
# that pass it MUST deal with interactive instances themselves - the whole point is that waiting on
# one is never right. Left off, the behaviour is the original name-prefix wait.
function Wait-QaWorkbenchFree {
    param(
        [string]$NamePrefix = 'ArmaReforgerWorkbench',
        [int]$TimeoutSec = 1800,
        [int]$PollSec = 5,
        [switch]$ScopeToHarness
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $procs = @(Get-QaWorkbenchProcesses -NamePrefix $NamePrefix)
        if ($ScopeToHarness) { $procs = @((Split-QaWorkbenchProcesses -Processes $procs).Harness) }
        if (@($procs).Count -eq 0) { return $true }
        $left = [int]($deadline - (Get-Date)).TotalSeconds
        $what = 'Workbench instance(s)'
        if ($ScopeToHarness) { $what = 'harness-launched instance(s)' }
        Write-QaInfo ("Waiting for " + @($procs).Count + " " + $what + " to close... (" + $left + "s left)")
        Start-Sleep -Seconds $PollSec
    }
    return $false
}

# ------------------------------------------------------------------------------------------------
# SHARED ENGINE LOCK (#1139)
#
# qa-run already refuses to race another autotest (#938) and another Workbench compile (#986). Both
# of those queues key on ONE process name, so they are blind to the other rigs: an autotest launches
# ArmaReforgerSteamDiag.exe while persist-run launches ArmaReforgerServerDiag.exe, and neither sees
# the other. Two heavyweight engine runs from different worktrees therefore start happily on top of
# each other.
#
# That is not theoretical. Measured 2026-09-21 (#1112): persistence runs saved at 22:17 and 22:38;
# an -autotest sweep began in a different worktree at ~22:40; every persistence run from 22:47
# onward blocked with IsSavingAllowed()=0, including a control feature that passes on Dev. A
# byte-identical script module (same Game CRC32) saved before the sweep and blocked during it, so
# the tree was ruled out. Correlation rather than proof - but a blocked run costs ~12 minutes AND
# looks exactly like a failing fix, because StartTracking() also returns false in those runs.
#
# These helpers are deliberately name-prefix-plural and EXCLUDE the caller's own run, so a rig can
# ask "is any OTHER harness engine process running?" without waiting on itself.
# ------------------------------------------------------------------------------------------------

# Engine process families that actually load a world. The Workbench compile gate is NOT here: it is
# comparatively light, it has its own queue (#986), and nothing implicates it.
function Get-QaEngineNamePrefixes {
    return @('ArmaReforgerSteam', 'ArmaReforgerServer')
}

# The worktree that launched a process, read off its own command line (-addonsDir C:\wt\<name>\Addons).
# Presentation only - it makes "wait for someone else" actionable by naming whose run it is.
function Get-QaProcessWorktree {
    param([string]$CommandLine)
    if ([string]::IsNullOrWhiteSpace($CommandLine)) { return '' }
    # [char]47 / [char]92 rather than quoted slashes: this file is edited by scripts often enough
    # that a literal backslash in a quoted string is a standing foot-gun.
    $norm = $CommandLine.Replace([char]47, [char]92)
    $pm = [regex]::Match($norm, '-profile\s+"?([^"\s]+)\\\.qa\\runs\\', 'IgnoreCase')
    $m = [regex]::Match($norm, '-addonsDir\s+"?([^"\s]+)\\Addons', 'IgnoreCase')
    if ($m.Success) {
        $cand = $m.Groups[1].Value
        if ($cand -notmatch '\\\.bgone-stage\\') { return $cand }
        if ($pm.Success) { return $pm.Groups[1].Value }
        return $cand
    }
    if ($pm.Success) { return $pm.Groups[1].Value }
    return ''
}

# The FILTER, split out from the enumeration so it can be tested with synthetic rows. Everything
# that decides whether a process blocks us lives here; Get-QaForeignEngineProcesses below is only
# the Get-CimInstance call. Keeping the two apart is what lets the self-test mutate this logic.
function Select-QaForeignEngineProcesses {
    param(
        $Processes,
        [string]$ExcludeToken
    )
    $found = New-Object System.Collections.ArrayList
    foreach ($p in @($Processes)) {
        if ($null -eq $p) { continue }
        # Only harness runs. An INTERACTIVE game or server the user started themselves is their
        # business: refusing to run because someone is playing would be worse than the race.
        if (-not (Test-QaHarnessCommandLine -CommandLine $p.CommandLine)) { continue }
        # ...and never ourselves, or a rig waits on its own children forever.
        if ((-not [string]::IsNullOrWhiteSpace($ExcludeToken)) -and (Test-QaProfileOwned -CommandLine $p.CommandLine -Token $ExcludeToken)) { continue }
        [void]$found.Add($p)
    }
    return @($found.ToArray())
}

# Harness-launched engine processes that are NOT ours. ExcludeToken is the caller's own profile/run
# directory, which appears in its children's command lines.
function Get-QaForeignEngineProcesses {
    param(
        [string]$ExcludeToken,
        [string[]]$NamePrefixes
    )
    if (-not $NamePrefixes -or @($NamePrefixes).Count -eq 0) { $NamePrefixes = @(Get-QaEngineNamePrefixes) }
    $all = New-Object System.Collections.ArrayList
    foreach ($prefix in @($NamePrefixes)) {
        foreach ($p in @(Get-QaWorkbenchProcesses -NamePrefix $prefix)) { [void]$all.Add($p) }
    }
    return @(Select-QaForeignEngineProcesses -Processes @($all.ToArray()) -ExcludeToken $ExcludeToken)
}

# One line per foreign process, naming the worktree. Used both by the queue and by the BLOCKED
# reason text, so a failed run carries the evidence instead of making the reader re-derive it.
function Get-QaForeignEngineSummary {
    param($Processes)
    $lines = New-Object System.Collections.ArrayList
    foreach ($p in @($Processes)) {
        if ($null -eq $p) { continue }
        $wt = Get-QaProcessWorktree -CommandLine $p.CommandLine
        $text = 'PID ' + [int]$p.ProcessId + '  ' + $p.Name
        if (-not [string]::IsNullOrWhiteSpace($wt)) { $text = $text + '  from ' + $wt }
        [void]$lines.Add($text)
    }
    return @($lines.ToArray())
}

# Block until no FOREIGN harness engine process remains, or the deadline passes. Returns $true when
# the machine is ours. The caller decides whether a timeout is fatal.
function Wait-QaEngineFree {
    param(
        [string]$ExcludeToken,
        [string[]]$NamePrefixes,
        [int]$TimeoutSec = 1800,
        [int]$PollSec = 5
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ($true) {
        $busy = @(Get-QaForeignEngineProcesses -ExcludeToken $ExcludeToken -NamePrefixes $NamePrefixes)
        if (@($busy).Count -eq 0) { return $true }
        if ((Get-Date) -ge $deadline) { return $false }
        $left = [int]($deadline - (Get-Date)).TotalSeconds
        Write-QaInfo ('Waiting for ' + @($busy).Count + ' other harness engine process(es) to finish... (' + $left + 's left)')
        foreach ($line in @(Get-QaForeignEngineSummary -Processes $busy)) { Write-QaInfo ('  ' + $line) }
        Start-Sleep -Seconds $PollSec
    }
}

# ------------------------------------------------------------------------------------------------
# PLUGIN STAGE completion model (epic #896, #897). Same SHAPE as Wait-QaProcessExit but it also
# captures the process EXIT CODE, which the plugin set via Workbench.Exit(n).
#
# The plugin run IS a Workbench launch (it carries -wbModule) and the plugin self-exits via
# Workbench.Exit - so natural exit is the NORMAL path and the timeout is only a backstop, exactly
# like the autotest stage. The exit code is the plugin's verdict and is read off the adopted
# process after it exits; the console.log marker corroborates it (exit codes are never taken from
# the process alone - the log is the second witness).
#
# Three exit shapes:
#   'exited'            - we saw the process live, then it went away; ExitCode was captured.
#   'exited-unobserved' - finished faster than the adoption poll; ExitCode may be unknown.
#   'timeout'           - a distinct failure: the plugin never called Workbench.Exit.
# ------------------------------------------------------------------------------------------------
function Wait-QaPluginExit {
    param(
        [int]$SeedPid,
        [string]$Token,
        [string]$NamePrefix,
        [datetime]$StartTime,
        [int]$TimeoutSec = 900,
        [int]$PollSec = 2,
        [int]$AdoptSec = 20,
        [scriptblock]$OnProgress
    )

    $deadline = $StartTime.AddSeconds($TimeoutSec)
    $sawLive = $false
    $pids = @()
    $procObjects = @()
    $exitCode = -1
    $reason = 'timeout'

    while ($true) {
        $now = Get-Date
        $live = @(Get-QaWorkbenchProcesses -NamePrefix $NamePrefix -OwnedByProfile $Token)

        if ($live.Count -gt 0) {
            $sawLive = $true
            $pids = @($live | ForEach-Object { [int]$_.ProcessId })
            # Hold Process objects so ExitCode is still readable after the process is gone.
            foreach ($livePid in $pids) {
                $already = $false
                foreach ($po in $procObjects) {
                    if ($po.Id -eq $livePid) { $already = $true; break }
                }
                if (-not $already) {
                    try {
                        $po = Get-Process -Id $livePid -ErrorAction Stop
                        if ($po) { $procObjects += $po }
                    } catch {
                        continue
                    }
                }
            }
        } elseif ($sawLive) {
            $reason = 'exited'
            break
        } else {
            # Never adopted. Either the Steam bootstrap has not settled yet, or the run was so short
            # it was already gone by the first poll. Only the seed PID can tell those apart.
            $seedAlive = $false
            if ($SeedPid -gt 0) {
                try {
                    $p = Get-Process -Id $SeedPid -ErrorAction Stop
                    if ($p) { $seedAlive = $true }
                } catch {
                    $seedAlive = $false
                }
            }
            $elapsed = ($now - $StartTime).TotalSeconds
            if ((-not $seedAlive) -and ($elapsed -ge $AdoptSec)) {
                $reason = 'exited-unobserved'
                break
            }
        }

        if ($now -ge $deadline) {
            $reason = 'timeout'
            break
        }

        if ($OnProgress) {
            & $OnProgress ([int]($now - $StartTime).TotalSeconds) $sawLive
        }
        Start-Sleep -Seconds $PollSec
    }

    # Read the exit code off the adopted process(es). A Process object holds its ExitCode after the
    # process has gone, as long as we keep the reference. Prefer a held object that has actually
    # exited - with a Steam bootstrap the seed may die early while the real process (also matching
    # our token) runs on and exits with the plugin's verdict.
    if (($reason -eq 'exited') -and ($procObjects.Count -gt 0)) {
        $exitedOne = $null
        foreach ($po in $procObjects) {
            try {
                $po.Refresh()
                if ($po.HasExited) { $exitedOne = $po; break }
            } catch {
                continue
            }
        }
        if ($null -eq $exitedOne) {
            $exitedOne = $procObjects[$procObjects.Count - 1]
            try { $exitedOne.Refresh() } catch { $exitedOne = $null }
        }
        if ($exitedOne -and $exitedOne.HasExited) { $exitCode = [int]$exitedOne.ExitCode }
    }

    return [pscustomobject]@{
        Reason     = $reason
        ExitCode   = $exitCode
        Pids       = @($pids)
        SawLive    = $sawLive
        ElapsedSec = [int]((Get-Date) - $StartTime).TotalSeconds
    }
}

# ------------------------------------------------------------------------------------------------
# Orphan reclamation (#954)
#
# mp-run.ps1 teardown is sound: try/finally, token-matched kills, plus a belt-and-braces sweep. It
# covers every path the SCRIPT can take. What it cannot cover is the script not taking a path at all -
# if the PowerShell host is force-killed (or crashes, or an outer harness terminates it), `finally`
# never runs and the engines it launched survive. MEASURED: after Stop-Process -Force on the host, both
# ArmaReforgerServerDiag and ArmaReforgerSteamDiag stay alive, ~4.7GB resident, and the server keeps
# UDP 2001 bound - so the NEXT run is refused and later runs starve for memory.
#
# The reclamation is deliberately EXACT rather than heuristic. Each run records its own host PID
# alongside the engine PIDs it launched, so an orphan has a precise definition:
#
#     an engine PID that is still alive whose recording host PID is dead
#
# That cannot misfire on a legitimately concurrent run (its host is alive, so it is skipped and left to
# the port guard to report honestly), and it cannot misfire on the user own game or server (never
# recorded in the first place). PID reuse is handled by re-checking that the live process still carries
# the recording run unique profile token before anything is killed.
# ------------------------------------------------------------------------------------------------
function Write-QaRunProcessRecord {
    param(
        [Parameter(Mandatory = $true)][string]$RunDir,
        # The unique profile path(s) this run launches under. This is the load-bearing field - it is
        # what identifies our processes later. Record it at LAUNCH time, before any PID is known.
        [Parameter(Mandatory = $true)][string[]]$Tokens,
        # Informational only: a breadcrumb for whoever reads the file. Never the kill list.
        [int[]]$EnginePids
    )
    if ([string]::IsNullOrWhiteSpace($RunDir)) { return }
    # The host's START TIME is recorded beside its PID and is load-bearing, not decoration. PID alone is
    # ambiguous: after the host dies the OS can hand that number to an unrelated process, which would make
    # a genuine orphan look like a LIVE concurrent run and leave it unreclaimable forever. A PID plus its
    # start time identifies one specific process instance.
    $hostStartUtc = ""
    try { $hostStartUtc = (Get-Process -Id $PID -ErrorAction Stop).StartTime.ToUniversalTime().ToString("o") } catch { $hostStartUtc = "" }

    $record = [pscustomobject]@{
        hostPid      = $PID
        hostStartUtc = $hostStartUtc
        enginePids   = @($EnginePids | Where-Object { $_ })
        tokens       = @(ConvertTo-QaTokenList -Tokens $Tokens)
        startedUtc   = (Get-Date).ToUniversalTime().ToString("o")
    }
    try {
        # Create the directory rather than assuming it exists. Callers record BEFORE launching, and in
        # qa-run that is now also before the artifact directories are made - so without this the write
        # throws, the catch below swallows it, no record is written, and reclamation silently degrades to
        # a no-op. That exact regression was introduced and then caught by an end-to-end test here; it is
        # invisible from the run output, which is why the catch now warns instead of staying quiet.
        if (-not (Test-Path -LiteralPath $RunDir)) {
            [void](New-Item -ItemType Directory -Force -Path $RunDir -ErrorAction Stop)
        }
        $path = Join-Path $RunDir "processes.json"
        $json = $record | ConvertTo-Json -Depth 4
        [System.IO.File]::WriteAllText($path, $json, (New-Object System.Text.UTF8Encoding($false)))
    } catch {
        # Still best-effort - a bookkeeping failure must never fail the run - but it is no longer SILENT.
        # A missing record costs a later run its ability to reclaim this one's orphans, so it is worth a
        # line in the transcript.
        Write-QaWarn ('Could not record this run process bookkeeping (#954 reclamation will not cover it): ' + $_.Exception.Message)
    }
}

#! Is the process that RECORDED a run still alive? PID alone cannot answer this - see the note in
#! Write-QaRunProcessRecord. When a start time was recorded, the live process must match it; a mismatch
#! means the PID was recycled and the true host is dead. Records written before this field existed fall
#! back to the PID-only answer rather than being treated as unreadable.
function Test-QaRecordedHostAlive {
    param([int]$HostPid, [string]$HostStartUtc)
    if ($HostPid -le 0) { return $false }
    $proc = $null
    try { $proc = Get-Process -Id $HostPid -ErrorAction Stop } catch { return $false }
    if (-not $proc) { return $false }
    if ([string]::IsNullOrWhiteSpace($HostStartUtc)) { return $true }
    $liveStart = ""
    try { $liveStart = $proc.StartTime.ToUniversalTime().ToString("o") } catch { return $true }
    return ($liveStart -eq $HostStartUtc)
}

function Test-QaPidAlive {
    param([int]$ProcessId)
    if ($ProcessId -le 0) { return $false }
    try {
        $null = Get-Process -Id $ProcessId -ErrorAction Stop
        return $true
    } catch {
        return $false
    }
}

#! #719 ARCH-01: ONE PROCESS INSTANCE's identity - its creation time in UTC. Windows recycles PIDs, so a
#! bare integer proves only that SOMETHING holds the number now; the creation time identifies the specific
#! instance our run launched. Returns '' when the process is gone or its start time cannot be read, which
#! callers must treat as "not the process we own" rather than as a match.
function Get-QaProcessIdentity {
    param([int]$ProcessId)
    if ($ProcessId -le 0) { return '' }
    try {
        $p = Get-Process -Id $ProcessId -ErrorAction Stop
        if ($p -and $p.StartTime) { return $p.StartTime.ToUniversalTime().ToString('o') }
    } catch {
        return ''
    }
    return ''
}

#! #719 ARCH-01: is the process holding $ProcessId still the SAME instance recorded for it? True only when it
#! is alive AND its creation identity matches the recorded one. An empty identity on either side is a
#! mismatch: an unverifiable occupant must never authorize a close, a kill, or real input.
function Test-QaProcessIdentity {
    param([int]$ProcessId, [string]$ExpectedStartUtc)
    if ($ProcessId -le 0) { return $false }
    if ([string]::IsNullOrEmpty($ExpectedStartUtc)) { return $false }
    $actual = Get-QaProcessIdentity -ProcessId $ProcessId
    if ([string]::IsNullOrEmpty($actual)) { return $false }
    return ($actual -eq $ExpectedStartUtc)
}

#! #719 REG-01: capture the creation identity of every PID in a list, PARALLEL to that list, so a later
#! operation can prove each number is still the process this run owns. '' marks an identity that could not be
#! read; every caller treats '' as "not ours", never as a match.
function Get-QaProcessIdentities {
    param([int[]]$Pids)
    $out = New-Object System.Collections.ArrayList
    foreach ($p in @($Pids)) { [void]$out.Add((Get-QaProcessIdentity -ProcessId ([int]$p))) }
    return @($out.ToArray())
}

#! #719 REG-01: the entries of parallel Pids/Identities whose live process is still the recorded instance, as
#! {Pid, Identity} objects, so a caller can both operate on them and NAME the ones it refused. A PID that is
#! alive but recycled, a PID whose identity was never recorded, and a PID that is already gone are all left
#! out. PURE (uses Get-QaProcessIdentity/Test-QaProcessIdentity in this file).
function Get-QaVerifiedProcessEntries {
    param([int[]]$Pids, [string[]]$Identities)
    $out = New-Object System.Collections.ArrayList
    $ps = @($Pids)
    $ids = @($Identities)
    for ($i = 0; $i -lt $ps.Count; $i++) {
        $p = 0
        try { $p = [int]$ps[$i] } catch { continue }
        if ($p -le 0) { continue }
        $want = ''
        if ($i -lt $ids.Count) { $want = [string]$ids[$i] }
        if (Test-QaProcessIdentity -ProcessId $p -ExpectedStartUtc $want) {
            [void]$out.Add([pscustomobject]@{ Pid = $p; Identity = $want })
        }
    }
    return @($out.ToArray())
}

#! #719 REG-01/U1: stop processes whose creation identity is re-checked at EVERY destructive boundary. This is the
#! guarantee mp-sweep's Stop-KeptServer applies to the server it kept; mp-run's FINAL kept-server teardown uses the
#! same call, so an adopted server's identity survives the whole scenario instead of decaying to a bare PID. The
#! entry filter below only decides who ENTERS the operation; the recorded identities are then carried INTO
#! Stop-QaWorkbench, which revalidates them at the close request, on every grace-period poll and at the escalation
#! kill. A number that is held by a different instance at ANY boundary - alive but recycled at entry, or replaced
#! while the grace period ran - is REFUSED (never closed, never killed) and named in RefusedAlive so the caller can
#! say so; the ones that changed during the operation are also named in Replaced. #719 N2/ADV24-01: the entries that do
#! enter are additionally PINNED to their live process object and that handle is held across the close, the wait and
#! the native kill, so the number cannot be recycled in the check-to-kill window; a number whose handle cannot be
#! obtained is refused rather than killed. Returns
#! { Stopped; RefusedAlive; Replaced; Gone; Graceful }.
function Stop-QaVerifiedWorkbench {
    param([int[]]$Pids, [string[]]$Identities, [int]$GraceSec = 10)
    $entries = @(Get-QaVerifiedProcessEntries -Pids $Pids -Identities $Identities)
    $verifiedPids = @($entries | ForEach-Object { [int]$_.Pid })
    $verifiedIds = @($entries | ForEach-Object { [string]$_.Identity })
    $refusedAlive = New-Object System.Collections.ArrayList
    $gone = New-Object System.Collections.ArrayList
    foreach ($p in @($Pids)) {
        $ip = 0
        try { $ip = [int]$p } catch { continue }
        if ($ip -le 0) { continue }
        if ($verifiedPids -contains $ip) { continue }
        if (Test-QaPidAlive -ProcessId $ip) { [void]$refusedAlive.Add($ip) } else { [void]$gone.Add($ip) }
    }
    $stop = $null
    if ($verifiedPids.Count -gt 0) {
        # The identities go WITH the PIDs: the operation keeps re-proving each number is the recorded instance.
        $stop = Stop-QaWorkbench -Pids $verifiedPids -Identities $verifiedIds -GraceSec $GraceSec
    }
    $stopped = @()
    $replaced = @()
    if ($stop) {
        $stopped = @($stop.Stopped)
        $replaced = @($stop.Replaced)
        $gone = @(@($gone.ToArray()) + @($stop.Gone))
        foreach ($p in @($stop.RefusedAlive)) {
            $ipp = 0
            try { $ipp = [int]$p } catch { continue }
            if ($ipp -gt 0 -and -not ($refusedAlive -contains $ipp)) { [void]$refusedAlive.Add($ipp) }
        }
    }
    return [pscustomobject]@{
        Stopped      = @($stopped)
        RefusedAlive = @($refusedAlive.ToArray())
        Replaced     = @($replaced)
        Gone         = @($gone)
        Graceful     = [bool]($null -ne $stop -and $stop.Graceful)
    }
}

#! One record per PREVIOUS run of this worktree that still has live engine processes, each tagged with
#! whether its host is alive (HostAlive = a concurrent run, NOT an orphan).
function Get-QaOrphanRuns {
    param(
        [Parameter(Mandatory = $true)][string]$WorktreePath,
        [string]$ExcludeRunDir
    )
    $out = New-Object System.Collections.ArrayList
    $runsRoot = Join-Path $WorktreePath ".qa/runs"
    if (-not (Test-Path -LiteralPath $runsRoot)) { return @() }

    $records = @()
    try {
        $records = @(Get-ChildItem -LiteralPath $runsRoot -Filter "processes.json" -Recurse -File -ErrorAction Stop)
    } catch {
        return @()
    }

    $excludeFull = ""
    if (-not [string]::IsNullOrWhiteSpace($ExcludeRunDir)) {
        try { $excludeFull = (Resolve-Path -LiteralPath $ExcludeRunDir -ErrorAction Stop).Path } catch { $excludeFull = $ExcludeRunDir }
    }

    # ONE filtered engine snapshot for the whole history (#1388): every record's tokens are matched against it in memory,
    # instead of one full process enumeration per token per record (100 retained records used to mean 200 enumerations).
    # A failed query reports nothing, as before: this only finds candidates; the kill re-checks (Invoke-QaOrphanReap).
    $snap = Get-QaEngineSnapshot
    if (-not $snap.Ok) { return @() }

    foreach ($f in $records) {
        if (-not [string]::IsNullOrWhiteSpace($excludeFull)) {
            if ($f.DirectoryName -eq $excludeFull) { continue }
        }
        $rec = $null
        try { $rec = Get-Content -LiteralPath $f.FullName -Raw | ConvertFrom-Json } catch { continue }
        if (-not $rec) { continue }

        $hostAlive = Test-QaRecordedHostAlive -HostPid ([int]$rec.hostPid) -HostStartUtc ([string]$rec.hostStartUtc)

        # Match on the run's unique PROFILE TOKEN, not on the recorded PIDs.
        #
        # Tokens are strictly better as the primary matcher, for two reasons found while extending this
        # to qa-run.ps1. First, PID reuse stops being a concern at all: a recycled PID belonging to
        # something else cannot carry our profile path on its command line. Second, and the reason it
        # had to change: some stages do not KNOW their real PIDs until the run is over. qa-run's
        # autotest stage resolves PIDs from Wait-QaProcessExit AFTER the wait returns, and Steam can
        # respawn the seed process into a different PID entirely - so a host death mid-run would have
        # recorded either nothing or a stale PID. The token is known at launch and stays true throughout.
        #
        # The recorded PIDs are kept in the file as a diagnostic breadcrumb, not as the kill list.
        $live = New-Object System.Collections.ArrayList
        foreach ($proc in @((Test-QaRunGone -Target (New-QaRunTarget -Profiles @($rec.tokens)) -Snapshot $snap).Engines)) {
            $pidInt = 0
            try { $pidInt = [int]$proc.ProcessId } catch { continue }
            if ($pidInt -le 0) { continue }
            if ($live -notcontains $pidInt) { [void]$live.Add($pidInt) }
        }

        if (@($live).Count -eq 0) { continue }
        [void]$out.Add([pscustomobject]@{
            RunDir    = $f.DirectoryName
            HostPid   = [int]$rec.hostPid
            HostAlive = $hostAlive
            Tokens    = @($rec.tokens)
            Pids      = @($live.ToArray())
        })
    }
    return @($out.ToArray())
}

#! Kills the engine processes of previous runs of THIS worktree whose host is dead. Runs whose host is
#! still alive are returned untouched so the caller can report a genuine concurrent run honestly.
function Invoke-QaOrphanReap {
    param(
        [Parameter(Mandatory = $true)][string]$WorktreePath,
        [string]$ExcludeRunDir,
        [int]$GraceSec = 5
    )
    $found = @(Get-QaOrphanRuns -WorktreePath $WorktreePath -ExcludeRunDir $ExcludeRunDir)
    $reaped = New-Object System.Collections.ArrayList
    $concurrent = New-Object System.Collections.ArrayList

    foreach ($run in $found) {
        if ($run.HostAlive) { [void]$concurrent.Add($run); continue }

        # Re-resolve by token IMMEDIATELY before killing rather than trusting the scan.
        #
        # Get-QaOrphanRuns proves the PIDs were ours at SCAN time. Between the scan and the kill an
        # orphan can exit and the OS can hand its number to something unrelated - and taskkill /T /F on a
        # recycled PID would then destroy a process that was never ours. Re-asking the OS "which live
        # processes carry this token" collapses that window to the gap between this query and the kill,
        # and anything it returns is ours by construction. This NUMERIC path re-resolves by token and has no recorded
        # creation identity to pin, so the window is not provably zero here (the identity-bound stop does hold a real
        # OS handle - see #719 N2); this is the achievable bound for orphan reaping, and it is why the kill list is
        # rebuilt here instead of reused from the scan.
        $fresh = New-Object System.Collections.ArrayList
        $freshSnap = Get-QaEngineSnapshot
        if (-not $freshSnap.Ok) { continue }
        foreach ($proc in @((Test-QaRunGone -Target (New-QaRunTarget -Profiles @($run.Tokens)) -Snapshot $freshSnap).Engines)) {
            $pidInt = 0
            try { $pidInt = [int]$proc.ProcessId } catch { continue }
            if ($pidInt -gt 0 -and ($fresh -notcontains $pidInt)) { [void]$fresh.Add($pidInt) }
        }
        if (@($fresh).Count -eq 0) { continue }

        $stop = Stop-QaWorkbench -Pids @($fresh.ToArray()) -GraceSec $GraceSec
        # taskkill returns before the process is gone; callers (and the lock's release scan) look right after.
        [void](Wait-QaPidsGone -Pids @($fresh.ToArray()))
        [void]$reaped.Add([pscustomobject]@{
            RunDir   = $run.RunDir
            HostPid  = $run.HostPid
            Pids     = @($fresh.ToArray())
            Graceful = $stop.Graceful
        })
    }

    return [pscustomobject]@{
        Reaped     = @($reaped.ToArray())
        Concurrent = @($concurrent.ToArray())
    }
}

#! Describes whatever holds a UDP port, so the caller can say something TRUE about it rather than guess.
#! Distinguishes a QA run (command line carries a .qa runs profile path) from the user own session -
#! the original message asserted the latter unconditionally, which sent #949 down the wrong path.
# ------------------------------------------------------------------------------------------------
# MP RIG LOCK (mp-run vs mp-run)
#
# #1139/#1142 serialise ACROSS rig types by process family - persist-run waits on harness autotest
# processes, qa-run's autotest stage waits on harness server processes. Two mp-runs are the SAME family
# on the SAME fixed port (UDP 2001), so that machinery never applied to them and nothing excluded them.
#
# Measured 2026-09-22 by two sessions independently: mp-run always launches its client at
# 127.0.0.1:2001, so a second run's client connects to the FIRST run's server. Different branches carry
# different scripts, so the handshake fails and the client is kicked (group=1 REPLICATION, reason=1
# SYSTEM_FAILURE). One session drew a real - and wrong - engine conclusion from that kick before
# tracing it. Another lost three runs to servers that never armed.
#
# -Queue does NOT prevent this. It is a start-time idle check: both runs can observe "idle" and both
# start. Only a lock taken BEFORE the server launch excludes.
#
# HOLDER IDENTITY IS PID **AND** PROCESS START TIME. Windows recycles pids; a crashed holder's pid can
# be reused by an unrelated process, and a pid-only liveness test would then report "holder alive"
# forever and wedge the rig - precisely the failure a staleness check exists to prevent.
# ------------------------------------------------------------------------------------------------

$script:QaRigDefaultLockPath = 'C:\wt\.mp-rig.lock'
# A lock file that cannot be parsed is treated as "being written" until it is this old. Records written by
# New-QaMpRigLock appear whole (temp file + move), so this only ever waits out a record left half-written by an
# older copy of the script, or one whose writer died.
$script:QaRigInitWindowSec = 15
# How long a contender waits to ENTER the admission mutex before reporting 'busy' (ms). The critical section is a few
# file operations, so a longer wait means its holder is stalled or dead mid-section.
$script:QaRigAdmissionWaitMs = 30000
# Seconds between the two clean scans that prove a run's engines are gone (Steam respawns the real engine from its bootstrap
# seed a moment after the seed exits, so one empty look is not proof).
$script:QaRigSettleSec = 3
# How long to wait for a killed process to actually disappear before calling it a survivor (ms).
$script:QaPidsGoneTimeoutMs = 4000
# A write of the lock file is tried this many times, this long apart, before it is called failed (always under the mutex).
$script:QaRigWriteTries = 5
$script:QaRigWriteDelayMs = 200
$script:QaLegacyWarned = $false

# MRS_QA_RIG_LOCK_PATH exists for the self-test (#1388): it points the lock at a temp file, and the mutex name below
# follows the path, so a test can never contend with (or wait on) the live rig.
function Get-QaMpRigLockPath {
    if (-not [string]::IsNullOrWhiteSpace($env:MRS_QA_RIG_LOCK_PATH)) { return $env:MRS_QA_RIG_LOCK_PATH }
    return $script:QaRigDefaultLockPath
}

# ------------------------------------------------------------------------------------------------
# HEAVYWEIGHT ADMISSION (#1388)
#
# One lock file, one machine-wide mutex, shared by every heavyweight launcher: qa-run's autotest stage, mp-run,
# persist-run (held across its restart gap) and soak-run. Compile-only work (Workbench gate, .rdb regeneration) never
# takes it: mp-run regenerates .rdb files through qa-run WHILE holding the lock, so locking the compile path would
# deadlock, and concurrent compiles are deliberately supported. The design is described at THE ADMISSION RECORD below.
#
# Every acquisition gets a unique TOKEN (a guid) recorded in the file. Release deletes the file only if the token still
# matches, so a run that lost its lock can never delete its successor's. NO NESTED HEAVYWEIGHT WORK: a token proves
# ownership for the run that took it and for the compile-only work it starts; a heavyweight child of the owner is refused at
# once ('nested'). The token lives in $env:MRS_QA_ADMISSION_TOKEN only so the release (and a kept-server sweep in the same
# process) can identify the lock; it is cleared when the lock is released or ended.
#
# SLOW WORK STAYS OUT OF THE MUTEX: process scans, port checks and the settle sleeps happen before it is taken; inside it the
# record is read, changed and written (the only waiting inside is the few short, bounded retries of the write itself).
#
# NOT COVERED: a force-killed host. `finally` does not run, so the lock stays on disk with a dead host; the next contender
# reclaims it only when everything the record owns (and any unconfirmed predecessor's) is shown gone.
# ------------------------------------------------------------------------------------------------
function Get-QaRigAdmissionMutexName {
    $path = Get-QaMpRigLockPath
    if ([string]::Equals($path, $script:QaRigDefaultLockPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        return 'Global\MRS_QaRigAdmission'
    }
    $sha = [System.Security.Cryptography.SHA1]::Create()
    $hash = $null
    try { $hash = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($path.ToLowerInvariant())) } finally { $sha.Dispose() }
    return ('Global\MRS_QaRigAdmission_' + ([System.BitConverter]::ToString($hash, 0, 6)).Replace('-', ''))
}

# Runs $Body while holding the admission mutex, and returns what it returns. Throws if the mutex cannot be entered in
# WaitMs (the caller treats that as "busy", never as "free"). An abandoned mutex (the previous holder died inside the
# section) is entered normally: every write below is whole-file atomic, so there is no half-state to repair.
function Invoke-QaRigAdmissionCritical {
    param([Parameter(Mandatory = $true)][scriptblock]$Body, [int]$WaitMs = $script:QaRigAdmissionWaitMs)
    $mutex = New-Object System.Threading.Mutex($false, (Get-QaRigAdmissionMutexName))
    $entered = $false
    try {
        try { $entered = $mutex.WaitOne($WaitMs) } catch [System.Threading.AbandonedMutexException] { $entered = $true }
        if (-not $entered) { throw ('the rig admission mutex was not available within ' + $WaitMs + ' ms') }
        return (& $Body)
    } finally {
        if ($entered) { try { $mutex.ReleaseMutex() } catch { } }
        $mutex.Dispose()
    }
}

# The current process's identity, as recorded in the lock.
function Get-QaMpLockIdentity {
    param([int]$ProcessId = $PID)
    $started = ''
    try {
        $p = Get-Process -Id $ProcessId -ErrorAction Stop
        if ($p.StartTime) { $started = $p.StartTime.ToUniversalTime().ToString('o') }
    } catch {
        # Process gone, or start time unreadable - an empty stamp makes the holder unverifiable, which
        # Test-QaMpLockHolderAlive treats as STALE rather than as live. Failing open is correct here:
        # refusing to run because we cannot identify a holder would wedge the rig just as badly.
    }
    return $started
}

# True only when the recorded holder is genuinely still running: pid alive AND its start time matches.
function Test-QaMpLockHolderAlive {
    param($Lock)
    if ($null -eq $Lock) { return $false }

    $holderPid = 0
    try { $holderPid = [int](Get-QaProp -Object $Lock -Name 'pid' -Default 0) } catch { return $false }
    if ($holderPid -le 0) { return $false }

    # Read defensively: under strict mode (soak-run) a missing property would throw instead of reading as "unverifiable".
    $recorded = [string](Get-QaProp -Object $Lock -Name 'pidStartedUtc' -Default '')
    if ([string]::IsNullOrWhiteSpace($recorded)) { return $false }

    $actual = Get-QaMpLockIdentity -ProcessId $holderPid
    if ([string]::IsNullOrWhiteSpace($actual)) { return $false }

    return ($actual -eq $recorded)
}

# Engine processes still alive from a given worktree, whoever launched them. This is what makes an
# ORPHAN visible: mp-run has exited (so its host pid is gone and the lock looks releasable) while its
# server/client survive and keep holding UDP 2001.
function Get-QaMpEnginesForWorktree {
    param([string]$WorktreePath)
    if ([string]::IsNullOrWhiteSpace($WorktreePath)) { return @() }

    $found = New-Object System.Collections.ArrayList
    foreach ($prefix in @(Get-QaEngineNamePrefixes)) {
        foreach ($p in @(Get-QaWorkbenchProcesses -NamePrefix $prefix)) {
            if ($null -eq $p -or -not $p.CommandLine) { continue }
            if ((Get-QaProcessWorktree -CommandLine $p.CommandLine) -eq $WorktreePath) { [void]$found.Add($p) }
        }
    }
    return @($found.ToArray())
}

# The rig is occupied while EITHER the holding run is alive OR it left engines behind. Host-pid
# liveness alone would hand the rig to the next run while an orphan still owns the port.
# ------------------------------------------------------------------------------------------------
# ORPHAN REAPER (follow-up to the MP rig lock, #1139)
#
# The lock DETECTS orphans - the rig lock reports the rig busy when engine processes from the
# holding worktree are still alive even though the host mp-run has exited. Detection alone means the
# rig stays wedged until a human notices, and MEASURED on 2026-09-22 that happened repeatedly: one
# session had to hand-kill leftovers at least four times in an evening, and an orphan holding UDP 2001
# ~96 s after its run "ended" is the likeliest cause of a REPLICATION kick that another session
# initially read as a genuine engine finding.
#
# So: reap at the source. Kill the engines belonging to a worktree we own, rather than leaving them.
#
# THE SAFETY RULE, and it is the whole reason this is a separate change: match on the WORKTREE PATH
# from the process command line, NEVER on the image name. Riley plays Arma on this box. A reaper that
# killed by image name would close their game mid-session, which is far worse than a wedged rig.
# Get-QaMpEnginesForWorktree already applies exactly that discriminator, so this builds on it rather
# than inventing a second rule that could drift from it.
# ------------------------------------------------------------------------------------------------
# IS THE RIG PORT ACTUALLY FREE?
#
# This exists because "the reap returned" and "the port is free" came apart repeatedly on 2026-09-22/23
# and nothing downstream noticed. Stop-QaMpOrphanEngines' taskkill can fail on a child process; it
# WARNED and mp-run released the lock anyway, so the rig advertised itself free while a surviving
# engine still owned UDP 2001. The next run's server then failed with "Unable to start replication"
# and its client silently joined the WRONG server - reporting clean, confident numbers about a world it
# was not authoritative for. That cost one published-then-retracted finding across two sessions.
#
# A successful reap is a PROXY for what matters. The port is the thing itself, so check the thing
# itself. This also closes the hole if some future reap path fails in a way nobody anticipated.
#
# Returns $true when the port is free (including when the OS gives us no way to tell, since refusing
# to release a lock on an unanswerable question would wedge the rig permanently).
function Test-QaMpPortFree {
    param([int]$Port = 2001)

    try {
        $held = @(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction Stop)
        return (@($held).Count -eq 0)
    } catch [Microsoft.PowerShell.Cmdletization.Cim.CimJobException] {
        # "No MSFT_NetUDPEndpoint objects found" is how this cmdlet reports an unused port.
        return $true
    } catch {
        # Cmdlet missing or the query failed for an unrelated reason. Say free rather than deadlock the
        # rig, but say so out loud - a silent assumption here is the exact failure being fixed.
        Write-QaInfo ('  NOTE: could not query UDP ' + $Port + ' (' + $_.Exception.Message + '); assuming free')
        return $true
    }
}

# ------------------------------------------------------------------------------------------------
# Which pids still hold the rig port? Used for the error message when a release is refused, because
# "port still held" is far less actionable than "port still held by pid 61672".
function Get-QaMpPortHolders {
    param([int]$Port = 2001)

    try {
        return @(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction Stop | Select-Object -ExpandProperty OwningProcess -Unique)
    } catch {
        return @()
    }
}

# Engines still alive from a worktree, split by whether this run can PROVE they are its own. Ownership is the unique
# profile token on the engine's command line, never the worktree alone: a worktree path is shared by every run in it,
# including a game the user started by hand with -addonsDir pointing there (#1388).
function Split-QaWorktreeEnginesByOwner {
    param([string]$WorktreePath, [string[]]$OwnedTokens)
    $owned = New-Object System.Collections.ArrayList
    $foreign = New-Object System.Collections.ArrayList
    $tokens = @($OwnedTokens | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    foreach ($e in @(Get-QaMpEnginesForWorktree -WorktreePath $WorktreePath)) {
        $mine = $false
        foreach ($tok in $tokens) {
            if (Test-QaProfileOwned -CommandLine $e.CommandLine -Token $tok) { $mine = $true; break }
        }
        if ($mine) { [void]$owned.Add($e) } else { [void]$foreign.Add($e) }
    }
    return [pscustomobject]@{ Owned = @($owned.ToArray()); Foreign = @($foreign.ToArray()) }
}

# Kills this run's own engines that are still alive in a worktree. Engines in the same worktree that do NOT carry one of
# OwnedTokens (a manual game, another admitted run, a previous run whose host may still be alive) are left alone and
# returned as Foreign; a previous run's dead-owner orphans are reclaimed separately, by Invoke-QaOrphanReap, which
# proves ownership from that run's own record. With no OwnedTokens nothing is killed.
function Stop-QaMpOrphanEngines {
    # NOT Mandatory: an empty path must return 0 rather than throw, matching the sibling
    # Get-QaMpEnginesForWorktree. A reaper that throws on a missing path would take down the exit
    # handler it is called from, losing the lock release with it.
    param(
        [string]$WorktreePath,
        [string[]]$OwnedTokens = @(),
        [int]$GraceMs = 3000,
        [switch]$DryRun
    )

    if ([string]::IsNullOrWhiteSpace($WorktreePath)) {
        return [pscustomobject]@{ Killed = 0; Survivors = @(); Foreign = @() }
    }

    $split = Split-QaWorktreeEnginesByOwner -WorktreePath $WorktreePath -OwnedTokens $OwnedTokens
    $engines = @($split.Owned)
    if ($engines.Count -eq 0) {
        return [pscustomobject]@{ Killed = 0; Survivors = @(); Foreign = @($split.Foreign) }
    }

    $killed = 0
    $survivors = New-Object System.Collections.ArrayList
    foreach ($e in $engines) {
        $enginePid = 0
        try { $enginePid = [int]$e.ProcessId } catch { continue }
        if ($enginePid -le 0) { continue }

        $name = ''
        try { $name = [string]$e.Name } catch { }

        if ($DryRun) {
            Write-QaInfo ('  would reap ' + $name + ' pid ' + $enginePid + ' (worktree ' + $WorktreePath + ')')
            $killed++
            continue
        }

        Write-QaInfo ('  reaping this run''s leftover ' + $name + ' pid ' + $enginePid + ' (worktree ' + $WorktreePath + ')')

        # Graceful first so the engine flushes its log tail - the logs are the entire point of a run.
        try {
            $proc = Get-Process -Id $enginePid -ErrorAction Stop
            [void]$proc.CloseMainWindow()
        } catch { }

        Start-Sleep -Milliseconds $GraceMs

        # Then a hard tree kill. PS 5.1's Stop-Process has no tree kill, and the engine spawns children.
        if (Test-QaAnyPidAlive -Pids @($enginePid)) {
            # The engine can exit between the check and the kill. taskkill then writes "no running instance" to
            # stderr, which 2>&1 turns into an error record that a caller's ErrorActionPreference=Stop makes fatal
            # (it aborted an mp-sweep mid-teardown, 2026-10-22). Wait-QaPidsGone below is the real check.
            try { & taskkill.exe /PID $enginePid /T /F 2>&1 | Out-Null } catch { }
        }

        # taskkill returns before the process object is gone: wait for it before calling the victim a survivor.
        if (Wait-QaPidsGone -Pids @($enginePid)) { $killed++ }
        else {
            Write-QaWarn ('  pid ' + $enginePid + ' SURVIVED the reap - the rig port may still be held')
            [void]$survivors.Add($enginePid)
        }
    }

    # Returning a bare count made a failed reap indistinguishable from a clean one: mp-run tested
    # `$reaped -gt 0` and released the lock either way, which is how a surviving engine kept the port
    # while the rig advertised itself free. Callers now get the survivors too, and are expected to
    # treat a non-empty list as a HARD failure rather than a warning.
    return [pscustomobject]@{
        Killed    = $killed
        Survivors = @($survivors.ToArray())
        Foreign   = @($split.Foreign)
    }
}

# Waits (bounded) for every pid to be gone. taskkill returns before the process object has finished going away, so a
# liveness check made straight after a kill can still see the victim; deciding "survivor" on that look is a false alarm.
function Wait-QaPidsGone {
    param([int[]]$Pids, [int]$TimeoutMs = $script:QaPidsGoneTimeoutMs)
    $deadline = (Get-Date).AddMilliseconds($TimeoutMs)
    while ((Test-QaAnyPidAlive -Pids @($Pids)) -and ((Get-Date) -lt $deadline)) { Start-Sleep -Milliseconds 100 }
    return (-not (Test-QaAnyPidAlive -Pids @($Pids)))
}

# ================================================================================================
# THE ADMISSION RECORD AND ITS STATE MACHINE (#1388)
#
# ONE file holds ALL of the rig's state - the lock record - and every change to it is a read-modify-write made WHILE HOLDING
# the machine-wide admission mutex and written as a whole file (temp file + atomic replace). So two writers can never lose
# each other's updates, a reader never sees a half-written record, and there is no second place (no side files) where part of
# the story could be lost, blocked, or overwritten by someone else's older writer.
#
#   record = { token, kind, pid, pidStartedUtc, worktree, branch, port, tokens[], runDir, takenUtc,   -- the OWNER (and its profiles)
#              state: active | closing | ended,                -- lifecycle
#              why[]: reasons, only ever added to ('port' makes the port part of the evidence),
#              carry[]: profiles of earlier owners that are not proven gone,
#              predecessors[]: earlier owners, a FLAT list (each: the owner fields above) - present while a takeover is not
#                              yet confirmed; never nested, de-duplicated by owner, refused beyond a fixed length,
#              rev }                                           -- bumped by every write
#   (an older-format record is just the owner fields: state defaults to active, the rest to empty.)
#
#   active --registration--> active (more profiles)
#   active --Close--> closing --all gone, record unchanged since checked--> (file deleted)
#                       \--not provably gone--> ended (why += reason)
#   active|ended --reclaim: host gone / run ended, and EVERYTHING it owns proven gone--> new owner, predecessors = [old owner, ...]
#   predecessors --confirming look clean--> active (predecessors dropped = admission granted)
#   predecessors --late engine / unknown--> the newest predecessor again, ended (reasons kept) = admission refused
#   predecessors, owner dead --> still names them all: the next contender needs ALL gone
#   predecessors, owner is THIS process --> resumed for the identical request only; any other request recovers it as a predecessor
#
# ONE absence check decides every "is it gone?": Test-QaRunGone(target, snapshot) -> Gone | Present | Unknown. A target is a
# list of PARTS, one per owner the record answers for (profiles, or - for an owner with none - its worktree; and the port where
# that owner is an ended run kept for 'port'); parts are never merged. Gone only on a successful query that finds no owned
# engine, no engine whose ownership cannot be decided, and a free port where it matters. Anything else is not Gone. A record
# that cannot be read, or an owner with no profiles and no worktree, is never "empty": any engine with any -profile counts.
# ================================================================================================

# --- the record: read, normalise, write ----------------------------------------------------------

# Read a small file without ever being the reason somebody else's replace or delete fails (share ReadWrite + Delete).
function Read-QaSharedText {
    param([string]$Path)
    $fs = New-Object System.IO.FileStream($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, ([System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete))
    try { return (New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)).ReadToEnd() } finally { $fs.Dispose() }
}

# Add reasons (comma lists allowed) to a reason list, keeping every one already there.
function Merge-QaWhy {
    param([string[]]$Existing, [string]$Add)
    $out = New-Object System.Collections.ArrayList
    foreach ($w in @(@($Existing) + @($Add -split ','))) {
        $t = ([string]$w).Trim()
        if (($t -ne '') -and ($out -notcontains $t)) { [void]$out.Add($t) }
    }
    return @($out.ToArray())
}

# Path lists compare ORDINALLY and case-insensitively - the NTFS rule the ownership matcher uses - never with PowerShell's
# culture-aware -eq / -contains / Sort-Object -Unique, which equate canonically equivalent Unicode spellings of DIFFERENT directories.
function Merge-QaPaths {
    param([string[]]$Existing, [string[]]$Add)
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    $out = New-Object System.Collections.ArrayList
    foreach ($p in @(@($Existing) + @($Add))) {
        if ((-not [string]::IsNullOrWhiteSpace($p)) -and $seen.Add([string]$p)) { [void]$out.Add([string]$p) }
    }
    return @($out.ToArray())
}

function Test-QaSamePathSet {
    param([string[]]$A, [string[]]$B)
    $set = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($x in @($A)) { if (-not [string]::IsNullOrWhiteSpace($x)) { [void]$set.Add([string]$x) } }
    $other = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($x in @($B)) { if (-not [string]::IsNullOrWhiteSpace($x)) { [void]$other.Add([string]$x) } }
    return $set.SetEquals($other)
}

# One owner as the one shape the rest of the code sees (the record's own fields; a predecessor entry is the same shape). An
# unknown lifecycle state is read as the STRICTEST one (ended, for process and port), never as "active".
function ConvertTo-QaRigOwner {
    param($Parsed)
    if (($null -eq $Parsed) -or -not ($Parsed -is [System.Management.Automation.PSCustomObject])) { return $null }
    $state = [string](Get-QaProp -Object $Parsed -Name 'state' -Default 'active')
    $why = @(Get-QaProp -Object $Parsed -Name 'why' -Default @() | ForEach-Object { [string]$_ })
    if (@('active', 'closing', 'ended') -notcontains $state) { $state = 'ended'; $why = Merge-QaWhy -Existing $why -Add 'process,port' }
    return [pscustomobject]@{
        token         = [string](Get-QaProp -Object $Parsed -Name 'token' -Default '')
        kind          = [string](Get-QaProp -Object $Parsed -Name 'kind' -Default '')
        pid           = Get-QaProp -Object $Parsed -Name 'pid' -Default 0
        pidStartedUtc = [string](Get-QaProp -Object $Parsed -Name 'pidStartedUtc' -Default '')
        worktree      = [string](Get-QaProp -Object $Parsed -Name 'worktree' -Default '')
        branch        = [string](Get-QaProp -Object $Parsed -Name 'branch' -Default '')
        port          = Get-QaProp -Object $Parsed -Name 'port' -Default 0
        tokens        = @(Get-QaProp -Object $Parsed -Name 'tokens' -Default @() | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) } | ForEach-Object { [string]$_ })
        runDir        = [string](Get-QaProp -Object $Parsed -Name 'runDir' -Default '')
        takenUtc      = [string](Get-QaProp -Object $Parsed -Name 'takenUtc' -Default '')
        state         = $state
        why           = @($why)
        carry         = @(Get-QaProp -Object $Parsed -Name 'carry' -Default @() | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) } | ForEach-Object { [string]$_ })
    }
}

# A parsed JSON record: the owner, plus predecessors[] (a FLAT list of earlier owners that are not yet proven gone) and rev.
# A nested `provisional` chain (the previous shape of this branch) is flattened, newest first.
function ConvertTo-QaRigRecord {
    param($Parsed)
    $rec = ConvertTo-QaRigOwner -Parsed $Parsed
    if ($null -eq $rec) { return $null }
    $rev = 0
    [void][int]::TryParse([string](Get-QaProp -Object $Parsed -Name 'rev' -Default 0), [ref]$rev)
    $preds = New-Object System.Collections.ArrayList
    $nested = Get-QaProp -Object $Parsed -Name 'provisional' -Default $null
    for ($n = 0; ($null -ne $nested) -and ($nested -is [System.Management.Automation.PSCustomObject]) -and ($n -lt 64); $n++) {
        [void]$preds.Add((ConvertTo-QaRigOwner -Parsed $nested))
        $nested = Get-QaProp -Object $nested -Name 'provisional' -Default $null
    }
    foreach ($p in @(Get-QaProp -Object $Parsed -Name 'predecessors' -Default @())) {
        $o = ConvertTo-QaRigOwner -Parsed $p
        if ($null -ne $o) { [void]$preds.Add($o) }
    }
    $rec | Add-Member -NotePropertyName predecessors -NotePropertyValue @($preds.ToArray())
    $rec | Add-Member -NotePropertyName rev -NotePropertyValue $rev
    return $rec
}

# The owner fields of a record, as a predecessor entry.
function ConvertTo-QaRigPredecessor {
    param($Rec)
    $e = [ordered]@{}
    foreach ($p in $Rec.PSObject.Properties) { if (@('predecessors', 'rev') -notcontains $p.Name) { $e[$p.Name] = $p.Value } }
    return [pscustomobject]$e
}

# One owner's identity for de-duplication: its token, or (an older record has none) its host.
function Get-QaOwnerKey {
    param($Owner)
    if ($Owner.token -ne '') { return [string]$Owner.token }
    return ('pid:' + [string]$Owner.pid + '|' + [string]$Owner.pidStartedUtc)
}

# The most unresolved predecessors one record will carry. Each is one flat entry, so the record's size - and the JSON depth -
# grow linearly and never nest; a chain past this is REFUSED (the takeover does not happen), never trimmed.
$script:QaRigMaxPredecessors = 32

# The predecessor list for a record that takes over `Existing`: it, then everything it still answered for, de-duplicated by
# owner (an owner seen twice keeps its first entry with the reasons and profiles of both). $null if the chain would be too long.
function Join-QaRigPredecessors {
    param($Existing)
    $out = New-Object System.Collections.ArrayList
    foreach ($e in @(@(ConvertTo-QaRigPredecessor -Rec $Existing) + @($Existing.predecessors))) {
        $dup = $null
        foreach ($o in $out) { if ((Get-QaOwnerKey -Owner $o) -eq (Get-QaOwnerKey -Owner $e)) { $dup = $o; break } }
        if ($null -eq $dup) { [void]$out.Add($e); continue }
        $dup.tokens = @(Merge-QaPaths -Existing @($dup.tokens) -Add @($e.tokens))
        $dup.carry = @(Merge-QaPaths -Existing @($dup.carry) -Add @($e.carry))
        $dup.why = @(Merge-QaWhy -Existing @($dup.why) -Add (@($e.why) -join ','))
        if ($e.state -eq 'ended') { $dup.state = 'ended' }
    }
    if ($out.Count -gt $script:QaRigMaxPredecessors) { return $null }
    return @($out.ToArray())
}

# The lock file as it is NOW. State: 'absent', 'ok' (parsed and complete: it names a positive pid and a pidStartedUtc), or
# 'unreadable' (empty, torn, unparsable, not an object, or valid JSON that lacks the ownership identity: a positive pid and a start time); AgeSec is how long ago
# an unreadable file was last written. Incomplete-but-valid JSON is deliberately 'unreadable'. An unreadable record is NEVER
# read as an empty one: nothing may be reclaimed from it without evidence (see Invoke-QaMpRigLockAttempt).
function Read-QaMpRigLockState {
    $path = Get-QaMpRigLockPath
    if (-not (Test-Path -LiteralPath $path)) { return [pscustomobject]@{ State = 'absent'; Lock = $null; AgeSec = 0 } }
    $age = 0
    try { $age = [int]((Get-Date).ToUniversalTime() - (Get-Item -LiteralPath $path -ErrorAction Stop).LastWriteTimeUtc).TotalSeconds } catch { $age = 0 }
    $lock = $null
    try {
        $raw = Read-QaSharedText -Path $path
        if (-not [string]::IsNullOrWhiteSpace($raw)) {
            $parsed = $raw | ConvertFrom-Json
            $pidValue = 0
            if (($null -ne $parsed) -and ($parsed -is [System.Management.Automation.PSCustomObject]) -and
                ($null -ne $parsed.PSObject.Properties['pid']) -and ($null -ne $parsed.PSObject.Properties['pidStartedUtc']) -and
                [int]::TryParse([string]$parsed.pid, [ref]$pidValue) -and ($pidValue -gt 0) -and
                (-not [string]::IsNullOrWhiteSpace([string]$parsed.pidStartedUtc))) {
                $lock = ConvertTo-QaRigRecord -Parsed $parsed
            }
        }
    } catch { $lock = $null }
    if ($null -eq $lock) { return [pscustomobject]@{ State = 'unreadable'; Lock = $null; AgeSec = $age } }
    return [pscustomobject]@{ State = 'ok'; Lock = $lock; AgeSec = 0 }
}

function Read-QaMpRigLock {
    $s = Read-QaMpRigLockState
    if ($s.State -eq 'ok') { return $s.Lock }
    return $null
}

# A mutable copy of a record (ordered hashtable), to be changed and turned back into a record with [pscustomobject].
function Copy-QaMpLockRecord {
    param($Lock)
    $copy = [ordered]@{}
    foreach ($p in $Lock.PSObject.Properties) { $copy[$p.Name] = $p.Value }
    return $copy
}

# Replace the lock file with $Record in one step (temp file + replace), retried a few times. Throws if it still cannot: a write
# that did not happen is never reported as one. Only Edit-QaRigRecord calls this for a live change.
function Write-QaMpRigLockRecord {
    param($Record, [switch]$CreateOnly)
    $path = Get-QaMpRigLockPath
    $dir = Split-Path -Parent $path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) { [void](New-Item -ItemType Directory -Path $dir -Force) }
    $body = $Record | ConvertTo-Json -Compress -Depth 12
    $last = $null
    for ($try = 1; $try -le $script:QaRigWriteTries; $try++) {
        $tmp = $path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
        try {
            [System.IO.File]::WriteAllText($tmp, $body, (New-Object System.Text.UTF8Encoding($false)))
            if ((-not $CreateOnly) -and (Test-Path -LiteralPath $path)) { [System.IO.File]::Replace($tmp, $path, [NullString]::Value) } else { [System.IO.File]::Move($tmp, $path) }   # Move never overwrites
            return
        } catch {
            $last = $_.Exception.Message
            if ($try -lt $script:QaRigWriteTries) { Start-Sleep -Milliseconds $script:QaRigWriteDelayMs }
        } finally {
            Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
        }
    }
    throw ('the lock file could not be written: ' + $last)
}

# THE way to change the record. Under the machine-wide mutex it reads the record, hands it to $Mutate, and - if $Mutate returns
# something to save - writes it (revision bumped). $Mutate receives the state (State/Lock/AgeSec) and returns
# New-QaRigEdit -Save <record or $null> -Result <anything>. Throws if the mutex cannot be entered or the write fails.
function New-QaRigEdit {
    param($Save = $null, $Result = $null, [switch]$CreateOnly)
    return [pscustomobject]@{ Save = $Save; Result = $Result; CreateOnly = [bool]$CreateOnly }
}

function Edit-QaRigRecord {
    param([Parameter(Mandatory = $true)][scriptblock]$Mutate, [int]$WaitMs = $script:QaRigAdmissionWaitMs)
    return (Invoke-QaRigAdmissionCritical -WaitMs $WaitMs -Body {
        $state = Read-QaMpRigLockState
        $edit = & $Mutate $state
        if (($null -ne $edit) -and ($null -ne $edit.Save)) {
            $rev = 0
            if ($state.State -eq 'ok') { $rev = [int]$state.Lock.rev }
            $edit.Save | Add-Member -NotePropertyName rev -NotePropertyValue ($rev + 1) -Force
            Write-QaMpRigLockRecord -Record $edit.Save -CreateOnly:$edit.CreateOnly
        }
        if ($null -eq $edit) { return $null }
        return $edit.Result
    })
}

# What identifies a record version: its owner and its revision. Changes with EVERY write, so a decision made on one version
# can be recognised as stale (profiles registered, state changed, taken over - all bump it).
function Get-QaMpLockKey {
    param($Lock)
    if ($null -eq $Lock) { return '' }
    return ([string]$Lock.token + '|' + [string]$Lock.pid + '|' + [string]$Lock.pidStartedUtc + '|' + [string]$Lock.rev)
}

# --- what a record OWNS, and the one absence check ----------------------------------------------

# What must be gone for a rig to be free is a list of PARTS, one per owner the record answers for (its own and every
# predecessor's), plus the two conservative switches. A part is what ONE owner stands for:
#   Profiles - engines started with one of them (or under it): the only evidence that PROVES an engine is the owner's
#   Worktree - for an owner with no profiles at all (an older record): the engines of that worktree - a possible owner only
#   Port     - a port that must also be free, when that owner is an ended run kept for 'port'. A part may carry nothing else:
#              the port is a constraint of its own, whatever the engine scope is.
# Parts are never merged: one owner's fields can neither replace nor suppress another's.
#   Anything - some owner has no profiles and no worktree (or the record cannot be read): its scope is unknown, so ANY engine
#              with ANY -profile is a possible owner. That BLOCKS (Present); it never authorises stopping anything.
#   Unknown  - no target could be formed at all: not Gone
function New-QaTargetPart {
    param([string[]]$Profiles = @(), [string]$Worktree = '', [int]$Port = 0)
    return [pscustomobject]@{ Profiles = @($Profiles); Worktree = $Worktree; Port = $Port }
}

function New-QaRunTarget {
    param([string[]]$Profiles = @(), [string]$Worktree = '', [int]$Port = 0, [switch]$Anything, [switch]$Unknown)
    $parts = @()
    if ((@($Profiles).Count -gt 0) -or ($Worktree -ne '') -or ($Port -gt 0)) { $parts = @(New-QaTargetPart -Profiles $Profiles -Worktree $Worktree -Port $Port) }
    return [pscustomobject]@{ Parts = @($parts); Anything = [bool]$Anything; Unknown = [bool]$Unknown }
}

# The target of a whole record: its own owner and every predecessor, each as its own part.
function Get-QaRunTarget {
    param($Rec)
    if ($null -eq $Rec) { return (New-QaRunTarget -Unknown) }
    $parts = New-Object System.Collections.ArrayList
    $anything = $false
    foreach ($o in @(@($Rec) + @($Rec.predecessors))) {
        $profiles = @(Merge-QaPaths -Existing @($o.tokens) -Add @($o.carry))
        $port = 0
        if (($o.state -eq 'ended') -and (@($o.why) -contains 'port')) { try { $port = [int]$o.port } catch { $port = 0 } }
        if ($profiles.Count -gt 0) { [void]$parts.Add((New-QaTargetPart -Profiles $profiles -Port $port)) }
        elseif ($o.worktree -ne '') { [void]$parts.Add((New-QaTargetPart -Worktree $o.worktree -Port $port)) }
        else {
            $anything = $true
            if ($port -gt 0) { [void]$parts.Add((New-QaTargetPart -Port $port)) }   # the port outlives an unknown engine scope
        }
    }
    return [pscustomobject]@{ Parts = @($parts.ToArray()); Anything = $anything; Unknown = $false }
}

# One-line description of a process that stands in the way, for refusal messages: PID, image, command-line excerpt.
function New-QaBlocker {
    param($Process, [string]$Why, [switch]$Killable)
    $cmd = [string]$Process.CommandLine
    if ($cmd.Length -gt 160) { $cmd = $cmd.Substring(0, 160) + '...' }
    return [pscustomobject]@{ ProcessId = [int]$Process.ProcessId; Name = [string]$Process.Name; Command = $cmd; Why = $Why; Killable = [bool]$Killable }
}

function Format-QaBlockers {
    param($Blockers)
    $lines = @(@($Blockers) | Where-Object { $null -ne $_ } | ForEach-Object { $(if ($_.ProcessId -gt 0) { 'PID ' + $_.ProcessId + ' ' } else { '' }) + $_.Name + ' [' + $_.Why + ']' + $(if ($_.Command) { ' ' + $_.Command } else { '' }) })
    if ($lines.Count -eq 0) { return '' }
    return ('blocked by: ' + ($lines -join ' ; '))
}

# The reason and the blockers of a refusal (an admission result), for EVERY message that reports one.
function Format-QaRefusal {
    param($Result)
    if ($null -eq $Result) { return '' }
    return ('[' + [string](Get-QaProp -Object $Result -Name 'Reason' -Default 'refused') + '] ' + (Format-QaBlockers -Blockers (Get-QaProp -Object $Result -Name 'Blockers' -Default @()))).TrimEnd()
}

# THE absence check. One look at a snapshot: is everything the target stands for gone?
#   Present - an owned or possibly-owned engine is alive, or a port that must be free is not (Blockers name them)
#   Unknown - the query failed, the target is unknown, or an engine whose ownership cannot be decided (a relative -profile) is
#             running (Blockers name it): absence cannot be shown
#   Gone    - none of the above
# Present wins over Unknown. Used by the reclaim looks, the takeover confirmation, release, retained reclaim, and orphan reaping.
# BLOCKING EVIDENCE IS NOT KILL AUTHORITY, and the result keeps them apart:
#   Engines  - the engines PROVEN to belong to the target: started with one of its exact profiles (or under it). The only
#              processes anything may stop.
#   Blockers - everything that stands in the way, each with Killable (true only for those same engines). A possible owner
#              (a scope-less owner's any-profile match, a worktree-only match), an undecidable engine and a busy port are
#              Killable = false: they keep the lock held and are named, never stopped.
function Test-QaRunGone {
    param($Target, $Snapshot)
    $result = { param($v, $engines, $blockers) return [pscustomobject]@{ Verdict = $v; Engines = @($engines); Blockers = @($blockers) } }
    if (($null -eq $Target) -or $Target.Unknown) { return (& $result 'Unknown' @() @()) }
    if (($null -eq $Snapshot) -or -not $Snapshot.Ok) { return (& $result 'Unknown' @() @()) }
    $proven = @()
    $possible = @()
    if ($Target.Anything) {
        $possible += @($Snapshot.Processes | Where-Object { $_ -and $_.CommandLine -and (@(Get-QaCommandLineProfileValues -CommandLine $_.CommandLine).Count -gt 0) })
    }
    foreach ($part in @($Target.Parts)) {
        if (@($part.Profiles).Count -gt 0) {
            $proven += @(Select-QaSnapshotByToken -Snapshot $Snapshot -Tokens @($part.Profiles))
        } elseif ($part.Worktree -ne '') {
            foreach ($p in @($Snapshot.Processes)) {
                if ($null -eq $p -or -not $p.CommandLine) { continue }
                $isEngine = $false
                foreach ($prefix in @(Get-QaEngineNamePrefixes)) { if ($p.Name -like ($prefix + '*')) { $isEngine = $true } }
                if ($isEngine -and ((Get-QaProcessWorktree -CommandLine $p.CommandLine) -eq $part.Worktree)) { $possible += $p }
            }
        }
    }
    $seenPids = @{}
    $proven = @($proven | Where-Object { $k = [string]$_.ProcessId; if ($seenPids.ContainsKey($k)) { $false } else { $seenPids[$k] = $true; $true } })
    $possible = @($possible | Where-Object { $k = [string]$_.ProcessId; if ($seenPids.ContainsKey($k)) { $false } else { $seenPids[$k] = $true; $true } })
    if (($proven.Count -gt 0) -or ($possible.Count -gt 0)) {
        $blockers = @($proven | ForEach-Object { New-QaBlocker -Process $_ -Why 'owned engine' -Killable }) +
                    @($possible | ForEach-Object { New-QaBlocker -Process $_ -Why 'may belong to this record (its scope is unknown or only a worktree): held, not stopped' })
        return (& $result 'Present' $proven $blockers)
    }
    $undecided = @(Get-QaSnapshotUndecided -Snapshot $Snapshot)
    if ($undecided.Count -gt 0) { return (& $result 'Unknown' @() @($undecided | ForEach-Object { New-QaBlocker -Process $_ -Why 'ownership cannot be decided: relative -profile' })) }
    # EVERY port any part requires must be free (not just one of them).
    $ports = @($Target.Parts | Where-Object { $_.Port -gt 0 } | ForEach-Object { [int]$_.Port } | Sort-Object -Unique)
    $holders = @()
    foreach ($port in $ports) {
        if (-not (Test-QaMpPortFree -Port $port)) {
            $h = @(Get-QaMpPortHolders -Port $port | ForEach-Object { [pscustomobject]@{ ProcessId = [int]$_; Name = '(holds UDP ' + $port + ')'; Command = ''; Why = 'port'; Killable = $false } })
            if ($h.Count -eq 0) { $h = @([pscustomobject]@{ ProcessId = 0; Name = '(unknown holder of UDP ' + $port + ')'; Command = ''; Why = 'port'; Killable = $false }) }
            $holders += $h
        }
    }
    if ($holders.Count -gt 0) { return (& $result 'Present' @() $holders) }
    return (& $result 'Gone' @() @())
}

# Gone must be seen TWICE, SettleSec apart, on fresh successful queries: one empty look can fall in the gap while Steam starts
# the real engine from its bootstrap seed. Anything but Gone on either look is the answer.
function Test-QaRunGoneSettled {
    param($Target, [double]$SettleSec = $script:QaRigSettleSec)
    $v = Test-QaRunGone -Target $Target -Snapshot (Get-QaEngineSnapshot)
    if ($v.Verdict -ne 'Gone') { return $v }
    if ($SettleSec -gt 0) { Start-Sleep -Milliseconds ([int]($SettleSec * 1000)) }
    return (Test-QaRunGone -Target $Target -Snapshot (Get-QaEngineSnapshot))
}

# Stops the engines a target owns and shows they are gone: kill what the settled check finds, look again, until it says Gone (or
# says it cannot, or the passes run out). The kill list is re-found on every pass - never a PID array from before: the seed PID
# Steam hands back is not the engine's, and a wait that was interrupted never got to assign anything. Undecidable engines are
# never killed. -ScanOnly checks without stopping anything.
#   Gone      - shown gone
#   Found     - how many distinct engine processes were seen
#   Survivors - pids still alive when it gave up (the caller must not release a lock over them)
#   Unknown   - absence could not be shown (query failed / undecidable engine); Blockers say what stands in the way
function Stop-QaRunEngines {
    param($Target, [int]$GraceSec = 10, [double]$SettleSec = $script:QaRigSettleSec, [int]$MaxPasses = 6, [switch]$ScanOnly)
    $result = [pscustomobject]@{ Gone = $false; Found = 0; Survivors = @(); Unknown = $false; Blockers = @() }
    $seen = @{}
    $stuck = 0
    $first = $true
    for ($pass = 1; $pass -le $MaxPasses; $pass++) {
        $v = Test-QaRunGoneSettled -Target $Target -SettleSec $SettleSec
        $result.Blockers = @($v.Blockers)
        if ($v.Verdict -eq 'Gone') { $result.Gone = $true; $result.Survivors = @(); return $result }
        if ($v.Verdict -eq 'Unknown') { $result.Unknown = $true; return $result }
        $pids = @($v.Engines | ForEach-Object { [int]$_.ProcessId } | Where-Object { $_ -gt 0 })
        foreach ($p in $pids) { $seen[[string]$p] = $true }
        $result.Found = $seen.Count
        $result.Survivors = $pids
        if ($ScanOnly -or ($pids.Count -eq 0)) { return $result }   # nothing killable (a busy port, say)
        [void](Stop-QaWorkbench -Pids $pids -GraceSec $(if ($first) { $GraceSec } else { 0 }))
        $first = $false
        if (-not (Wait-QaPidsGone -Pids $pids)) {
            $stuck++
            if ($stuck -ge 2) { $result.Survivors = @($pids | Where-Object { Test-QaPidAlive -ProcessId $_ }); return $result }
        }
    }
    $result.Survivors = @($result.Survivors | Where-Object { Test-QaPidAlive -ProcessId $_ })
    if ($result.Survivors.Count -eq 0) { $result.Unknown = $true }   # the passes ran out without ever seeing Gone
    return $result
}

# The same, for plain profile paths (qa-run / soak-run cleanup when no admission was taken).
function Stop-QaOwnedProcesses {
    param([string[]]$Tokens, [int]$GraceSec = 10, [string]$NamePrefix = 'ArmaReforger', [double]$SettleSec = $script:QaRigSettleSec, [int]$MaxScans = 6, [switch]$ScanOnly)
    $list = @(ConvertTo-QaTokenList -Tokens $Tokens)
    if ($list.Count -eq 0) { return [pscustomobject]@{ Gone = $true; Found = 0; Survivors = @(); Unknown = $false; Blockers = @() } }
    return (Stop-QaRunEngines -Target (New-QaRunTarget -Profiles $list) -GraceSec $GraceSec -SettleSec $SettleSec -MaxPasses $MaxScans -ScanOnly:$ScanOnly)
}

# --- leftovers of the previous state format ------------------------------------------------------

# An earlier revision of this branch kept an ended run's state in side files next to the lock: <lock>.<token>.end and
# <lock>.<token>.restore. That format never shipped and is NOT migrated: whatever those files say (a retained port, a carried
# profile, a predecessor) cannot be reconstructed safely from the files alone. If any exist, admission refuses ('legacy') and
# names each one; an operator verifies nothing of this rig is running and deletes them.
function Get-QaLegacySideFiles {
    $lock = Get-QaMpRigLockPath
    $dir = Split-Path -Parent $lock
    if ((-not $dir) -or (-not (Test-Path -LiteralPath $dir))) { return @() }
    return @(Get-ChildItem -LiteralPath $dir -Filter ((Split-Path -Leaf $lock) + '.*') -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '\.[0-9a-fA-F]+\.(end|restore)$' } | ForEach-Object { $_.FullName })
}

function Get-QaLegacyBlockers {
    param([string[]]$Files)
    $b = @($Files | ForEach-Object { [pscustomobject]@{ ProcessId = 0; Name = $_; Command = ''; Why = 'legacy rig side files present'; Killable = $false } })
    $b += [pscustomobject]@{ ProcessId = 0; Name = '(how to clear)'; Command = 'confirm no Arma Reforger / Workbench process of this rig is running and UDP 2001 is free, then delete the file(s) above and retry'; Why = 'operator action'; Killable = $false }
    return $b
}

# --- acquiring -----------------------------------------------------------------------------------

# Is this a takeover THIS process started and could not finish (confirmation neither committed nor backed out)?
function Test-QaOwnUnconfirmed {
    param($Rec)
    return ((@($Rec.predecessors).Count -gt 0) -and (([int]$Rec.pid) -eq $PID) -and ($Rec.pidStartedUtc -eq (Get-QaMpLockIdentity)))
}

# Is `Rec` an earlier attempt at exactly THIS request? (same kind, worktree, run directory and profiles)
function Test-QaSameRequest {
    param($Rec, [string]$Kind, [string]$WorktreePath, [string[]]$Tokens, [string]$RunDir)
    if (($Rec.kind -ne $Kind) -or ($Rec.runDir -ne $RunDir) -or ($Rec.worktree -ne $WorktreePath)) { return $false }
    return (Test-QaSamePathSet -A @($Rec.tokens) -B @($Tokens))
}

# One attempt. Anything slow - the settled looks at the engines - happens OUTSIDE the admission mutex; inside it the record is
# re-read and compared (its version must be the one the looks were taken for) and then replaced in one write.
#   A reclaim is PROVISIONAL: the new owner's record carries the predecessor (and everything the predecessor still answered
#   for) as a FLAT predecessors[] list, so a contender that dies before confirming leaves a record that still names them all.
#   Confirm-QaMpTakeover then takes one more look and either commits (predecessors dropped) or restores the predecessor,
#   ended. Taken = true leaves here only after a commit.
#   A takeover this process left unfinished is RESUMED only for the identical request; any other request recovers it like any
#   other unconfirmed record (as a predecessor) and acquires a record of its own.
#   Reason: taken | live | orphan | unknown | nested | legacy | overflow | initialising | rescan | race | busy
function Invoke-QaMpRigLockAttempt {
    param([string]$WorktreePath, [string]$Branch, [int]$Port, [bool]$Force, [string]$Kind, [string[]]$Tokens, [string]$RunDir)
    $envToken = [string]$env:MRS_QA_ADMISSION_TOKEN
    $newTokens = @(ConvertTo-QaTokenList -Tokens $Tokens)

    $legacyFiles = @(Get-QaLegacySideFiles)
    if ($legacyFiles.Count -gt 0) { return [pscustomobject]@{ Taken = $false; Holder = $null; Reason = 'legacy'; Token = ''; Blockers = @(Get-QaLegacyBlockers -Files $legacyFiles) } }

    $pre = Read-QaMpRigLockState
    $verdict = $null
    $preKey = ''
    if (-not $Force) {
        if ($pre.State -eq 'ok') {
            $rec = $pre.Lock
            $preKey = Get-QaMpLockKey -Lock $rec
            $ownUnconfirmed = Test-QaOwnUnconfirmed -Rec $rec
            if ($ownUnconfirmed -and ($rec.state -eq 'active') -and (Test-QaSameRequest -Rec $rec -Kind $Kind -WorktreePath $WorktreePath -Tokens $newTokens -RunDir $RunDir)) {
                return (Confirm-QaMpTakeover -Token $rec.token)   # the same request, resumed
            }
            if ($ownUnconfirmed -or ($rec.state -eq 'ended') -or (-not (Test-QaMpLockHolderAlive -Lock $rec))) { $verdict = Test-QaRunGoneSettled -Target (Get-QaRunTarget -Rec $rec) }
        } elseif (($pre.State -eq 'unreadable') -and ($pre.AgeSec -ge $script:QaRigInitWindowSec)) {
            $verdict = Test-QaRunGoneSettled -Target (New-QaRunTarget -Anything)   # a record we cannot read is not an empty one
        }
    }

    try {
        $r = Edit-QaRigRecord -Mutate {
            param($state)
            $predecessors = @()
            $refuse = { param($reason, $holder, $blockers) return (New-QaRigEdit -Result ([pscustomobject]@{ Taken = $false; Holder = $holder; Reason = $reason; Token = ''; Blockers = @($blockers) })) }
            if ($state.State -eq 'unreadable') {
                # May be mid-write by a live owner: left alone for the initialisation window; after it, only removed on evidence.
                if ($state.AgeSec -lt $script:QaRigInitWindowSec) { return (& $refuse 'initialising' $null @()) }
                if ($null -eq $verdict) { return (& $refuse 'rescan' $null @()) }
                if ($verdict.Verdict -ne 'Gone') { return (& $refuse $(if ($verdict.Verdict -eq 'Present') { 'orphan' } else { 'unknown' }) $null $verdict.Blockers) }
                Write-QaWarn ('Rig lock: discarding an unreadable or incomplete lock file (last written ' + $state.AgeSec + 's ago, past the ' + $script:QaRigInitWindowSec + 's initialisation window) - no engine with a profile is running.')
                Remove-Item -LiteralPath (Get-QaMpRigLockPath) -Force -ErrorAction Stop
            } elseif ($state.State -eq 'ok') {
                $existing = $state.Lock
                $alive = Test-QaMpLockHolderAlive -Lock $existing
                $holderPid = 0
                try { $holderPid = [int]$existing.pid } catch { $holderPid = 0 }
                $legacy = ($existing.token -eq '')
                $ownUnconfirmed = Test-QaOwnUnconfirmed -Rec $existing
                # NO inheritance of a heavyweight admission: a token proves ownership for the run that took it and for the
                # compile-only work it starts. A heavyweight child of the owner is refused at once.
                if ($envToken -and ($existing.token -eq $envToken) -and $alive -and ($existing.state -eq 'active') -and ($holderPid -ne $PID)) { return (& $refuse 'nested' $existing @()) }
                if (-not $Force) {
                    if (($existing.state -ne 'ended') -and $alive -and (-not $ownUnconfirmed)) {
                        if ($legacy -and (-not $script:QaLegacyWarned)) {
                            $script:QaLegacyWarned = $true
                            Write-QaWarn ('Rig lock: the live holder (PID ' + $holderPid + ') was written by an OLDER copy of these scripts (no ownership token). Respecting it by pid and start time, but that copy does not use this protocol: its lock handling is not serialised with ours. Every checkout that runs rigs must be updated.')
                        }
                        return (& $refuse 'live' $existing @())
                    }
                    if (($null -eq $verdict) -or ((Get-QaMpLockKey -Lock $existing) -ne $preKey)) { return (& $refuse 'rescan' $null @()) }
                    if ($verdict.Verdict -eq 'Unknown') { return (& $refuse 'unknown' $existing $verdict.Blockers) }
                    if ($verdict.Verdict -eq 'Present') { return (& $refuse 'orphan' $existing $verdict.Blockers) }
                    $predecessors = Join-QaRigPredecessors -Existing $existing
                    if ($null -eq $predecessors) {
                        return (& $refuse 'overflow' $existing @([pscustomobject]@{ ProcessId = 0; Name = '(the lock record)'; Command = ('more than ' + $script:QaRigMaxPredecessors + ' earlier owners are still unresolved - this does not clear when their engines exit. Confirm nothing of this rig is running and UDP 2001 is free, then delete ' + (Get-QaMpRigLockPath)); Why = 'too many predecessors'; Killable = $false }))
                    }
                }
                $why = 'STALE (holder pid/start-time no longer matches - crashed or pid recycled)'
                if ($existing.state -eq 'ended') { $why = 'STALE (the run that kept it ended and everything it owned is gone)' }
                if ($ownUnconfirmed) { $why = 'an unconfirmed takeover of this same process, for a different request' }
                if ($Force) { $why = 'FORCED OVER a live holder by -ForceRigLock' }
                Write-QaWarn ('MP rig lock: taking over a lock held by PID ' + $holderPid + ' (' + $existing.branch + ', ' + $existing.worktree + ') - ' + $why + '.')
            }
            $token = [guid]::NewGuid().ToString('N')
            $record = [pscustomobject]@{
                token = $token; kind = $Kind; pid = $PID; pidStartedUtc = (Get-QaMpLockIdentity); worktree = $WorktreePath; branch = $Branch
                port = $Port; tokens = @($newTokens); runDir = $RunDir; takenUtc = (Get-Date).ToUniversalTime().ToString('o')
                state = 'active'; why = @(); carry = @(); predecessors = @($predecessors); rev = 0
            }
            return (New-QaRigEdit -Save $record -CreateOnly:((@($predecessors).Count -eq 0) -and $state.State -ne 'ok') -Result ([pscustomobject]@{ Taken = $true; Holder = $null; Reason = 'taken'; Token = $token; Blockers = @(); Provisional = (@($predecessors).Count -gt 0) }))
        }
    } catch {
        Write-QaWarn ('Rig lock: could not decide ownership (' + $_.Exception.Message + '); not taking it.')
        return [pscustomobject]@{ Taken = $false; Holder = $null; Reason = 'busy'; Token = ''; Blockers = @() }
    }
    if ($r.Taken -and $r.Provisional) { return (Confirm-QaMpTakeover -Token $r.Token) }
    return $r
}

# Step 3 of a reclaim, on a record that is PROVISIONAL (it carries its predecessors): one more look at everything the record
# answers for - its own profiles and every predecessor's, each owner on its own terms. Only an explicit commit grants admission.
#   Gone    -> commit: predecessors are dropped
#   Present / Unknown -> BACK OUT: the newest predecessor's record is written back, ended, with the reason added to what it had
#             (the older ones stay listed behind it)
# Either outcome is written only if the record is STILL the version (revision and lifecycle state) the look was taken for; if
# it changed meanwhile (a reason or profile was added, the run ended) the look is taken again, a bounded number of times.
#   the record is no longer ours, or the write keeps failing -> REFUSED; the record on disk is unchanged, so it still names the
#             predecessors (and this process can resume the confirmation for the same request, or a later contender can recover it)
function Confirm-QaMpTakeover {
    param([string]$Token)
    $outcome = 'failed'
    $rec = $null
    $look = $null
    for ($round = 1; $round -le 3; $round++) {
        $rec = Read-QaMpRigLock
        if (($null -eq $rec) -or ($rec.token -ne $Token) -or (@($rec.predecessors).Count -eq 0)) {
            return [pscustomobject]@{ Taken = $false; Holder = $rec; Reason = 'busy'; Token = ''; Blockers = @() }
        }
        $basedRev = [int]$rec.rev
        $basedState = [string]$rec.state
        $look = Test-QaRunGone -Target (Get-QaRunTarget -Rec $rec) -Snapshot (Get-QaEngineSnapshot)
        $whyBack = 'late engine'
        if ($look.Verdict -eq 'Unknown') { $whyBack = 'unconfirmed takeover: ownership or the process query was unknown' }
        $outcome = 'failed'
        for ($try = 1; ($try -le 3) -and ($outcome -eq 'failed'); $try++) {
            try {
                $outcome = [string](Edit-QaRigRecord -Mutate {
                    param($s)
                    if (($s.State -ne 'ok') -or ($s.Lock.token -ne $Token) -or (@($s.Lock.predecessors).Count -eq 0)) { return (New-QaRigEdit -Result 'superseded') }
                    if (([int]$s.Lock.rev -ne $basedRev) -or ([string]$s.Lock.state -ne $basedState)) { return (New-QaRigEdit -Result 'changed') }
                    if ($look.Verdict -eq 'Gone') {
                        $h = Copy-QaMpLockRecord -Lock $s.Lock
                        $h['predecessors'] = @()
                        return (New-QaRigEdit -Save ([pscustomobject]$h) -Result 'committed')
                    }
                    $newest = @($s.Lock.predecessors)[0]
                    $h = Copy-QaMpLockRecord -Lock $newest
                    $h['state'] = 'ended'
                    $h['why'] = @(Merge-QaWhy -Existing @($newest.why) -Add $whyBack)
                    $h['predecessors'] = @(@($s.Lock.predecessors) | Select-Object -Skip 1)
                    return (New-QaRigEdit -Save ([pscustomobject]$h) -Result 'backed-out')
                })
            } catch {
                $outcome = 'failed'
                if ($try -lt 3) { Start-Sleep -Milliseconds $script:QaRigWriteDelayMs }
            }
        }
        if ($outcome -ne 'changed') { break }
    }
    if ($outcome -eq 'committed') { return [pscustomobject]@{ Taken = $true; Holder = $null; Reason = 'taken'; Token = $Token; Blockers = @() } }
    if ($outcome -eq 'backed-out') {
        $reason = 'orphan'
        if ($look.Verdict -eq 'Unknown') { $reason = 'unknown' }
        return [pscustomobject]@{ Taken = $false; Holder = @($rec.predecessors)[0]; Reason = $reason; Token = ''; Blockers = @($look.Blockers) }
    }
    if ($outcome -eq 'failed') { Write-QaWarn 'Rig lock: a takeover could not be confirmed or backed out; refusing. The lock still names the predecessors, so nothing is lost.' }
    $why = 'unknown'
    if (($outcome -eq 'superseded') -or ($outcome -eq 'changed')) { $why = 'busy' }
    return [pscustomobject]@{ Taken = $false; Holder = @($rec.predecessors)[0]; Reason = $why; Token = ''; Blockers = @($look.Blockers) }
}

# Take the lock, or explain who holds it. On success the token is exported for this process (and, through the environment, for
# the compile-only children it starts) until Close-QaRigAdmission releases it.
function New-QaMpRigLock {
    param(
        [string]$WorktreePath,
        [string]$Branch = '',
        [int]$Port = 2001,
        [switch]$Force,
        # What is being admitted ('mp', 'persist', 'autotest', 'soak') and the unique profile paths its engines will carry.
        [string]$Kind = 'mp',
        [string[]]$Tokens = @(),
        [string]$RunDir = ''
    )
    $started = Get-Date
    $rescans = 0
    while ($true) {
        $r = Invoke-QaMpRigLockAttempt -WorktreePath $WorktreePath -Branch $Branch -Port $Port -Force ([bool]$Force) -Kind $Kind -Tokens $Tokens -RunDir $RunDir
        if ($r.Reason -eq 'initialising' -and ((Get-Date) - $started).TotalSeconds -lt ($script:QaRigInitWindowSec + 15)) {
            Start-Sleep -Seconds 1
            continue
        }
        if ($r.Reason -eq 'rescan' -and $rescans -lt 5) { $rescans++; continue }
        if ($r.Taken) { $env:MRS_QA_ADMISSION_TOKEN = $r.Token }
        return $r
    }
}

# Refusals that waiting will not clear: an orphan (engines still alive), a nested request, leftovers of the previous state
# format and an over-long chain of unresolved owners each need someone to act.
function Test-QaRefusalFinal {
    param([string]$Reason)
    return (@('orphan', 'nested', 'legacy', 'overflow') -contains $Reason)
}

# What to do about a refusal, in words. A final refusal (Test-QaRefusalFinal) is never "wait" and never "force": waiting does not
# clear it and -ForceRigLock does not apply to it, so each says what blocks and who has to act. Anything else is a live holder.
function Get-QaRefusalAdvice {
    param([string]$Reason, [string]$WaitAdvice = 'Wait for it to finish, or pass -ForceRigLock if you have confirmed it is dead.')
    switch ($Reason) {
        'orphan'   { return 'An earlier run''s engines are still running with nobody to stop them. Waiting will not clear this and nothing was killed: stop the engines named below yourself (or let their owner finish), then run again.' }
        'nested'   { return 'This run was started from inside another heavyweight run on this rig. Waiting will not clear this: run it after that run ends instead.' }
        'legacy'   { return 'Lock state from an older version of this tooling is still on disk. Waiting will not clear this: check that no run is using the files named below, remove them, then run again.' }
        'overflow' { return 'Too many earlier owners of the rig are unresolved. Waiting will not clear this: resolve the runs named below (stop or finish them), then run again.' }
    }
    return $WaitAdvice
}

# Wait for a live holder instead of refusing (the queue semantics qa-run and soak-run share). Holders that will not clear on
# their own - an ORPHAN (engines still alive) and a NESTED request - are refused at once.
function Enter-QaHeavyAdmission {
    param(
        [Parameter(Mandatory = $true)][string]$Kind,
        [string]$WorktreePath,
        [string]$Branch = '',
        [string[]]$Tokens = @(),
        [string]$RunDir = '',
        [int]$TimeoutSec = 14400,
        [int]$PollSec = 10,
        [switch]$NoWait
    )
    $r = New-QaMpRigLock -WorktreePath $WorktreePath -Branch $Branch -Port 2001 -Kind $Kind -Tokens $Tokens -RunDir $RunDir
    if ($r.Taken -or $NoWait -or (Test-QaRefusalFinal -Reason $r.Reason)) { return $r }
    $who = 'another run'
    if ($r.Holder) { $who = 'PID ' + $r.Holder.pid + ' (' + $(if ($r.Holder.kind) { $r.Holder.kind } else { 'run' }) + ', ' + $r.Holder.worktree + ')' }
    Write-QaStep ('Heavyweight admission: the rig is held by ' + $who + ' ' + (Format-QaRefusal -Result $r) + ' - waiting up to ' + $TimeoutSec + 's')
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((-not $r.Taken) -and (-not (Test-QaRefusalFinal -Reason $r.Reason)) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Seconds $PollSec
        $r = New-QaMpRigLock -WorktreePath $WorktreePath -Branch $Branch -Port 2001 -Kind $Kind -Tokens $Tokens -RunDir $RunDir
    }
    return $r
}

# --- registering, ending, releasing --------------------------------------------------------------

# Register more engine profiles under the lock this run holds, BEFORE launching them (a reused server's later clients, for
# one). The record must always name every engine the run starts: if the host dies, a contender decides from the record alone.
# Registered: registered | absent (no lock) | foreign (held by another run) | closing (the holder has begun its release) |
# ended (the run is over, its host is gone, or its takeover is unconfirmed: nothing may join it) | busy (nothing was registered).
function Add-QaMpRigLockTokens {
    param([string[]]$Tokens, [string]$Token = '')
    if ([string]::IsNullOrWhiteSpace($Token)) { $Token = [string]$env:MRS_QA_ADMISSION_TOKEN }
    $newTokens = @(ConvertTo-QaTokenList -Tokens $Tokens)
    try {
        return (Edit-QaRigRecord -Mutate {
            param($s)
            $no = { param($why) return (New-QaRigEdit -Result ([pscustomobject]@{ Registered = $false; Reason = $why })) }
            if ($s.State -ne 'ok') { return (& $no 'absent') }
            if ([string]::IsNullOrWhiteSpace($Token) -or ($s.Lock.token -ne $Token)) { return (& $no 'foreign') }
            if ($s.Lock.state -eq 'closing') { return (& $no 'closing') }
            if (($s.Lock.state -ne 'active') -or (@($s.Lock.predecessors).Count -gt 0) -or -not (Test-QaMpLockHolderAlive -Lock $s.Lock)) { return (& $no 'ended') }
            $h = Copy-QaMpLockRecord -Lock $s.Lock
            $h['tokens'] = @(Merge-QaPaths -Existing @($s.Lock.tokens) -Add @($newTokens))
            return (New-QaRigEdit -Save ([pscustomobject]$h) -Result ([pscustomobject]@{ Registered = $true; Reason = 'registered' }))
        })
    } catch {
        return [pscustomobject]@{ Registered = $false; Reason = 'busy' }
    }
}

# Registration that must succeed before any engine of the run is launched. A transient 'busy' is retried within TimeoutSec; any
# other refusal (absent, foreign, closing, ended) is final. The caller refuses to launch unless Registered.
function Register-QaRigEngines {
    param([string[]]$Tokens, [int]$TimeoutSec = 60, [int]$PollSec = 2)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ($true) {
        $r = Add-QaMpRigLockTokens -Tokens $Tokens
        if ($r.Registered -or $r.Reason -ne 'busy' -or (Get-Date) -ge $deadline) { return $r }
        Start-Sleep -Seconds $PollSec
    }
}

# The run ended but its engines did not die (or could not be shown to be gone, or the port stayed busy): the record becomes
# ENDED - its occupancy is then decided by what it owns being gone (and the port, for Why = 'port'), not by the host that wrote
# it, which may be a long-lived shell. The reasons are added to the record under the mutex, so they accumulate and cannot be lost
# to another writer. The token also leaves this process's environment. Returns whether the record was changed. If it was not (the
# lock file could not be written), that is said loudly and the lock FAILS CLOSED: it keeps looking like a live owner until this
# host exits - which refuses everyone and loses nothing.
function Set-QaMpRigLockRetained {
    param([string]$Token = '', [string]$Why = '')
    if ([string]::IsNullOrWhiteSpace($Token)) { $Token = [string]$env:MRS_QA_ADMISSION_TOKEN }
    $done = $false
    if (-not [string]::IsNullOrWhiteSpace($Token)) {
        try {
            $done = [bool](Edit-QaRigRecord -Mutate {
                param($s)
                if (($s.State -ne 'ok') -or ($s.Lock.token -ne $Token)) { return (New-QaRigEdit -Result $false) }
                $h = Copy-QaMpLockRecord -Lock $s.Lock
                $h['state'] = 'ended'
                $h['why'] = @(Merge-QaWhy -Existing @($s.Lock.why) -Add $Why)
                return (New-QaRigEdit -Save ([pscustomobject]$h) -Result $true)
            })
        } catch {
            Write-QaError ('The rig lock could NOT be marked as ended-but-kept (' + $_.Exception.Message + '). It fails closed: it keeps looking like a live owner until this PowerShell host exits - close it, or kill the engines and delete ' + (Get-QaMpRigLockPath) + '.')
        }
    }
    if ([string]$env:MRS_QA_ADMISSION_TOKEN -eq $Token) { Remove-Item Env:MRS_QA_ADMISSION_TOKEN -ErrorAction SilentlyContinue }
    return $done
}

# THE release. Deletes the lock only when (1) it still carries OUR token and (2) the absence check says Gone for the record's
# COMPLETE target - every owner it answers for, with their worktree fallbacks, and the port where an ended run says 'port' -
# and (3) the record is still the version that was checked. Otherwise the lock is kept, ended (see Set-QaMpRigLockRetained),
# and the answer says why:
#   survivors - an owned engine (or the port) is still there
#   unknown   - absence could not be shown (query failed, undecidable engine); Blockers say what stands in the way
#   busy      - the mutex or the record could not be used
# ORDER, explicitly: the release first marks the record CLOSING (under the mutex) and reads what it owns then; from that moment
# registration is refused. It checks that target, and the final delete re-reads the record under the mutex and refuses to
# delete if the record's revision moved since (anything - a registration, an ended transition, a new reason - bumps it): it
# then checks the NEW target, a bounded number of times.
# -StopEngines first stops the engines the record owns. Returns Released / Reason / Survivors / Blockers.
function Close-QaRigAdmission {
    param([string]$Token = '', [switch]$StopEngines, [int]$GraceSec = 10, [double]$SettleSec = $script:QaRigSettleSec)
    if ([string]::IsNullOrWhiteSpace($Token)) { $Token = [string]$env:MRS_QA_ADMISSION_TOKEN }
    $fail = { param($reason, $survivors, $blockers) return [pscustomobject]@{ Released = $false; Reason = $reason; Survivors = @($survivors); Blockers = @($blockers) } }

    $state = Read-QaMpRigLockState
    if ($state.State -ne 'ok') { return (& $fail 'absent' @() @()) }
    $lock = $state.Lock
    $mine = $false
    if (-not [string]::IsNullOrWhiteSpace($Token)) {
        $mine = ($lock.token -eq $Token)
    } else {
        # No token anywhere (a lock taken by an older copy of the script): this process's own pid and start time.
        $mine = (([int]$lock.pid) -eq $PID) -and ($lock.pidStartedUtc -eq (Get-QaMpLockIdentity))
    }
    if (-not $mine) {
        Write-QaWarn ('MP rig lock: NOT releasing - it is held by PID ' + $lock.pid + ', not by this run (' + $PID + ').')
        return (& $fail 'not-owner' @() @())
    }
    $recToken = $lock.token
    $identity = Get-QaMpLockIdentity
    $isMine = { param($l) if ($recToken -ne '') { return ($l.token -eq $recToken) } return ((([int]$l.pid) -eq $PID) -and ($l.pidStartedUtc -eq $identity)) }

    # 1. Begin closing: registration is refused from here; read what the record owns, and the revision that read belongs to.
    try {
        $begin = Edit-QaRigRecord -Mutate {
            param($s)
            if (($s.State -ne 'ok') -or -not (& $isMine $s.Lock)) { return (New-QaRigEdit -Result $null) }
            $h = Copy-QaMpLockRecord -Lock $s.Lock
            if ($s.Lock.state -eq 'active') { $h['state'] = 'closing' }
            return (New-QaRigEdit -Save ([pscustomobject]$h) -Result ([pscustomobject]@{ Target = (Get-QaRunTarget -Rec ([pscustomobject]$h)); Rev = ([int]$s.Lock.rev + 1) }))
        }
    } catch {
        Write-QaWarn ('MP rig lock: could not begin the release (' + $_.Exception.Message + '). It stays on disk and keeps refusing others.')
        return (& $fail 'busy' @() @())
    }
    if ($null -eq $begin) { return (& $fail 'absent' @() @()) }
    $target = $begin.Target
    $checkedRev = [int]$begin.Rev

    # 2. Check, then delete only if the record is still the version that was checked.
    for ($round = 1; $round -le 3; $round++) {
        $scan = Stop-QaRunEngines -Target $target -GraceSec $GraceSec -SettleSec $SettleSec -ScanOnly:(-not $StopEngines)
        if ($scan.Found -gt 0 -and $StopEngines) { Write-QaInfo ('Admission cleanup: stopped ' + $scan.Found + ' engine process(es) still carrying this run''s profile.') }
        if (-not $scan.Gone) {
            $what = 'survivors'
            $why = 'engines survived'
            $text = 'engine pid(s) ' + (@($scan.Survivors) -join ', ') + ' from this run are still alive'
            if (@($scan.Survivors).Count -eq 0) { $text = 'something this run owns is still there' }
            if (@($scan.Blockers | Where-Object { $_.Why -eq 'port' }).Count -gt 0) { $why = 'port' }
            if ($scan.Unknown) { $what = 'unknown'; $why = 'process query failed'; $text = 'absence could not be shown (the process query failed, or an engine whose ownership cannot be decided is running)' }
            Write-QaError ('NOT releasing the rig admission: ' + $text + '. Kept (as an ended run); it clears by itself once everything it owns is gone. ' + (Format-QaBlockers -Blockers $scan.Blockers))
            [void](Set-QaMpRigLockRetained -Token $recToken -Why $why)
            return (& $fail $what @($scan.Survivors) @($scan.Blockers))
        }
        try {
            $outcome = Edit-QaRigRecord -Mutate {
                param($s)
                if (($s.State -ne 'ok') -or -not (& $isMine $s.Lock)) { return (New-QaRigEdit -Result ([pscustomobject]@{ Done = $false; Changed = $false; Target = $null; Rev = 0 })) }
                if ([int]$s.Lock.rev -ne $checkedRev) { return (New-QaRigEdit -Result ([pscustomobject]@{ Done = $false; Changed = $true; Target = (Get-QaRunTarget -Rec $s.Lock); Rev = [int]$s.Lock.rev })) }
                Remove-Item -LiteralPath (Get-QaMpRigLockPath) -Force -ErrorAction Stop
                return (New-QaRigEdit -Result ([pscustomobject]@{ Done = $true; Changed = $false; Target = $null; Rev = 0 }))
            }
        } catch {
            Write-QaWarn ('MP rig lock: could not release (' + $_.Exception.Message + '). It stays on disk and keeps refusing others.')
            [void](Set-QaMpRigLockRetained -Token $recToken -Why 'release failed')
            return (& $fail 'busy' @() @())
        }
        if ($outcome.Done) {
            if ([string]$env:MRS_QA_ADMISSION_TOKEN -eq $Token) { Remove-Item Env:MRS_QA_ADMISSION_TOKEN -ErrorAction SilentlyContinue }
            return [pscustomobject]@{ Released = $true; Reason = 'released'; Survivors = @(); Blockers = @() }
        }
        if (-not $outcome.Changed) { return (& $fail 'absent' @() @()) }
        $target = $outcome.Target   # the record changed while we checked: check what it says now
        $checkedRev = [int]$outcome.Rev
    }
    Write-QaError 'NOT releasing the rig admission: the record kept changing while it was being verified.'
    [void](Set-QaMpRigLockRetained -Token $recToken -Why 'record kept changing')
    return (& $fail 'busy' @() @())
}

# Compatibility name for callers that only want "give it back": the same verified release, result ignored.
function Remove-QaMpRigLock {
    param([string]$Token = '')
    [void](Close-QaRigAdmission -Token $Token)
}


function Get-QaUdpPortHolder {
    param([int]$Port = 2001)
    $ep = $null
    try { $ep = Get-NetUDPEndpoint -LocalPort $Port -ErrorAction Stop } catch { return $null }
    if (-not $ep) { return $null }

    $owningPid = 0
    try { $owningPid = [int](@($ep)[0].OwningProcess) } catch { $owningPid = 0 }

    $name = "unknown"
    $cmdLine = ""
    try {
        $proc = @(Get-CimInstance -ClassName Win32_Process -Filter ("ProcessId=" + $owningPid) -ErrorAction Stop)
        if (@($proc).Count -gt 0) {
            $name = [string]$proc[0].Name
            $cmdLine = [string]$proc[0].CommandLine
        }
    } catch {
        # A port we cannot attribute is still worth reporting as busy.
    }

    $isQaRun = $false
    if ($cmdLine) {
        foreach ($n in @(".qa\runs\", ".qa/runs/")) {
            if ($cmdLine.IndexOf($n, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) { $isQaRun = $true }
        }
    }

    return [pscustomobject]@{
        Port      = $Port
        OwningPid = $owningPid
        Name      = $name
        IsQaRun   = $isQaRun
    }
}

# ------------------------------------------------------------------------------------------------
# MP QUEUED PRE-FLIGHT (#974). Shared by mp-run.ps1 (-Queue) and mp-sweep.ps1 (between scenarios).
#
# Block until an MP run can actually start. Polls rather than sleeping a fixed amount, because engine
# shutdown time varies with how far into world streaming the previous run got.
#
# Scoped to what is genuinely CONTENDED, not to "any Reforger process anywhere" (#954 review). The first
# version waited on a bare Get-Process ArmaReforger* wildcard, justified by "the port is global" - but
# that also matches the user own singleplayer game and any Workbench instance, none of which bind UDP
# 2001 or block anything. A user alt-tabbed into their own game would have stalled the wait for the full
# timeout and then proceeded anyway, so the wait bought nothing but a ten-minute delay. What is actually
# contended is the port, plus this worktree own engines - so those are what we wait on.
#
# -Announce makes the wait narrate itself (mp-run's -Queue, where a silent 30-minute stall would read as
# a hang). Left off, the function is silent, which is what mp-sweep has always done.
function Wait-QaMpIdle {
    param(
        [string]$WorktreePath,
        [int]$TimeoutSec = 600,
        [int]$Port = 2001,
        [int]$PollSec = 5,
        [switch]$Announce
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $holder = Get-QaUdpPortHolder -Port $Port
        $portHeld = ($null -ne $holder)

        # Only a LIVE run is worth waiting for. An orphan never exits on its own, and mp-run reclaims it
        # in its own pre-flight the moment we hand over - so counting orphans here would make the caller
        # sit out the whole timeout waiting for something that will be gone a second later, and then
        # proceed anyway. Wait for what can actually finish.
        $liveRuns = @(Get-QaOrphanRuns -WorktreePath $WorktreePath | Where-Object { $_.HostAlive })
        if (-not $portHeld -and @($liveRuns).Count -eq 0) { return $true }

        if ($Announce) {
            $what = New-Object System.Collections.ArrayList
            if ($portHeld) { [void]$what.Add('UDP ' + $Port + ' held by ' + $holder.Name + ' (PID ' + $holder.OwningPid + ')') }
            if (@($liveRuns).Count -gt 0) { [void]$what.Add(@($liveRuns).Count.ToString() + ' live run(s) in this worktree') }
            $left = [int]($deadline - (Get-Date)).TotalSeconds
            Write-QaInfo ('Waiting: ' + (@($what.ToArray()) -join '; ') + '... (' + $left + 's left)')
        }
        Start-Sleep -Seconds $PollSec
    }
    return $false
}
