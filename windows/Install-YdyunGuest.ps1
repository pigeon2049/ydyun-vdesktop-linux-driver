[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string]$DriverRoot,

    [ValidateSet('w10', 'w11')]
    [string]$OsFamily = 'w10',

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$SpiceAgentMsi,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{64}$')]
    [string]$SpiceAgentSha256,

    [string]$UsbIpInstaller = '',

    [string]$UsbIpSha256 = '',

    [switch]$EnableUsbIp,

    [switch]$AllowUnsignedSourceBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:InstallerCmdlet = $PSCmdlet
$driverSource = (Resolve-Path -LiteralPath $DriverRoot).Path

$allowedDrivers = [ordered]@{
    'qxldod.inf' = 'QXL WDDM DOD display'
    'viogpudo.inf' = 'VirtIO GPU display'
    'vioser.inf' = 'VirtIO serial/SPICE channel'
    'vioinput.inf' = 'VirtIO keyboard and pointer'
}

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw '请在 64 位“以管理员身份运行”的 Windows PowerShell 中执行。'
    }
}

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [int[]]$SuccessExitCode = @(0)
    )

    $process = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList `
        -Wait -PassThru -NoNewWindow
    if ($process.ExitCode -notin $SuccessExitCode) {
        throw "$FilePath 退出码为 $($process.ExitCode)：$($ArgumentList -join ' ')"
    }
}

function Get-LocalAccountSnapshot {
    foreach ($command in @('Get-LocalUser', 'Get-LocalGroup', 'Get-LocalGroupMember')) {
        if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
            throw "缺少账户边界校验命令 $command；请使用 64 位 Windows PowerShell 5.1 或更高版本。"
        }
    }

    $users = @(Get-LocalUser | Sort-Object { $_.SID.Value } | ForEach-Object {
        [ordered]@{
            Name = $_.Name
            SID = $_.SID.Value
            Enabled = $_.Enabled
            PasswordRequired = $_.PasswordRequired
            PasswordLastSet = if ($null -eq $_.PasswordLastSet) {
                $null
            }
            else {
                $_.PasswordLastSet.ToUniversalTime().ToString('o')
            }
        }
    })

    $memberships = @(Get-LocalGroup | Sort-Object Name | ForEach-Object {
        $groupName = $_.Name
        Get-LocalGroupMember -Name $groupName -ErrorAction Stop |
            Sort-Object { $_.SID.Value } | ForEach-Object {
                [ordered]@{
                    Group = $groupName
                    Name = $_.Name
                    SID = $_.SID.Value
                    ObjectClass = [string]$_.ObjectClass
                }
            }
    })

    return [ordered]@{
        Users = $users
        Memberships = $memberships
    } | ConvertTo-Json -Depth 6 -Compress
}

function Assert-NoVendorGuestStack {
    $forbiddenServices = @(
        'IceMainService', 'IceDisplayService', 'IceInputService',
        'IceSoundService', 'IceTunnelService', 'Vdservice',
        'ZProcessMonitor', 'USBIP Client'
    )
    foreach ($name in $forbiddenServices) {
        if ($null -ne (Get-Service -Name $name -ErrorAction SilentlyContinue)) {
            throw "检测到原厂 GuestOS 服务 $name。请在干净 Windows 快照中安装；本项目不会混装或自动删除它。"
        }
    }

    $forbiddenDrivers = Get-CimInstance Win32_SystemDriver -ErrorAction Stop |
        Where-Object {
            $_.Name -match '^(ice|zte|ZProcess)' -or
            $_.PathName -match '(?i)(ZTEGuestOS|\\ice[^\\]*\.sys|\\zte[^\\]*\.sys)'
        }
    if ($null -ne $forbiddenDrivers) {
        $names = ($forbiddenDrivers | Select-Object -ExpandProperty Name) -join ', '
        throw "检测到原厂内核驱动：$names。为避免闭源组件继续驻留，拒绝混装。"
    }
}

function Get-ConnectedHardwareText {
    return (Get-CimInstance Win32_PnPEntity -ErrorAction Stop |
        Select-Object -ExpandProperty DeviceID | Out-String)
}

function Assert-OpenSourceHardware {
    $devices = Get-ConnectedHardwareText
    $script:HasQxl = $devices -match 'PCI\\VEN_1B36&DEV_0100'
    $script:HasVirtioGpu = $devices -match 'PCI\\VEN_1AF4&DEV_1050'
    $script:HasVirtioInput = $devices -match 'PCI\\VEN_1AF4&DEV_(1005|1052)'
    $hasSerial = $devices -match 'PCI\\VEN_1AF4&DEV_(1003|1043)'
    $hasAudio = $devices -match '(HDAUDIO\\|PCI\\VEN_8086&DEV_(2415|2668|293E|293F))'

    if (-not ($script:HasQxl -or $script:HasVirtioGpu)) {
        throw '未发现 QXL (1B36:0100) 或 VirtIO GPU (1AF4:1050)。客体软件不能凭空创建宿主虚拟显卡。'
    }
    if (-not $hasSerial) {
        throw '未发现 VirtIO serial (1AF4:1003/1043)。没有宿主提供的 SPICE port，剪贴板和自动分辨率无法工作。'
    }
    if (-not $hasAudio) {
        throw '未发现标准 HDA/AC97 声卡。VirtIO-Win 没有 Windows virtio-snd 驱动，必须由宿主暴露标准虚拟声卡。'
    }
}

function Resolve-AllowedDrivers {
    $resolved = @()
    foreach ($entry in $allowedDrivers.GetEnumerator()) {
        if ($entry.Key -eq 'qxldod.inf' -and -not $script:HasQxl) {
            continue
        }
        if ($entry.Key -eq 'viogpudo.inf' -and -not $script:HasVirtioGpu) {
            continue
        }
        if ($entry.Key -eq 'vioinput.inf' -and -not $script:HasVirtioInput) {
            continue
        }
        $matches = @(Get-ChildItem -LiteralPath $driverSource -Filter $entry.Key `
            -File -Recurse -ErrorAction Stop | Where-Object {
                $_.FullName -match "[\\/]$([regex]::Escape($OsFamily))[\\/]amd64[\\/]"
            })
        if ($matches.Count -gt 1) {
            throw "找到多个 $($entry.Key)，请把 -DriverRoot 指向单一发行版/版本目录。"
        }
        if ($matches.Count -eq 1) {
            $content = Get-Content -LiteralPath $matches[0].FullName -Raw
            if ($matches[0].FullName -match '(?i)(zte|ice)' -or
                $content -match '(?i)(ZTE CORPORATION|ZTEGuestOS|ICE virtual)') {
                throw "拒绝原厂或重签名驱动：$($matches[0].FullName)"
            }
            $resolved += [pscustomobject]@{
                Path = $matches[0].FullName
                Name = $entry.Value
                FileName = $entry.Key
            }
        }
    }

    if (-not ($resolved.FileName -contains 'vioser.inf')) {
        throw "在 $OsFamily/amd64 下找不到 vioser.inf。SPICE agent 必须使用开源 VirtIO serial 驱动。"
    }
    if (-not (($resolved.FileName -contains 'qxldod.inf') -or
            ($resolved.FileName -contains 'viogpudo.inf'))) {
        throw "在 $OsFamily/amd64 下找不到 qxldod.inf 或 viogpudo.inf。"
    }
    return $resolved
}

function Assert-PayloadHash {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256,
        [Parameter(Mandatory = $true)][string]$Description
    )

    if ($ExpectedSha256 -notmatch '^[0-9A-Fa-f]{64}$') {
        throw "$Description 必须提供 64 位 SHA-256。"
    }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actual -ine $ExpectedSha256) {
        throw "$Description SHA-256 不匹配。expected=$ExpectedSha256 actual=$actual"
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne 'Valid' -and -not $AllowUnsignedSourceBuild) {
        throw "$Description 的 Authenticode 状态为 $($signature.Status)。发布包必须有效签名；仅自行从固定源码构建时可显式使用 -AllowUnsignedSourceBuild。"
    }
    Write-Host "[ok] $Description SHA-256：$actual"
}

function Set-AccountSafePolicy {
    foreach ($serviceName in @('RemoteRegistry', 'WinRM')) {
        $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
        if ($null -ne $service) {
            if ($service.Status -ne 'Stopped') {
                Stop-Service -Name $serviceName -Force
            }
            Set-Service -Name $serviceName -StartupType Disabled
        }
    }

    $policyPath = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System'
    $lsaPath = 'HKLM:\SYSTEM\CurrentControlSet\Control\Lsa'
    New-ItemProperty -Path $policyPath -Name LocalAccountTokenFilterPolicy `
        -PropertyType DWord -Value 0 -Force | Out-Null
    New-ItemProperty -Path $lsaPath -Name RestrictAnonymousSAM `
        -PropertyType DWord -Value 1 -Force | Out-Null
    New-ItemProperty -Path $lsaPath -Name RestrictAnonymous `
        -PropertyType DWord -Value 1 -Force | Out-Null
    New-ItemProperty -Path $lsaPath -Name LimitBlankPasswordUse `
        -PropertyType DWord -Value 1 -Force | Out-Null

    $winlogon = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon'
    New-ItemProperty -Path $winlogon -Name AutoAdminLogon `
        -PropertyType String -Value '0' -Force | Out-Null
    foreach ($valueName in @('DefaultPassword', 'ForceAutoLogon', 'AutoLogonSID')) {
        Remove-ItemProperty -Path $winlogon -Name $valueName -ErrorAction SilentlyContinue
    }
}

function Protect-WinlogonFromSpiceAgent {
    if ($null -eq (Get-Service -Name 'spice-agent' -ErrorAction SilentlyContinue)) {
        throw 'SPICE agent MSI 安装完成后没有出现 spice-agent 服务。'
    }

    Invoke-NativeChecked -FilePath "$env:SystemRoot\System32\sc.exe" `
        -ArgumentList @('sidtype', 'spice-agent', 'unrestricted')
    $identity = [Security.Principal.NTAccount]::new('NT SERVICE', 'spice-agent')
    $rights = [Microsoft.Win32.RegistryRights]::SetValue -bor
        [Microsoft.Win32.RegistryRights]::CreateSubKey -bor
        [Microsoft.Win32.RegistryRights]::Delete
    $rule = [Security.AccessControl.RegistryAccessRule]::new(
        $identity, $rights,
        [Security.AccessControl.InheritanceFlags]::ContainerInherit,
        [Security.AccessControl.PropagationFlags]::None,
        [Security.AccessControl.AccessControlType]::Deny
    )
    $path = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon'
    $acl = Get-Acl -Path $path
    $acl.SetAccessRule($rule)
    Set-Acl -Path $path -AclObject $acl
}

Assert-Administrator
Assert-NoVendorGuestStack
Assert-OpenSourceHardware
$drivers = Resolve-AllowedDrivers
$spiceMsi = (Resolve-Path -LiteralPath $SpiceAgentMsi).Path
Assert-PayloadHash -Path $spiceMsi -ExpectedSha256 $SpiceAgentSha256 `
    -Description '开源 SPICE agent MSI'
$accountStateBefore = Get-LocalAccountSnapshot

if ($script:InstallerCmdlet.ShouldProcess('Windows account policy', '收敛远程账户入口并禁用自动登录')) {
    Set-AccountSafePolicy
}

foreach ($driver in $drivers) {
    Write-Host "安装 $($driver.Name)：$($driver.Path)"
    if ($script:InstallerCmdlet.ShouldProcess($driver.Path, 'pnputil /add-driver /install')) {
        $quoted = '"{0}"' -f $driver.Path
        Invoke-NativeChecked -FilePath "$env:SystemRoot\System32\pnputil.exe" `
            -ArgumentList @('/add-driver', $quoted, '/install')
    }
}

if ($script:InstallerCmdlet.ShouldProcess($spiceMsi, '安装开源 SPICE agent')) {
    $quotedMsi = '"{0}"' -f $spiceMsi
    Invoke-NativeChecked -FilePath "$env:SystemRoot\System32\msiexec.exe" `
        -ArgumentList @('/i', $quotedMsi, '/qn', '/norestart') `
        -SuccessExitCode @(0, 3010)
    Protect-WinlogonFromSpiceAgent
}

if ($EnableUsbIp) {
    if ([Environment]::OSVersion.Version.Build -lt 18362) {
        throw 'usbip-win2 要求 Windows 10 1903/build 18362 或更高；当前系统版本过旧。'
    }
    if ([string]::IsNullOrWhiteSpace($UsbIpInstaller) -or
        [string]::IsNullOrWhiteSpace($UsbIpSha256)) {
        throw '启用 USB/IP 时必须同时提供 -UsbIpInstaller 和 -UsbIpSha256。'
    }
    $usbInstaller = (Resolve-Path -LiteralPath $UsbIpInstaller).Path
    Assert-PayloadHash -Path $usbInstaller -ExpectedSha256 $UsbIpSha256 `
        -Description '开源 usbip-win2 安装器'
    if ($script:InstallerCmdlet.ShouldProcess($usbInstaller, '安装开源 USB/IP UDE client')) {
        Invoke-NativeChecked -FilePath $usbInstaller `
            -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') `
            -SuccessExitCode @(0, 3010)
    }
}

if ($script:InstallerCmdlet.ShouldProcess('Windows account policy', '安装后复核自动登录状态')) {
    Set-AccountSafePolicy
}
$accountStateAfter = Get-LocalAccountSnapshot
if ($accountStateAfter -cne $accountStateBefore) {
    throw '账户边界校验失败：安装期间本地用户、密码时间戳或本地组成员发生变化。请勿重启，立即回滚快照。'
}

Write-Host ''
Write-Host '完成：只安装了开源 QXL/VirtIO/SPICE 组件，没有复制或启动任何 ZTE/ICE 二进制。'
Write-Host '账户边界：安装前后用户、密码时间戳和本地组成员完全一致；自动登录保持关闭。'
Write-Host '请重启后运行 ydyun-guestctl doctor，再进行官方客户端画面、输入、声音和 USB 验收。'
