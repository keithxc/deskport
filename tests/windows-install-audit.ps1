param(
    [Parameter(Mandatory=$true)][string]$OutputFile,
    [string]$Baseline,
    [string]$InstallDirectory = (Join-Path $env:ProgramFiles 'DeskPort'),
    [string]$DataDirectory = (Join-Path $env:LOCALAPPDATA 'DeskPort\DeskPort'),
    [switch]$AllowRunning
)
# Read-only lifecycle evidence. Never export identity bytes, peer addresses,
# process command lines, clipboard data or the machine/user name.
$ErrorActionPreference = 'Stop'
function Digest([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function File-Digest([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
    return $null
}
$processes = @(Get-CimInstance Win32_Process | Where-Object {
    # A medium user may not see a SYSTEM guardian's image through CIM. An
    # inaccessible same-name process must block a quiescent snapshot too.
    $_.Name -in @('DeskPort.exe','deskport-host.exe','deskport-display.exe','deskport-display-recovery.exe') -and
    (!$_.ExecutablePath -or $_.ExecutablePath.StartsWith($InstallDirectory.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase))
})
if ($processes.Count -and !$AllowRunning) { throw 'Quiesce this installation in an authorized window before a preservation snapshot.' }
$preserved = [ordered]@{}
$key = Get-Item 'HKCU:\Software\DeskPort\DeskPort' -ErrorAction SilentlyContinue
foreach ($name in 'uniqueid','certificate','key') {
    $value = if ($key) { $key.GetValue($name, $null) } else { $null }
    $preserved['viewer-' + $name] = if ($null -eq $value) { $null } elseif ($value -is [byte[]]) { Digest $value } else { Digest ([Text.Encoding]::UTF8.GetBytes([string]$value)) }
}
foreach ($path in 'binding\peers.json','host\credentials\cert.pem','host\credentials\key.pem','host\state.json') {
    $preserved[$path.Replace('\','/')] = File-Digest (Join-Path $DataDirectory $path)
}
$binaries = [ordered]@{}
foreach ($path in 'DeskPort.exe','deskport-maintenance.exe','deskport-driver-setup.exe','host\deskport-host.exe',
    'host\deskport-display.exe','host\deskport-display-recovery.exe','host\deskport-display-service.exe') {
    $binaries[$path.Replace('\','/')] = File-Digest (Join-Path $InstallDirectory $path)
}
$service = Get-CimInstance Win32_Service -Filter "Name='DeskPortDisplayService'"
$scm = if ($service) {
    $sddl = & "$env:SystemRoot\System32\sc.exe" sdshow DeskPortDisplayService
    if ($LASTEXITCODE -ne 0) { throw 'Cannot query service DACL' }
    $failure = & "$env:SystemRoot\System32\sc.exe" qfailure DeskPortDisplayService
    if ($LASTEXITCODE -ne 0) { throw 'Cannot query SCM failure actions' }
    [ordered]@{
        state=$service.State; automatic=($service.StartMode -eq 'Auto'); system=($service.StartName -eq 'LocalSystem')
        exactImage=($service.PathName -eq "`"$InstallDirectory\host\deskport-display-service.exe`" --service")
        dacl=($sddl -join "`n").Trim(); failureActions=($failure -join "`n").Trim()
    }
} else { $null }
$result = [ordered]@{
    schema=1; timeUtc=[DateTime]::UtcNow.ToString('o'); quiescent=($processes.Count -eq 0)
    preserved=$preserved; binaries=$binaries; service=$scm; preservationCompared=$false
}
if ($Baseline) {
    $old = Get-Content -LiteralPath $Baseline -Raw | ConvertFrom-Json
    if ($old.schema -ne 1 -or !$old.quiescent -or !$result.quiescent) { throw 'Preservation comparison requires two quiescent schema-1 snapshots' }
    # getUniqueId() persists the viewer ID lazily. A host-only profile can
    # legitimately omit it; its null/present state is still compared below.
    # The host state contains the hosting identity and must already exist.
    foreach ($required in 'viewer-certificate','viewer-key','binding/peers.json',
        'host/credentials/cert.pem','host/credentials/key.pem','host/state.json') {
        if (!$old.preserved.$required) { throw "Baseline has no $required; initialize a synthetic test identity and pairing first" }
    }
    foreach ($name in $preserved.Keys) {
        if ($null -eq $old.preserved.PSObject.Properties[$name]) { throw "Baseline omitted $name" }
        if ($old.preserved.$name -cne $preserved[$name]) { throw "Preserved state changed: $name" }
    }
    $result.preservationCompared = $true
}
$output = [IO.Path]::GetFullPath($OutputFile)
if (Test-Path -LiteralPath $output) { throw 'Use a new immutable audit output file' }
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $output -Encoding UTF8
if ($result.preservationCompared) { 'PASS identity, certificate, private-key and saved-peer hashes preserved' }
else { 'Snapshot saved; this alone is not a lifecycle or preservation pass' }
