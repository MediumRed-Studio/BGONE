# QaReport.ps1 - JUnit XML report parser for BGONE QA runners (PowerShell 5.1 ONLY).

function Read-QaTestReport {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    if (-not (Test-Path -LiteralPath $Path)) { return $null }

    $xml = $null
    try {
        $raw = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop
        $xml = [xml]$raw
    } catch {
        return [pscustomobject]@{
            Path = $Path; Suites = @(); Total = 0; Passed = 0; Failed = 0; Skipped = 0; Errored = 0
            Malformed = $true
            ParseError = $_.Exception.Message
        }
    }

    $suiteNodes = @($xml.SelectNodes('//testsuite'))
    if ($suiteNodes.Count -eq 0) {
        return [pscustomobject]@{
            Path = $Path; Suites = @(); Total = 0; Passed = 0; Failed = 0; Skipped = 0; Errored = 0
            Malformed = $true
            ParseError = 'No <testsuite> element found.'
        }
    }

    $suites = New-Object System.Collections.ArrayList
    $tTotal = 0; $tFailed = 0; $tSkipped = 0; $tErrored = 0

    foreach ($node in $suiteNodes) {
        $name = Get-QaXmlAttr -Node $node -Name 'name' -Default '(unnamed)'
        $cases = @($node.SelectNodes('testcase'))

        $failedNames = New-Object System.Collections.ArrayList
        $count = 0; $failed = 0; $skipped = 0; $errored = 0

        if ($cases.Count -gt 0) {
            foreach ($c in $cases) {
                $count++
                $cname = Get-QaXmlAttr -Node $c -Name 'name' -Default '(unnamed case)'
                $isFail = (@($c.SelectNodes('failure')).Count -gt 0)
                $isErr  = (@($c.SelectNodes('error')).Count -gt 0)
                $isSkip = (@($c.SelectNodes('skipped')).Count -gt 0)
                if ($isFail) { $failed++ }
                if ($isErr) { $errored++ }
                if ($isSkip) { $skipped++ }
                if ($isFail -or $isErr) {
                    $msg = ''
                    $attrMsg = ''
                    $type = ''
                    $detail = $c.SelectSingleNode('failure')
                    if ($null -eq $detail) { $detail = $c.SelectSingleNode('error') }
                    if ($null -ne $detail) {
                        $attrMsg = Get-QaXmlAttr -Node $detail -Name 'message' -Default ''
                        $type    = Get-QaXmlAttr -Node $detail -Name 'type'    -Default ''
                        $msg     = $attrMsg
                        if ([string]::IsNullOrWhiteSpace($msg)) {
                            $inner = $detail.InnerText
                            if (-not [string]::IsNullOrWhiteSpace($inner)) { $msg = $inner.Trim() }
                        }
                    }
                    $elapsed = Get-QaCaseElapsedSec -Node $c
                    $budget  = Get-QaTimeoutBudgetMs -Message $attrMsg
                    $isTimeout = ($budget -gt 0)
                    if ($type -like '*Timeout*') { $isTimeout = $true }
                    [void]$failedNames.Add([pscustomobject]@{
                        Suite      = $name
                        Case       = $cname
                        Message    = $msg
                        Type       = $type
                        ElapsedSec = $elapsed
                        BudgetMs   = $budget
                        IsTimeout  = $isTimeout
                    })
                }
            }
        } else {
            $count   = [int](Get-QaXmlAttr -Node $node -Name 'tests'    -Default '0')
            $failed  = [int](Get-QaXmlAttr -Node $node -Name 'failures' -Default '0')
            $skipped = [int](Get-QaXmlAttr -Node $node -Name 'skipped'  -Default '0')
            $errored = [int](Get-QaXmlAttr -Node $node -Name 'errors'   -Default '0')
        }

        $passed = $count - $failed - $errored - $skipped
        if ($passed -lt 0) { $passed = 0 }

        [void]$suites.Add([pscustomobject]@{
            Name        = $name
            Total       = $count
            Passed      = $passed
            Failed      = $failed
            Skipped     = $skipped
            Errored     = $errored
            FailedCases = @($failedNames.ToArray())
        })

        $tTotal += $count; $tFailed += $failed; $tSkipped += $skipped; $tErrored += $errored
    }

    $tPassed = $tTotal - $tFailed - $tErrored - $tSkipped
    if ($tPassed -lt 0) { $tPassed = 0 }

    return [pscustomobject]@{
        Path       = $Path
        Suites     = @($suites.ToArray())
        Total      = $tTotal
        Passed     = $tPassed
        Failed     = $tFailed
        Skipped    = $tSkipped
        Errored    = $tErrored
        Malformed  = $false
        ParseError = ''
    }
}

function Get-QaXmlAttr {
    param($Node, [string]$Name, [string]$Default = '')
    if ($null -eq $Node) { return $Default }
    $attr = $Node.Attributes[$Name]
    if ($null -eq $attr) { return $Default }
    if ($null -eq $attr.Value) { return $Default }
    return $attr.Value
}

function Get-QaTimeoutBudgetMs {
    param([string]$Message)
    if ([string]::IsNullOrWhiteSpace($Message)) { return 0 }
    if ($Message -match 'Timed\s+out\s+after\s*(?<ms>\d+)\s*ms') { return [int]$Matches['ms'] }
    return 0
}

function Get-QaCaseElapsedSec {
    param($Node)
    $raw = Get-QaXmlAttr -Node $Node -Name 'time' -Default ''
    if ([string]::IsNullOrWhiteSpace($raw)) { return -1.0 }
    $v = 0.0
    if ([double]::TryParse($raw, [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$v)) {
        return $v
    }
    return -1.0
}

function Format-QaCaseTiming {
    param([double]$ElapsedSec, [int]$BudgetMs)
    if ($ElapsedSec -lt 0) {
        if ($BudgetMs -le 0) { return 'no timing recorded' }
        return ('budget ' + [string][math]::Round($BudgetMs / 1000.0, 2) + 's (elapsed not recorded)')
    }
    $took = 'took ' + [string][math]::Round($ElapsedSec, 2) + 's'
    if ($BudgetMs -le 0) { return ($took + ' (no budget recorded)') }
    $budgetSec = $BudgetMs / 1000.0
    $pct = [int][math]::Round((100.0 * $ElapsedSec) / $budgetSec)
    return ($took + ' against a ' + [string][math]::Round($budgetSec, 2) + 's budget (' + [string]$pct + '% of budget)')
}

function Format-QaTestTable {
    param($Report)
    $lines = New-Object System.Collections.ArrayList
    if ($null -eq $Report) {
        [void]$lines.Add('  (no test report)')
        return @($lines.ToArray())
    }
    if ($Report.Malformed) {
        [void]$lines.Add('  (test report malformed: ' + $Report.ParseError + ')')
        return @($lines.ToArray())
    }

    $w = 8
    foreach ($s in $Report.Suites) { if ($s.Name.Length -gt $w) { $w = $s.Name.Length } }
    if ($w -gt 60) { $w = 60 }

    [void]$lines.Add(('  ' + 'SUITE'.PadRight($w) + '  ' + 'CASES'.PadLeft(6) + '  ' + 'PASS'.PadLeft(6) + '  ' + 'FAIL'.PadLeft(6) + '  ' + 'SKIP'.PadLeft(6) + '  ' + 'ERR'.PadLeft(6)))
    [void]$lines.Add(('  ' + ('-' * $w) + '  ' + ('-' * 6) + '  ' + ('-' * 6) + '  ' + ('-' * 6) + '  ' + ('-' * 6) + '  ' + ('-' * 6)))
    foreach ($s in $Report.Suites) {
        $n = $s.Name
        if ($n.Length -gt $w) { $n = $n.Substring(0, $w) }
        [void]$lines.Add(('  ' + $n.PadRight($w) + '  ' + ([string]$s.Total).PadLeft(6) + '  ' + ([string]$s.Passed).PadLeft(6) + '  ' + ([string]$s.Failed).PadLeft(6) + '  ' + ([string]$s.Skipped).PadLeft(6) + '  ' + ([string]$s.Errored).PadLeft(6)))
    }
    [void]$lines.Add(('  ' + ('-' * $w) + '  ' + ('-' * 6) + '  ' + ('-' * 6) + '  ' + ('-' * 6) + '  ' + ('-' * 6) + '  ' + ('-' * 6)))
    [void]$lines.Add(('  ' + 'TOTAL'.PadRight($w) + '  ' + ([string]$Report.Total).PadLeft(6) + '  ' + ([string]$Report.Passed).PadLeft(6) + '  ' + ([string]$Report.Failed).PadLeft(6) + '  ' + ([string]$Report.Skipped).PadLeft(6) + '  ' + ([string]$Report.Errored).PadLeft(6)))

    $failing = New-Object System.Collections.ArrayList
    foreach ($s in $Report.Suites) {
        foreach ($f in $s.FailedCases) { [void]$failing.Add($f) }
    }
    if ($failing.Count -gt 0) {
        [void]$lines.Add('')
        [void]$lines.Add('  FAILING CASES:')
        foreach ($f in $failing) {
            $suffix = ''
            if ($f.Message) { $suffix = ' - ' + $f.Message }
            [void]$lines.Add(('    ' + $f.Suite + '.' + $f.Case + $suffix))
            if ($f.PSObject.Properties['IsTimeout'] -and $f.IsTimeout) {
                [void]$lines.Add(('      TIMING: ' + (Format-QaCaseTiming -ElapsedSec ([double]$f.ElapsedSec) -BudgetMs ([int]$f.BudgetMs))))
            }
        }
    }
    return @($lines.ToArray())
}
