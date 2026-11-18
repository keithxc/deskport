param(
    [Parameter(Mandatory=$true)][string]$FixtureRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [Parameter(Mandatory=$true)][string]$TaskId,
    [ValidateSet('offscreen','windows')][string]$Platform = 'offscreen',
    [ValidateSet('software','native')][string]$Renderer = 'software',
    [ValidatePattern('^\d{3,4}(,\d{3,4})*$')][string]$Widths = '640,1120',
    [ValidateRange(360,1080)][int]$Height = 760
)
$ErrorActionPreference = 'Stop'
$fixture = (Resolve-Path $FixtureRoot).Path
$output = [System.IO.Path]::GetFullPath($OutputRoot)
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $env:USERPROFILE 'mygit/build/deskport'))
if (!$output.StartsWith($buildRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputRoot must be beneath the DeskPort build workspace'
}
$binary = Join-Path $fixture 'desktop-typography.exe'
if (!(Test-Path $binary) -or !(Test-Path (Join-Path $fixture 'gui/SettingsHome.qml'))) { throw 'Incomplete fixture' }
$sessionId = (Get-Process -Id $PID).SessionId
if ($Platform -eq 'windows' -and ($sessionId -eq 0 -or ![Environment]::UserInteractive)) {
    throw 'Native window QA requires the owned interactive user session; use offscreen for SSH font checks'
}
$lock = Join-Path $buildRoot 'locks/windows-ui.lock'
New-Item -ItemType Directory -Path (Split-Path $lock) -Force | Out-Null
# Atomic reservation. An existing lock is never removed or adopted.
New-Item -ItemType Directory -Path $lock | Out-Null
$saved = @{}
$keys = @('QT_QPA_PLATFORM','QT_SCALE_FACTOR','QT_SCREEN_SCALE_FACTORS','QT_QUICK_BACKEND',
          'QT_QUICK_CONTROLS_STYLE','QML_DISABLE_DISK_CACHE','TEST_GUI_DIR',
          'DESKPORT_TYPOGRAPHY_OUTPUT','DESKPORT_TYPOGRAPHY_SETTINGS','DESKPORT_TYPOGRAPHY_DPR',
          'DESKPORT_TYPOGRAPHY_WIDTHS','DESKPORT_TYPOGRAPHY_HEIGHT')
try {
    @{thread=$TaskId; pid=$PID; session=$sessionId; platform=$Platform; renderer=$Renderer; time=(Get-Date).ToUniversalTime().ToString('o'); task='isolated desktop typography'} |
        ConvertTo-Json | Set-Content (Join-Path $lock 'owner.json')
    foreach ($key in $keys) { $saved[$key] = [Environment]::GetEnvironmentVariable($key) }
    $env:QT_QPA_PLATFORM = $Platform
    $env:QT_SCREEN_SCALE_FACTORS = '1'
    if ($Renderer -eq 'software') { $env:QT_QUICK_BACKEND = 'software' } else { $env:QT_QUICK_BACKEND = $null }
    $env:QT_QUICK_CONTROLS_STYLE = 'Material'
    $env:QML_DISABLE_DISK_CACHE = '1'
    $env:TEST_GUI_DIR = Join-Path $fixture 'gui'
    $env:DESKPORT_TYPOGRAPHY_WIDTHS = $Widths
    $env:DESKPORT_TYPOGRAPHY_HEIGHT = "$Height"
    $env:QT_SCALE_FACTOR = '1'
    $baseDpr = [double](& $binary --print-dpr)
    if ($LASTEXITCODE -ne 0 -or $baseDpr -le 0) { throw 'DPR probe failed' }
    foreach ($scale in @(1,2)) {
        $env:QT_SCALE_FACTOR = ($scale / $baseDpr).ToString([Globalization.CultureInfo]::InvariantCulture)
        $env:DESKPORT_TYPOGRAPHY_DPR = "$scale"
        $env:DESKPORT_TYPOGRAPHY_OUTPUT = Join-Path $output "scale-$scale"
        $env:DESKPORT_TYPOGRAPHY_SETTINGS = Join-Path $output "settings-$scale"
        New-Item -ItemType Directory -Path $env:DESKPORT_TYPOGRAPHY_OUTPUT -Force | Out-Null
        # Qt writes informational messages to stderr. Capture them directly so
        # Windows PowerShell 5 does not turn them into terminating ErrorRecords.
        $start = New-Object System.Diagnostics.ProcessStartInfo
        $start.FileName = $binary
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $process = New-Object System.Diagnostics.Process
        $process.StartInfo = $start
        try {
            if (!$process.Start()) { throw 'Could not start fixture' }
            $stdout = $process.StandardOutput.ReadToEndAsync()
            $stderr = $process.StandardError.ReadToEndAsync()
            if (!$process.WaitForExit(300000)) { throw 'Fixture timed out; reconcile its owned PID before retrying' }
            $log = $stdout.Result + $stderr.Result
            $log | Set-Content -Encoding UTF8 (Join-Path $output "scale-$scale.log")
            Write-Host $log
            if ($process.ExitCode -ne 0) { throw "Typography fixture failed: $($process.ExitCode)" }
        } finally { $process.Dispose() }
    }
} finally {
    foreach ($key in $keys) { if ($saved.ContainsKey($key)) { [Environment]::SetEnvironmentVariable($key, $saved[$key]) } }
    $children = @(Get-Process desktop-typography -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $binary })
    if ($children.Count -gt 0) {
        Write-Warning "Owned fixture PIDs $($children.Id -join ', ') still running; retain the lock for reconciliation"
    } else {
        Remove-Item (Join-Path $lock 'owner.json') -ErrorAction SilentlyContinue
        Remove-Item $lock
    }
}
