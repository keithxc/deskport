param([Parameter(Mandatory=$true)][string]$OutputDirectory)
# Portable tests of the actual audit with synthetic files and CIM/registry doubles.
# No Windows registry, service, installed app, user identity or network access.
$ErrorActionPreference = 'Stop'
$audit = Join-Path $PSScriptRoot 'windows-install-audit.ps1'
$tokens = $null; $errors = $null
$null = [Management.Automation.Language.Parser]::ParseFile($audit, [ref]$tokens, [ref]$errors)
if ($errors) { throw ($errors | Out-String) }
$root = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
$data = Join-Path $root 'data'
foreach ($path in 'binding/peers.json','host/credentials/cert.pem','host/credentials/key.pem','host/state.json') {
    $file = Join-Path $data $path
    $null = New-Item -ItemType Directory -Force (Split-Path $file)
    [IO.File]::WriteAllText($file, 'synthetic-' + $path)
}
$fixtureProcesses = @()
$fixtureIdentity = [pscustomobject]@{ Values=@{uniqueid='synthetic-id';certificate=[byte[]](1,2,3);key=[byte[]](4,5,6)} }
$fixtureIdentity | Add-Member ScriptMethod GetValue { param($name,$fallback) return $this.Values[$name] }
function Get-Item { param($Path,$ErrorAction) return $fixtureIdentity }
function Get-CimInstance {
    param($ClassName,$Filter)
    if ($ClassName -eq 'Win32_Process') { return $fixtureProcesses }
    if ($ClassName -ne 'Win32_Service') { throw 'Unexpected CIM class' }
    return $null
}
$common = @{InstallDirectory=(Join-Path $root 'installation');DataDirectory=$data}
$baseline = Join-Path $root 'baseline.json'
& $audit @common -OutputFile $baseline
& $audit @common -Baseline $baseline -OutputFile (Join-Path $root 'same.json')
function Must-Fail([scriptblock]$Action,[string]$Message) {
    try { & $Action } catch {
        if ($_.Exception.Message -notlike $Message) { throw }
        return
    }
    throw "Expected rejection: $Message"
}
$fixtureIdentity.Values.Remove('uniqueid')
$hostOnly = Join-Path $root 'host-only.json'
& $audit @common -OutputFile $hostOnly
& $audit @common -Baseline $hostOnly -OutputFile (Join-Path $root 'host-only-compared.json')
$hostStateFile = Join-Path $data 'host/state.json'
$hostState = [IO.File]::ReadAllText($hostStateFile)
Remove-Item -LiteralPath $hostStateFile
$noHostState = Join-Path $root 'no-host-state.json'
& $audit @common -OutputFile $noHostState
Must-Fail { & $audit @common -Baseline $noHostState -OutputFile (Join-Path $root 'no-host-state-compared.json') } '*Baseline has no host/state.json*'
[IO.File]::WriteAllText($hostStateFile, $hostState)
# Absence is legitimate, but changing it is still a strict preservation failure.
$fixtureIdentity.Values.uniqueid = 'new-synthetic-viewer-id'
Must-Fail { & $audit @common -Baseline $hostOnly -OutputFile (Join-Path $root 'viewer-id-added.json') } '*Preserved state changed*'
$fixtureIdentity.Values.uniqueid = 'synthetic-id'
[IO.File]::WriteAllText((Join-Path $data 'binding/peers.json'), 'changed-synthetic-pairing')
Must-Fail { & $audit @common -Baseline $baseline -OutputFile (Join-Path $root 'changed.json') } '*Preserved state changed*'
$fixtureIdentity = $null
Remove-Item -Recurse $data
$empty = Join-Path $root 'empty.json'
& $audit @common -OutputFile $empty
Must-Fail { & $audit @common -Baseline $empty -OutputFile (Join-Path $root 'empty-compared.json') } '*Baseline has no*'
$fixtureProcesses = @([pscustomobject]@{Name='deskport-display-recovery.exe';ExecutablePath=$null})
Must-Fail { & $audit @common -OutputFile (Join-Path $root 'hidden-guardian.json') } '*Quiesce this installation*'
& $audit @common -AllowRunning -OutputFile (Join-Path $root 'running-inventory.json')
Must-Fail { & $audit @common -AllowRunning -Baseline $baseline -OutputFile (Join-Path $root 'running-compared.json') } '*requires two quiescent*'
'PASS audit parser, synthetic retention, lazy viewer ID, missing host state, changed pairing, empty baseline, hidden guardian and running-inventory rejection'
