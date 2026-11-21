# QaVideoExtras.ps1 - summary card, failure note and side-by-side video for BGONE QA (PowerShell 5.1 ONLY).

function Get-QaFfprobePath {
    $ffmpeg = Get-QaFfmpegPath
    if (-not $ffmpeg) { return $null }
    $probe = Join-Path (Split-Path -Parent $ffmpeg) 'ffprobe.exe'
    if (Test-Path -LiteralPath $probe) { return $probe }
    return $null
}

function Get-QaStepResults {
    param($Events)
    $evts = @($Events | Sort-Object Time, Order)
    $play = $null
    $title = ''
    $verdict = $null
    foreach ($e in $evts) {
        if ($e.Kind -eq 'play' -and -not $play) { $play = $e }
        if ($e.Kind -eq 'title' -and -not $title) { $title = $e.Title }
        if ($e.Kind -eq 'verdict' -and -not $verdict) { $verdict = $e }
    }
    $setup = 0
    if ($play) {
        $seen = @{}
        foreach ($e in $evts) {
            if ($e.Kind -eq 'beat' -and $e.State -eq 'START' -and $e.Time -lt $play.Time) { $seen[$e.N] = $true }
        }
        $setup = $seen.Count
    }
    $steps = New-Object System.Collections.ArrayList
    foreach ($e in $evts) {
        if ($e.Kind -eq 'beat' -and $e.N -gt $setup -and ($e.State -eq 'PASS' -or $e.State -eq 'FAIL')) {
            [void]$steps.Add([pscustomobject]@{ Step = ($e.N - $setup); Name = $e.Name; Passed = ($e.State -eq 'PASS'); Say = $e.Say; Detail = $e.Detail; Time = $e.Time })
        }
    }
    return [pscustomobject]@{ Title = $title; Steps = $steps.ToArray(); Verdict = $verdict; SetupBeats = $setup }
}

function New-QaSummaryCard {
    param($Results, [string]$OutPng, [int]$Width = 1280, [int]$Height = 720, [string]$CodeVersion = '', [string]$RunOutcome = '')
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap($Width, $Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.SmoothingMode = 'AntiAlias'
        $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
        $g.Clear([System.Drawing.Color]::FromArgb(27, 28, 31))
        $white = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
        $grey = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(170, 170, 178))
        $green = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(124, 252, 124))
        $red = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 112, 112))
        $px = [System.Drawing.GraphicsUnit]::Pixel
        $fTitle = New-Object System.Drawing.Font('Segoe UI', 22, [System.Drawing.FontStyle]::Regular, $px)
        $fVerdict = New-Object System.Drawing.Font('Segoe UI', 40, [System.Drawing.FontStyle]::Bold, $px)
        $fRow = New-Object System.Drawing.Font('Segoe UI', 19, [System.Drawing.FontStyle]::Regular, $px)
        $fWhy = New-Object System.Drawing.Font('Segoe UI', 24, [System.Drawing.FontStyle]::Regular, $px)

        $steps = @($Results.Steps)
        $failed = @($steps | Where-Object { -not $_.Passed })
        $passedAll = ($steps.Count -gt 0) -and ($failed.Count -eq 0) -and ($null -ne $Results.Verdict) -and ($Results.Verdict.State -eq 'PASS') -and ($RunOutcome -ne 'failed')

        $g.DrawString(('Testing: ' + (Limit-QaText -Text $Results.Title -Max 90)), $fTitle, $grey, 48, 30)
        if ($passedAll) {
            $g.DrawString('Test passed', $fVerdict, $green, 48, 62)
        } else {
            $g.DrawString('Test failed', $fVerdict, $red, 48, 62)
        }

        $perColumn = 14
        $top = 140
        $rowH = 31
        $colW = 590
        $max = $perColumn * 2
        $shown = $steps
        $more = 0
        if ($steps.Count -gt $max) { $shown = @($steps | Select-Object -First ($max - 1)); $more = $steps.Count - ($max - 1) }
        for ($i = 0; $i -lt $shown.Count; $i++) {
            $col = [math]::Floor($i / $perColumn)
            $row = $i % $perColumn
            $x = 48 + $col * $colW
            $y = $top + $row * $rowH
            $s = $shown[$i]
            $brush = $green
            $word = 'Done'
            if (-not $s.Passed) { $brush = $red; $word = "Didn't work" }
            $name = Limit-QaText -Text $s.Name -Max 46
            $g.DrawString(('{0,2}.  {1}' -f $s.Step, $name), $fRow, $white, $x, $y)
            $g.DrawString($word, $fRow, $brush, ($x + 440), $y)
        }
        if ($more -gt 0) {
            $g.DrawString(('... and ' + $more + ' more'), $fRow, $grey, (48 + $colW), ($top + ($perColumn - 1) * $rowH))
        }
        if ($failed.Count -gt 0) {
            $why = $failed[0].Say
            if (-not $why) { $why = 'This step did not work.' }
            $rect = New-Object System.Drawing.RectangleF(48, ($Height - 120), ($Width - 96), 100)
            $g.DrawString(('What went wrong, at step ' + $failed[0].Step + ': ' + $why), $fWhy, $red, $rect)
        } elseif ($RunOutcome -eq 'failed') {
            $rect = New-Object System.Drawing.RectangleF(48, ($Height - 120), ($Width - 96), 100)
            $g.DrawString('This player''s steps worked, but the test as a whole did not pass. See summary.txt.', $fWhy, $red, $rect)
        }
        if ($CodeVersion) {
            $fSmall = New-Object System.Drawing.Font('Segoe UI', 15, [System.Drawing.FontStyle]::Regular, $px)
            $g.DrawString(('Recorded from code version ' + $CodeVersion), $fSmall, $grey, 48, ($Height - 30))
        }
        $bmp.Save($OutPng, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $g.Dispose()
        $bmp.Dispose()
    }
}

function Add-QaSummaryCard {
    param([string]$Mp4, [string]$CardPng, [int]$Seconds = 7)
    $ffmpeg = Get-QaFfmpegPath
    $probe = Get-QaFfprobePath
    if (-not $ffmpeg -or -not $probe) { return $false }
    $dims = (& $probe -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 $Mp4 | Select-Object -First 1)
    if ($dims -notmatch '^\d+x\d+$') { return $false }
    $w = ($dims -split 'x')[0]
    $h = ($dims -split 'x')[1]
    $tmp = $Mp4 + '.card.mp4'
    $graph = '[0:v]fps=30,format=yuv420p,setsar=1[a];[1:v]scale=' + $w + ':' + $h + ',fps=30,format=yuv420p,setsar=1[b];[a][b]concat=n=2:v=1:a=0[v]'
    & $ffmpeg -hide_banner -loglevel error -y -i $Mp4 -loop 1 -framerate 30 -t $Seconds -i $CardPng -filter_complex $graph -map '[v]' -c:v libx264 -preset veryfast -crf 26 -pix_fmt yuv420p -movflags +faststart $tmp
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $tmp)) { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue; return $false }
    Move-Item -LiteralPath $tmp -Destination $Mp4 -Force
    return $true
}

function New-QaSideBySide {
    param($Videos, [string]$OutPath)
    $ffmpeg = Get-QaFfmpegPath
    if (-not $ffmpeg) { return $false }
    $vs = @($Videos | Sort-Object { $_.Start })
    if ($vs.Count -lt 2) { return $false }
    $base = $vs[0].Start
    $inv = [System.Globalization.CultureInfo]::InvariantCulture
    $ffArgs = @('-hide_banner', '-loglevel', 'error', '-y')
    foreach ($v in $vs) { $ffArgs += @('-i', $v.Path) }
    $parts = @()
    $labels = @()
    for ($i = 0; $i -lt $vs.Count; $i++) {
        $delay = [math]::Max(0, ($vs[$i].Start - $base).TotalSeconds)
        $filter = '[' + $i + ':v]fps=30,format=yuv420p,setsar=1'
        if ($delay -gt 0.05) { $filter += ',tpad=start_duration=' + $delay.ToString('0.###', $inv) + ':start_mode=clone' }
        $filter += '[v' + $i + ']'
        $parts += $filter
        $labels += ('[v' + $i + ']')
    }
    $parts += (($labels -join '') + 'hstack=inputs=' + $vs.Count + ':shortest=0[out]')
    $ffArgs += @('-filter_complex', ($parts -join ';'), '-map', '[out]', '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '26', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', $OutPath)
    & $ffmpeg @ffArgs
    return ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $OutPath))
}

function Write-QaFailureNote {
    param([string]$OutPath, [string]$Scenario, [string]$Reason, $Clients, [string]$ServerLog, [int]$ServerSkipLines, [datetime]$Day)
    $failed = $null
    $failedClient = $null
    $failedRes = $null
    foreach ($c in @($Clients)) {
        $events = @(Get-QaBotEvents -LogPath $c.Log -Day $Day)
        $res = Get-QaStepResults -Events $events
        $bad = @($res.Steps | Where-Object { -not $_.Passed } | Select-Object -First 1)
        if ($bad.Count -gt 0 -and -not $failed) { $failed = $bad[0]; $failedClient = $c; $failedRes = $res }
    }

    $out = New-Object System.Collections.ArrayList
    if ($failed) {
        $say = $failed.Say
        if (-not $say) { $say = 'That step did not work.' }
        [void]$out.Add(('The test stopped at step ' + $failed.Step + ', "' + $failed.Name + '". ' + $say))
    } else {
        [void]$out.Add('The test never reported a result, so there is no failing step to name.')
    }
    [void]$out.Add('')
    [void]$out.Add('--- technical detail ---')
    [void]$out.Add(('Scenario : ' + $Scenario))
    [void]$out.Add(('Reason   : ' + $Reason))
    if ($failed) {
        [void]$out.Add(('Failing step detail: ' + $failed.Detail))
        [void]$out.Add('Steps just before it:')
        foreach ($s in @($failedRes.Steps | Where-Object { $_.Step -lt $failed.Step } | Select-Object -Last 4)) {
            $word = 'done'
            if (-not $s.Passed) { $word = 'FAILED' }
            [void]$out.Add(('  step ' + $s.Step + ' ' + $s.Name + ' - ' + $word + ': ' + (Limit-QaText -Text $s.Detail -Max 160)))
        }
    }

    $window = $null
    if ($failed) { $window = $failed.Time }
    $keep = 'BGONE_MPBOT|\(E\)|\(F\)'
    $sources = @()
    if ($failedClient) { $sources += @(@{ Label = ('client ' + $failedClient.Index); Path = $failedClient.Log; Skip = 0; Before = 25; After = 3 }) }
    if ($ServerLog) { $sources += @(@{ Label = 'server'; Path = $ServerLog; Skip = $ServerSkipLines; Before = 25; After = 10 }) }
    foreach ($src in $sources) {
        if (-not $src.Path -or -not (Test-Path -LiteralPath $src.Path)) { continue }
        [void]$out.Add('')
        [void]$out.Add(('Log lines near the failure, ' + $src.Label + ':'))
        $count = 0
        $lines = @(Read-QaLines -Path $src.Path)
        $clock = New-QaLogClock -LogPath $src.Path -Fallback $Day
        for ($li = 0; $li -lt $lines.Count; $li++) {
            $line = $lines[$li]
            $m = [regex]::Match($line, '^\s*(?<t>\d{2}:\d{2}:\d{2}\.\d{3})')
            $t = $null
            if ($m.Success) { $t = Get-QaLogClockTime -Clock $clock -TimeOfDay $m.Groups['t'].Value }
            if ($li -lt $src.Skip) { continue }
            if ($line -notmatch $keep) { continue }
            if ($window -and $null -ne $t) {
                $delta = ($t - $window).TotalSeconds
                if ($delta -lt (-1 * $src.Before) -or $delta -gt $src.After) { continue }
            } elseif ($window) {
                continue
            }
            $count++
            if ($count -gt 30) { break }
            [void]$out.Add('  ' + (Limit-QaText -Text $line.Trim() -Max 260))
        }
        if ($count -eq 0) { [void]$out.Add('  (none)') }
    }
    [System.IO.File]::WriteAllLines($OutPath, $out.ToArray(), (New-Object System.Text.UTF8Encoding($false)))
}

function New-QaTextCard {
    param([string[]]$Lines, [string]$OutPng, [int]$Width = 1280, [int]$Height = 720)
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap($Width, $Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.SmoothingMode = 'AntiAlias'
        $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
        $g.Clear([System.Drawing.Color]::FromArgb(27, 28, 31))
        $white = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White)
        $font = New-Object System.Drawing.Font('Segoe UI', 34, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
        $y = ($Height / 2) - (@($Lines).Count * 26)
        foreach ($line in @($Lines)) {
            $size = $g.MeasureString($line, $font)
            $g.DrawString($line, $font, $white, (($Width - $size.Width) / 2), $y)
            $y += 52
        }
        $bmp.Save($OutPng, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $g.Dispose()
        $bmp.Dispose()
    }
}
