# Registers (or with -Remove, unregisters) the MotionVR Bridge SteamVR driver
# that sits next to this script, using SteamVR's own vrpathreg tool.
param([switch]$Remove)

$ErrorActionPreference = 'Stop'
$driverDir = Join-Path $PSScriptRoot 'driver\motionvrbridge'
$pathsFile = Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath'

if (-not (Test-Path $pathsFile)) {
    Write-Host 'SteamVR was not found. Start SteamVR once, then run this script again.'
    exit 1
}

$runtime = (Get-Content $pathsFile -Raw | ConvertFrom-Json).runtime | Select-Object -First 1
$vrpathreg = Join-Path $runtime 'bin\win64\vrpathreg.exe'
if (-not (Test-Path $vrpathreg)) {
    Write-Host "vrpathreg.exe not found at $vrpathreg"
    exit 1
}

$command = if ($Remove) { 'removedriver' } else { 'adddriver' }
& $vrpathreg $command $driverDir
Write-Host "SteamVR driver ${command}: $driverDir"
exit $LASTEXITCODE
