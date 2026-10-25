# Serial probe runner. WAIT <seconds> is a host delay, never a device command.
[CmdletBinding(SupportsShouldProcess=$true)]
param(
    [string]$Port,
    [string]$Script,
    [string[]]$Command,
    [string]$LogPath,
    [ValidateRange(1,86400)][int]$DurationSeconds = 400,
    [ValidateRange(0,60)][double]$DelaySeconds = 1,
    [switch]$DryRun,
    [string]$SummaryLog
)
$ErrorActionPreference = 'Stop'
if ($SummaryLog) {
    if ($Port -or $Script -or $Command -or $LogPath) { throw 'SummaryLog cannot be combined with session arguments.' }
    $lines = @(Get-Content -LiteralPath $SummaryLog)
    $metrics = @($lines | Where-Object { $_ -match '(?:^| RX )(?:METRIC|TRACK) ' })
    foreach ($key in @('heap','largest','min_heap','producer_stack','sink_stack','loop_stack','underrun_frames','underrun_calls')) {
        $values = @($metrics | ForEach-Object {
            if ($_ -match "(?:^| )$key=(\d+)(?: |$)") { [long]$Matches[1] }
        })
        if ($values.Count) {
            $m = $values | Measure-Object -Minimum -Maximum
            Write-Output "$key min=$($m.Minimum) max=$($m.Maximum) first=$($values[0]) last=$($values[-1])"
        } else { Write-Output "$key no samples" }
    }
    Write-Output 'M4A statuses (sample counts, not track counts):'
    $metrics | ForEach-Object {
        if ($_ -match '(?:^| )m4a=([^ ]+)') { $Matches[1] }
    } | Group-Object | Sort-Object Name | ForEach-Object { Write-Output "$($_.Name) count=$($_.Count)" }
    Write-Output 'ERR lines:'
    $lines | Where-Object { $_ -match '(?:^| RX )ERR(?: |$)' } | Write-Output
    return
}
# Explicit checks avoid an unattended Mandatory parameter prompt in PowerShell 5.1.
if (!$Port -or $Port -notmatch '^COM[1-9][0-9]*$') { throw 'An explicit -Port COM<number> is required; no discovery or default port.' }
if (!$LogPath) { throw '-LogPath is required.' }
if ([bool]$Script -eq [bool]$Command) { throw 'Supply exactly one of -Script or -Command.' }
$inputLines = if ($Script) { @(Get-Content -LiteralPath $Script -Encoding UTF8) } else { @($Command) }
$plan = @(); $at = 2.0
foreach ($line in $inputLines) {
    $line = $line.Trim()
    if (!$line -or $line.StartsWith('#')) { continue }
    if ($line -match '^WAIT ([0-9]+(?:\.[0-9]+)?)$') {
        $at += [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture)
        continue
    }
    if ($line -match '[\r\n]' -or $line.Length -gt 126) { throw 'Commands must be single lines of at most 126 characters.' }
    $kind = if ($line.StartsWith('HOST USER ACTION: ')) { 'HOST' } else { 'TX' }
    if ($kind -eq 'HOST') { $line = $line.Substring(5) }
    $plan += [pscustomobject]@{ At=$at; Text=$line; Kind=$kind }
    $at += $DelaySeconds
}
if (!$plan.Count) { throw 'No commands supplied.' }
if ($plan[-1].At -ge $DurationSeconds) { throw 'DurationSeconds must exceed the final scheduled command.' }
if ($DryRun -or $WhatIfPreference) {
    Write-Output "DRY RUN: $Port 115200; duration=$DurationSeconds; log=$LogPath; no port opened"
    $plan | ForEach-Object { Write-Output ("{0:F3}s {1} {2}" -f $_.At,$_.Kind,$_.Text) }
    return
}
if (!$PSCmdlet.ShouldProcess($Port, 'Open serial port and send probe commands')) { return }
$serial = $null; $writer = $null; $pending = ''; $index = 0
try {
    # CreateNew preserves existing evidence; DryRun never creates a file.
    $stream = [IO.File]::Open($LogPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    $writer = New-Object IO.StreamWriter($stream, (New-Object Text.UTF8Encoding($false)))
    $writer.AutoFlush = $true
    $serial = New-Object IO.Ports.SerialPort($Port,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
    $serial.DtrEnable = $false; $serial.RtsEnable = $false
    $serial.Encoding = New-Object Text.UTF8Encoding($false); $serial.NewLine = "`n"; $serial.WriteTimeout = 1000
    $serial.Open()
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt $DurationSeconds) {
        if (Test-Path .tools/cy11c-emergency-mute) { $serial.WriteLine("mute"); $serial.WriteLine("stop"); $serial.WriteLine("stats"); Start-Sleep -Milliseconds 500; $index = $plan.Count; $DurationSeconds = 0 }
        $pending += $serial.ReadExisting()
        while (($newline = $pending.IndexOf("`n")) -ge 0) {
            $received = $pending.Substring(0,$newline).TrimEnd("`r")
            $pending = $pending.Substring($newline+1)
            $entry = [DateTime]::UtcNow.ToString('o') + ' RX ' + $received
            $writer.WriteLine($entry); Write-Output $entry
        }
        if ($index -lt $plan.Count -and $watch.Elapsed.TotalSeconds -ge $plan[$index].At) {
            if ($plan[$index].Kind -eq 'HOST') {
                Write-Output $plan[$index].Text
                $receipt = & $env:ORCA_CLI_COMMAND orchestration send --from orca_session_id:7744f60a-46c9-4514-afe4-2bc882c357c5 --type status --subject $plan[$index].Text --body $plan[$index].Text --task-id task_42b932d6332f --dispatch-id ctx_79c9ef946a95 --json
                $receipt | Write-Output
                if (!(($receipt -join "`n" | ConvertFrom-Json).ok)) { throw 'Coordinator prompt delivery failed; stopping safely.' }
            } else { $serial.WriteLine($plan[$index].Text) }
            $entry = [DateTime]::UtcNow.ToString('o') + ' ' + $plan[$index].Kind + ' ' + $plan[$index].Text
            $writer.WriteLine($entry); Write-Output $entry
            $index++
        }
        Start-Sleep -Milliseconds 20
    }
} finally {
    if ($serial) {
        try { if ($serial.IsOpen) {
            foreach ($safe in @('mute','stop','stats')) {
                $serial.WriteLine($safe)
                if ($writer) { $writer.WriteLine([DateTime]::UtcNow.ToString('o') + ' TX ' + $safe) }
                Start-Sleep -Milliseconds 200
            }
            Start-Sleep -Milliseconds 500
            $pending += $serial.ReadExisting()
        } }
        catch { Write-Warning "Final serial read failed: $_" }
        finally { $serial.Dispose() }
    }
    if ($writer) {
        foreach ($tail in ($pending -split "`n")) {
            if ($tail) { $writer.WriteLine([DateTime]::UtcNow.ToString('o') + ' RX ' + $tail.TrimEnd("`r")) }
        }
        $writer.Dispose()
    }
}
