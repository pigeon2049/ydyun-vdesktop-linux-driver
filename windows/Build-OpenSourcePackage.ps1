[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string]$VirtioRoot,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$SpiceAgentMsi,

    [string]$UsbIpInstaller = '',

    [ValidateSet('w10', 'w11')]
    [string]$OsFamily = 'w10',

    [string]$OutputRoot = '.\dist\ydyun-windows-open-source'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = [IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputRoot))
$resourcesRoot = Join-Path $output 'resources'
$payloadRoot = Join-Path $resourcesRoot 'payload'
$driverOutput = Join-Path $payloadRoot 'virtio-win'

if (Test-Path -LiteralPath $output) {
    throw "输出目录已存在，为避免覆盖旧包请换用 -OutputRoot：$output"
}
New-Item -ItemType Directory -Force -Path $driverOutput | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $payloadRoot 'spice-agent') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $payloadRoot 'usbip') | Out-Null

function Find-DriverInf {
    param([Parameter(Mandatory = $true)][string]$Name)

    $matches = @(Get-ChildItem -LiteralPath $VirtioRoot -Filter $Name -File -Recurse |
        Where-Object { $_.FullName -match "[\\/]$([regex]::Escape($OsFamily))[\\/]amd64[\\/]" })
    if ($matches.Count -ne 1) {
        throw "$Name 在 $OsFamily/amd64 下必须找到一个，实际找到 $($matches.Count) 个。"
    }
    $content = Get-Content -LiteralPath $matches[0].FullName -Raw
    if ($matches[0].FullName -match '(?i)(zte|ice)' -or
        $content -match '(?i)(ZTE CORPORATION|ZTEGuestOS|ICE virtual)') {
        throw "拒绝原厂或重签名驱动：$($matches[0].FullName)"
    }
    return $matches[0]
}

function Copy-DriverPackage {
    param([Parameter(Mandatory = $true)][System.IO.FileInfo]$Inf)

    $driverName = [IO.Path]::GetFileNameWithoutExtension($Inf.Name)
    $destination = Join-Path $driverOutput "$driverName\$OsFamily"
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -LiteralPath $Inf.DirectoryName -Destination $destination -Recurse -Force
}

foreach ($driverName in @('qxldod.inf', 'vioser.inf')) {
    $inf = Find-DriverInf $driverName
    Copy-DriverPackage $inf
}

foreach ($optionalDriver in @('viogpudo.inf', 'vioinput.inf')) {
    $matches = @(Get-ChildItem -LiteralPath $VirtioRoot -Filter $optionalDriver -File -Recurse |
        Where-Object { $_.FullName -match "[\\/]$([regex]::Escape($OsFamily))[\\/]amd64[\\/]" })
    if ($matches.Count -eq 1) {
        $content = Get-Content -LiteralPath $matches[0].FullName -Raw
        if ($content -match '(?i)(ZTE CORPORATION|ZTEGuestOS|ICE virtual)') {
            throw "拒绝原厂或重签名驱动：$($matches[0].FullName)"
        }
        Copy-DriverPackage $matches[0]
    }
    elseif ($matches.Count -gt 1) {
        throw "$optionalDriver 找到多个候选目录。"
    }
}

$spice = (Resolve-Path -LiteralPath $SpiceAgentMsi).Path
Copy-Item -LiteralPath $spice -Destination (Join-Path $payloadRoot 'spice-agent') -Force

if (-not [string]::IsNullOrWhiteSpace($UsbIpInstaller)) {
    $usb = (Resolve-Path -LiteralPath $UsbIpInstaller).Path
    Copy-Item -LiteralPath $usb -Destination (Join-Path $payloadRoot 'usbip') -Force
}

& cmake -S (Join-Path $repoRoot 'windows') -B (Join-Path $repoRoot 'build-windows') -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败。' }
& cmake --build (Join-Path $repoRoot 'build-windows') --config Release
if ($LASTEXITCODE -ne 0) { throw 'Windows 诊断工具构建失败。' }

New-Item -ItemType Directory -Force -Path $resourcesRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'windows\Install-YdyunGuest.ps1') -Destination $resourcesRoot
Copy-Item -LiteralPath (Join-Path $repoRoot 'windows\Install-YdyunOpenGuest.ps1') -Destination $resourcesRoot
Copy-Item -LiteralPath (Join-Path $repoRoot 'windows\dependencies.lock.json') -Destination $resourcesRoot
Copy-Item -LiteralPath (Join-Path $repoRoot 'windows\Install-YdyunOpenGuest.bat') -Destination $output
Copy-Item -LiteralPath (Join-Path $repoRoot 'build-windows\Release\ydyun-guestctl.exe') -Destination $resourcesRoot
Copy-Item -LiteralPath (Join-Path $repoRoot 'windows\README.md') -Destination $resourcesRoot
Copy-Item -LiteralPath (Join-Path $repoRoot 'docs\WINDOWS-OPEN-SOURCE-AUDIT.md') -Destination $resourcesRoot

$manifest = [ordered]@{
    name = 'ydyun-windows-open-source'
    format = 1
    os_family = $OsFamily
    generated_utc = [DateTime]::UtcNow.ToString('o')
    source_commit = (& git -C $repoRoot rev-parse HEAD).Trim()
    policy = 'No ZTE/ICE payload; no account/password/group mutation; no QEMU guest agent.'
    files = @()
}
Get-ChildItem -LiteralPath $output -File -Recurse | ForEach-Object {
    $relative = $_.FullName.Substring($output.Length + 1).Replace('\', '/')
    $manifest.files += [ordered]@{
        path = $relative
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        bytes = $_.Length
    }
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'manifest.json') -Encoding utf8
Get-ChildItem -LiteralPath $output -File -Recurse | ForEach-Object {
    $relative = $_.FullName.Substring($output.Length + 1).Replace('\', '/')
    "{0}  {1}" -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
} | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS') -Encoding ascii

$zip = "$output.zip"
Compress-Archive -Path (Join-Path $output '*') -DestinationPath $zip -CompressionLevel Optimal
Write-Host "已生成目录：$output"
Write-Host "已生成安装包：$zip"
