[CmdletBinding()]
param(
    [ValidateSet('w10', 'w11')]
    [string]$OsFamily = '',

    [switch]$EnableUsbIp
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$packageRoot = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$installer = Join-Path $packageRoot 'Install-YdyunGuest.ps1'
$driverRoot = Join-Path $packageRoot 'payload\virtio-win'
$spiceMsi = @(Get-ChildItem -LiteralPath (Join-Path $packageRoot 'payload\spice-agent') `
    -Filter '*.msi' -File)

if ($spiceMsi.Count -ne 1) {
    throw "payload\spice-agent 中必须且只能有一个 SPICE agent MSI，当前数量：$($spiceMsi.Count)"
}
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "缺少安装器：$installer"
}
if (-not (Test-Path -LiteralPath $driverRoot -PathType Container)) {
    throw "缺少 VirtIO payload：$driverRoot"
}

if ([string]::IsNullOrWhiteSpace($OsFamily)) {
    $build = [int](Get-CimInstance Win32_OperatingSystem).BuildNumber
    $candidate = if ($build -ge 22000) { 'w11' } else { 'w10' }
    $candidateDisplay = Join-Path $driverRoot "qxldod\$candidate\amd64\qxldod.inf"
    $candidateGpu = Join-Path $driverRoot "viogpudo\$candidate\amd64\viogpudo.inf"
    $OsFamily = if ((Test-Path -LiteralPath $candidateDisplay -PathType Leaf) -or
        (Test-Path -LiteralPath $candidateGpu -PathType Leaf)) {
        $candidate
    }
    else {
        'w10'
    }
}

$arguments = @{
    DriverRoot = $driverRoot
    OsFamily = $OsFamily
    SpiceAgentMsi = $spiceMsi[0].FullName
    SpiceAgentSha256 = (Get-FileHash -LiteralPath $spiceMsi[0].FullName -Algorithm SHA256).Hash
}

if ($EnableUsbIp) {
    $usbInstallers = @(Get-ChildItem -LiteralPath (Join-Path $packageRoot 'payload\usbip') `
        -Filter '*.exe' -File)
    if ($usbInstallers.Count -ne 1) {
        throw "启用 USB/IP 时 payload\usbip 中必须且只能有一个安装器，当前数量：$($usbInstallers.Count)"
    }
    $arguments.EnableUsbIp = $true
    $arguments.UsbIpInstaller = $usbInstallers[0].FullName
    $arguments.UsbIpSha256 = (Get-FileHash -LiteralPath $usbInstallers[0].FullName -Algorithm SHA256).Hash
}

& $installer @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
