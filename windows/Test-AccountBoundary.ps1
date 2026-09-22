[CmdletBinding()]
param(
    [string[]]$BinaryPath = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$windowsRoot = Split-Path $PSScriptRoot
$forbiddenCpp = @(
    '\bNetUser(Add|SetInfo|Del|ChangePassword)\s*\(',
    '\bNetLocalGroup(Add|Del|Set)[A-Za-z]*\s*\(',
    '\bLsaStorePrivateData\s*\('
)
$forbiddenPowerShell = @(
    '\bNew-LocalUser\b',
    '\bSet-LocalUser\b',
    '\bAdd-LocalGroupMember\b',
    '\bRemove-LocalGroupMember\b',
    '\bnet(\.exe)?\s+user\b',
    '\[ADSI\].*WinNT://',
    '\bSet-ADAccountPassword\b',
    '\bSet-ADUser\b'
)

$sourceChecks = @(
    @{
        Path = (Join-Path $windowsRoot 'windows\src\ydyun_guestctl.cpp')
        Patterns = $forbiddenCpp
    },
    @{
        Path = (Join-Path $windowsRoot 'windows\Install-YdyunGuest.ps1')
        Patterns = $forbiddenPowerShell
    },
    @{
        Path = (Join-Path $windowsRoot 'windows\Install-YdyunOpenGuest.ps1')
        Patterns = $forbiddenPowerShell
    },
    @{
        Path = (Join-Path $windowsRoot 'windows\Build-OpenSourcePackage.ps1')
        Patterns = $forbiddenPowerShell
    },
    @{
        Path = (Join-Path $windowsRoot 'windows\Install-YdyunOpenGuest.bat')
        Patterns = $forbiddenPowerShell
    }
)

foreach ($check in $sourceChecks) {
    foreach ($pattern in $check.Patterns) {
        $source = $check.Path
        $match = Select-String -LiteralPath $source -Pattern $pattern -CaseSensitive:$false
        if ($null -ne $match) {
            throw "账户边界检查失败：$source 匹配 $pattern"
        }
    }
}

$forbiddenImports = @(
    'NetUserAdd',
    'NetUserSetInfo',
    'NetUserDel',
    'NetUserChangePassword',
    'NetLocalGroupAddMembers',
    'NetLocalGroupDelMembers',
    'LsaStorePrivateData'
)

if ($BinaryPath.Count -gt 0) {
    $dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    $llvmReadObj = Get-Command llvm-readobj.exe -ErrorAction SilentlyContinue
    if ($null -eq $dumpbin -and $null -eq $llvmReadObj) {
        throw '检查 PE 导入表需要 Visual Studio dumpbin.exe 或 llvm-readobj.exe。'
    }

    foreach ($binary in $BinaryPath) {
        $resolved = (Resolve-Path -LiteralPath $binary).Path
        if ($null -ne $dumpbin) {
            $imports = & $dumpbin.Source /nologo /imports $resolved 2>&1 | Out-String
        }
        else {
            $imports = & $llvmReadObj.Source --coff-imports $resolved 2>&1 | Out-String
        }
        foreach ($symbol in $forbiddenImports) {
            if ($imports -match [regex]::Escape($symbol)) {
                throw "账户边界检查失败：$resolved 导入 $symbol"
            }
        }
        Write-Host "[ok] PE 账户 API 导入检查：$resolved"
    }
}

Write-Host '[ok] Windows 实现不包含创建用户、改密码或修改本地组成员的代码路径。'
