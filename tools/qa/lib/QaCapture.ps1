# QaCapture.ps1 - optional background window recording for BGONE MP scenarios (PowerShell 5.1 ONLY).
# Touches no mouse or foreground focus (uses ffmpeg gfxcapture).

function Get-QaFfmpegPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    foreach ($dir in (($machine + ';' + $user + ';' + $env:Path) -split ';')) {
        if ([string]::IsNullOrWhiteSpace($dir)) { continue }
        $candidate = Join-Path $dir.Trim() 'ffmpeg.exe'
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    return $null
}

if (-not ('QaCaptureWin32' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class QaCaptureWin32 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
}
'@
}

function Find-QaProcessWindow {
    param([int[]]$Pids)
    foreach ($procId in @($Pids)) {
        $p = Get-Process -Id $procId -ErrorAction SilentlyContinue
        if ($null -eq $p) { continue }
        $p.Refresh()
        if ($p.MainWindowHandle -ne [IntPtr]::Zero -and [QaCaptureWin32]::IsWindowVisible($p.MainWindowHandle)) {
            return [int64]$p.MainWindowHandle
        }
    }
    return [int64]0
}

function Get-QaWindowClientSize {
    param([int64]$Hwnd)
    $r = New-Object QaCaptureWin32+RECT
    [void][QaCaptureWin32]::GetClientRect([IntPtr]$Hwnd, [ref]$r)
    return ('' + ($r.Right - $r.Left) + 'x' + ($r.Bottom - $r.Top))
}

function Start-QaCapture {
    param(
        [Parameter(Mandatory)] [int64]$Hwnd,
        [Parameter(Mandatory)] [string]$RawPath,
        [int]$MaxSec = 3600,
        [int]$Fps = 30
    )
    $ffmpeg = Get-QaFfmpegPath
    if (-not $ffmpeg) {
        Write-QaWarn 'Recording skipped: ffmpeg.exe is not on Path.'
        return $null
    }

    $source = 'gfxcapture=hwnd=' + $Hwnd + ':max_framerate=' + $Fps + ':capture_cursor=0,hwdownload,format=bgra'
    $ffArgs = @('-hide_banner', '-loglevel', 'warning', '-y',
        '-f', 'lavfi', '-i', $source,
        '-t', [string]$MaxSec,
        '-vf', "scale=-2:'min(720,ih)'",
        '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '26', '-pix_fmt', 'yuv420p',
        $RawPath)

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ffmpeg
    $psi.Arguments = (($ffArgs | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join ' ')
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    $stderr = New-Object System.Text.StringBuilder
    $handler = Register-ObjectEvent -InputObject $proc -EventName ErrorDataReceived -MessageData $stderr -Action {
        if ($EventArgs.Data) { [void]$Event.MessageData.AppendLine($EventArgs.Data) }
    }
    [void]$proc.Start()
    $proc.BeginErrorReadLine()

    return [pscustomobject]@{
        Process   = $proc
        Handler   = $handler
        Stderr    = $stderr
        Hwnd      = $Hwnd
        RawPath   = $RawPath
        StartTime = Get-Date
        Size      = Get-QaWindowClientSize -Hwnd $Hwnd
    }
}

function Stop-QaCapture {
    param($Capture)
    if ($null -eq $Capture) { return '' }
    $proc = $Capture.Process
    if (-not $proc.HasExited) {
        try { $proc.StandardInput.Write('q'); $proc.StandardInput.Flush() } catch {}
        if (-not $proc.WaitForExit(15000)) {
            try { $proc.Kill() } catch {}
        }
    }
    $Capture | Add-Member -NotePropertyName StopTime -NotePropertyValue (Get-Date) -Force
    try { Unregister-Event -SourceIdentifier $Capture.Handler.Name -ErrorAction SilentlyContinue } catch {}
    return $Capture.Stderr.ToString().Trim()
}

# Parses BGONE_MPBOT beat/title/play/run/verdict events from a client log.
function Get-QaBotEvents {
    param([string]$LogPath, [datetime]$Day)
    $events = New-Object System.Collections.ArrayList
    $order = 0
    if (-not $LogPath -or -not (Test-Path -LiteralPath $LogPath)) { return $events.ToArray() }
    $t = '^(?<t>\d{2}:\d{2}:\d{2}\.\d{3}).*'
    $beatRx = $t + 'BGONE_MPBOT \| BEAT \| (?<n>\d+) \| (?<name>[^|]+?) \| (?<state>START|PASS|FAIL)(?: \| (?<detail>.*?))?(?: \| SAY: (?<say>.*?))?''?\s*$'
    $titleRx = $t + 'BGONE_MPBOT \| TITLE \| (?<title>.*?)''?\s*$'
    $verdictRx = $t + 'BGONE_MPBOT \| VERDICT \| (?<state>PASS|FAIL) \| (?<name>[^|]+?) \| (?<detail>.*?)''?\s*$'
    $playRx = $t + 'BGONE_MPBOT \| PLAY \| START'
    $runRx = $t + 'BGONE_MPBOT \| RUN \| (?<name>[^|]+?) \| (?<total>\d+) beat'
    $clock = New-QaLogClock -LogPath $LogPath -Fallback $Day
    foreach ($line in [System.IO.File]::ReadLines($LogPath)) {
        $kind = $null
        $m = [regex]::Match($line, $beatRx)
        if ($m.Success) { $kind = 'beat' }
        if (-not $kind) { $m = [regex]::Match($line, $verdictRx); if ($m.Success) { $kind = 'verdict' } }
        if (-not $kind) { $m = [regex]::Match($line, $playRx); if ($m.Success) { $kind = 'play' } }
        if (-not $kind) { $m = [regex]::Match($line, $titleRx); if ($m.Success) { $kind = 'title' } }
        if (-not $kind) { $m = [regex]::Match($line, $runRx); if ($m.Success) { $kind = 'run' } }
        if (-not $kind) { continue }
        $time = Get-QaLogClockTime -Clock $clock -TimeOfDay $m.Groups['t'].Value
        $n = 0
        if ($kind -eq 'beat') { $n = [int]$m.Groups['n'].Value }
        if ($kind -eq 'run') { $n = [int]$m.Groups['total'].Value }
        $order++
        [void]$events.Add([pscustomobject]@{
            Order  = $order
            Kind   = $kind
            Time   = $time
            N      = $n
            Name   = $m.Groups['name'].Value.Trim()
            State  = $m.Groups['state'].Value
            Detail = $m.Groups['detail'].Value.Trim()
            Say    = $m.Groups['say'].Value.Trim()
            Title  = $m.Groups['title'].Value.Trim()
        })
    }
    return $events.ToArray()
}

function Format-QaSrtTime {
    param([double]$Seconds)
    if ($Seconds -lt 0) { $Seconds = 0 }
    $ts = [timespan]::FromSeconds($Seconds)
    return ('{0:00}:{1:00}:{2:00},{3:000}' -f [int][math]::Floor($ts.TotalHours), $ts.Minutes, $ts.Seconds, $ts.Milliseconds)
}

function Get-QaTrimSeconds {
    param($Events, [datetime]$CaptureStart)
    foreach ($e in @($Events)) {
        if ($e.Kind -eq 'play') { return [math]::Max(0, ($e.Time - $CaptureStart).TotalSeconds - 3.0) }
    }
    return 0
}

function Limit-QaText {
    param([string]$Text, [int]$Max)
    if ($Text.Length -le $Max) { return $Text }
    return ($Text.Substring(0, $Max - 3) + '...')
}

function Write-QaBeatSrt {
    param($Events, [datetime]$VideoStart, [string]$SrtPath, [string]$RunOutcome = '')
    $evts = @($Events | Sort-Object Time, Order)
    $play = $null
    $runTotal = 0
    foreach ($e in $evts) {
        if ($e.Kind -eq 'play' -and -not $play) { $play = $e }
        if ($e.Kind -eq 'run') { $runTotal = $e.N }
    }
    $setupBeats = 0
    if ($play) {
        $seen = @{}
        foreach ($e in $evts) {
            if ($e.Kind -eq 'beat' -and $e.State -eq 'START' -and $e.Time -lt $play.Time) { $seen[$e.N] = $true }
        }
        $setupBeats = $seen.Count
    }
    $total = [math]::Max(0, $runTotal - $setupBeats)
    $shown = @($evts | Where-Object { $_.Kind -eq 'verdict' -or ($_.Kind -eq 'beat' -and $_.N -gt $setupBeats) })

    $green = '<font color="#7CFC7C">'
    $red = '<font color="#FF7070">'
    $small = '{' + [char]92 + 'fs9}'
    $cues = New-Object System.Collections.ArrayList
    $results = New-Object System.Collections.ArrayList
    $started = @{}

    $title = ''
    $playAt = 0
    foreach ($e in $evts) {
        if ($e.Kind -eq 'title' -and -not $title) { $title = $e.Title }
        if ($e.Kind -eq 'play' -and -not $playAt) { $playAt = ($e.Time - $VideoStart).TotalSeconds }
    }
    if ($title -and $playAt -gt 0.5) {
        [void]$cues.Add([pscustomobject]@{ From = 0; To = $playAt; Text = ('Testing: ' + $title) })
    }
    for ($i = 0; $i -lt $shown.Count; $i++) {
        $e = $shown[$i]
        $at = ($e.Time - $VideoStart).TotalSeconds
        if ($e.Kind -eq 'beat' -and $e.State -eq 'START') { $started[$e.N] = $at; continue }

        $until = $at + 6
        if (($i + 1) -lt $shown.Count) { $until = ($shown[$i + 1].Time - $VideoStart).TotalSeconds }
        $until = [math]::Max($until, $at + 0.2)

        $color = $green
        $word = 'Done'
        if ($e.State -eq 'FAIL') { $color = $red; $word = "Didn't work" }
        $detail = ''
        if ($e.Kind -eq 'beat') {
            $step = $e.N - $setupBeats
            $label = ('Step ' + $step + ' of ' + $total + ': ' + $e.Name)
            if ($started.ContainsKey($e.N) -and (($at - $started[$e.N]) -ge 0.6)) {
                [void]$cues.Add([pscustomobject]@{ From = $started[$e.N]; To = $at; Text = ($label + '...') })
            }
            if ($e.State -eq 'FAIL') { $detail = Limit-QaText -Text $e.Say -Max 95 }
        } else {
            $subject = $title
            if (-not $subject) { $subject = $e.Name }
            $label = Get-QaVerdictCaption -PlayerPassed ($e.State -eq 'PASS') -RunOutcome $RunOutcome
            if ($label -ne 'Test passed') { $color = $red }
            $word = ''
            $detail = $subject
            $until = $at + 6
        }
        $text = $label
        if ($word) { $text += '  ' + $color + $word + '</font>' }
        elseif ($e.Kind -ne 'beat') { $text = $color + $label + '</font>' }
        if ($detail) { $text += ("`n" + $small + $detail) }
        [void]$cues.Add([pscustomobject]@{ From = $at; To = $until; Text = $text })
        [void]$results.Add([pscustomobject]@{ End = $at; Label = (($label + ' - ' + $word).TrimEnd(' ', '-')) })
    }

    $sb = New-Object System.Text.StringBuilder
    $n = 0
    foreach ($c in ($cues | Sort-Object From)) {
        if (($c.To - $c.From) -lt 0.2) { continue }
        $n++
        [void]$sb.AppendLine([string]$n)
        [void]$sb.AppendLine((Format-QaSrtTime $c.From) + ' --> ' + (Format-QaSrtTime $c.To))
        [void]$sb.AppendLine($c.Text)
        [void]$sb.AppendLine('')
    }
    [System.IO.File]::WriteAllText($SrtPath, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
    return $results.ToArray()
}

function Complete-QaCapture {
    param($Capture, [string]$ClientLog, [string]$OutDir, [string]$BaseName, [switch]$KeepBody, [string]$CodeVersion = '', [string]$RunOutcome = '')
    if ($null -eq $Capture) { return 'not recorded' }
    $ffmpeg = Get-QaFfmpegPath
    if (-not (Test-Path -LiteralPath $Capture.RawPath) -or (Get-Item -LiteralPath $Capture.RawPath).Length -lt 1024) {
        return ('capture produced no video (' + $Capture.Stderr.ToString().Trim() + ')')
    }

    $events = Get-QaBotEvents -LogPath $ClientLog -Day $Capture.StartTime
    $trim = Get-QaTrimSeconds -Events $events -CaptureStart $Capture.StartTime
    $videoStart = $Capture.StartTime.AddSeconds($trim)
    $Capture | Add-Member -NotePropertyName VideoStart -NotePropertyValue $videoStart -Force
    $srtName = $BaseName + '.beats.srt'
    $cues = Write-QaBeatSrt -Events $events -VideoStart $videoStart -SrtPath (Join-Path $OutDir $srtName) -RunOutcome $RunOutcome
    $inv = [System.Globalization.CultureInfo]::InvariantCulture

    Push-Location $OutDir
    try {
        $rawName = Split-Path -Leaf $Capture.RawPath
        $mp4Name = $BaseName + '.mp4'
        $vf = 'null'
        if (@($cues).Count -gt 0) {
            $style = 'FontName=Arial,FontSize=13,Bold=1,BorderStyle=3,Outline=3,Shadow=0,MarginV=14,PrimaryColour=&H00FFFFFF,OutlineColour=&HA0000000'
            $vf = ('subtitles=' + $srtName + ":force_style='" + $style + "'")
        }
        $cleanName = $BaseName + '.clean.mp4'
        & $ffmpeg -hide_banner -loglevel error -y -ss $trim.ToString($inv) -i $rawName -vf $vf -c:v libx264 -preset veryfast -crf 26 -pix_fmt yuv420p -movflags +faststart $mp4Name -vf null -c:v libx264 -preset veryfast -crf 26 -pix_fmt yuv420p -movflags +faststart $cleanName
        if ($LASTEXITCODE -ne 0) { return ('encode failed (ffmpeg exit ' + $LASTEXITCODE + ')') }

        if ($KeepBody) { Copy-Item -LiteralPath (Join-Path $OutDir $mp4Name) -Destination (Join-Path $OutDir ($BaseName + '.body.mp4')) -Force }

        $stepResults = Get-QaStepResults -Events $events
        if (@($stepResults.Steps).Count -gt 0) {
            $cardPng = Join-Path $OutDir ($BaseName + '.card.png')
            try {
                New-QaSummaryCard -Results $stepResults -OutPng $cardPng -CodeVersion $CodeVersion -RunOutcome $RunOutcome
                [void](Add-QaSummaryCard -Mp4 $mp4Name -CardPng $cardPng)
            } catch { }
            Remove-Item -LiteralPath $cardPng -Force -ErrorAction SilentlyContinue
        }

        $frames = @($cues | Where-Object { $_.End -gt 0 })
        if ($frames.Count -gt 0) {
            $tmp = '_contact'
            if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
            New-Item -ItemType Directory -Path $tmp | Out-Null
            Copy-Item -LiteralPath (Join-Path $env:WINDIR 'Fonts\arial.ttf') -Destination (Join-Path $tmp 'font.ttf')
            $k = 0
            foreach ($f in $frames) {
                $k++
                $labelFile = $tmp + '/label' + $k.ToString('00') + '.txt'
                [System.IO.File]::WriteAllText((Join-Path $OutDir $labelFile), $f.Label, (New-Object System.Text.UTF8Encoding($false)))
                $at = ([math]::Max(0, $f.End + $trim - 0.3)).ToString($inv)
                $draw = 'scale=640:-2,drawtext=fontfile=' + $tmp + '/font.ttf:textfile=' + $labelFile + ':x=8:y=8:fontsize=20:fontcolor=white:box=1:boxcolor=black@0.6:boxborderw=6'
                & $ffmpeg -hide_banner -loglevel error -y -ss $at -i $rawName -frames:v 1 -update 1 -vf $draw ($tmp + '/f' + $k.ToString('00') + '.png')
            }
            $rows = [math]::Ceiling($frames.Count / 3)
            & $ffmpeg -hide_banner -loglevel error -y -framerate 1 -i ($tmp + '/f%02d.png') -vf ('tile=3x' + $rows + ':padding=4:color=black') -frames:v 1 -update 1 ($BaseName + '.contact.png')
            Remove-Item -Recurse -Force $tmp
        }
    } finally {
        Pop-Location
    }

    Remove-Item -LiteralPath $Capture.RawPath -Force -ErrorAction SilentlyContinue
    $seconds = 0
    if ($Capture.StopTime) { $seconds = [int]($Capture.StopTime - $Capture.StartTime).TotalSeconds - [int]$trim }
    return ('recorded ' + $seconds + ' s of a ' + $Capture.Size + ' window (cut ' + [int]$trim + ' s of loading), ' + @($cues).Count + ' result(s)')
}

. (Join-Path $PSScriptRoot 'QaVideoExtras.ps1')

function Get-QaVerdictCaption {
    param([bool]$PlayerPassed, [string]$RunOutcome = '')
    if ($RunOutcome -eq 'passed' -and $PlayerPassed) { return 'Test passed' }
    if ($RunOutcome -eq 'failed' -and $PlayerPassed) { return "This player's steps worked, but the test failed" }
    if ($RunOutcome -eq 'failed' -or $RunOutcome -eq 'passed') { return 'Test failed' }
    if ($PlayerPassed) { return "This player's steps worked" }
    return "This player's steps did not all work"
}
