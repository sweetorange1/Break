# Break 构建脚本
# 用法： powershell -NoProfile -ExecutionPolicy Bypass -File "D:/Break/build.ps1"
# 可选： -Target Break_Standalone / Break_VST3 / break_headless / break_dsp_test
#        -Jobs N           并行度（Ninja 用 -j，Visual Studio 用 /m）
#        -Generator Ninja|VS   强制生成器（默认：有 ninja 用 Ninja，否则 VS）
#
# 路径全部相对脚本自身解析，不再依赖固定盘符。
# JUCE 源码复用本地已有副本（避免重新下载），可用环境变量 JUCE_SRC 指定。

param(
    [string]$Target    = "",
    [string]$Jobs      = "",
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"

$root = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }

# ---- 生成器选择：优先 Ninja（有则手动配 MSVC 环境），否则 Visual Studio 多配置 ----
$ninjaCmd = Get-Command ninja -ErrorAction SilentlyContinue
$useNinja = if ($Generator -ne "") { $Generator -eq "Ninja" } else { $null -ne $ninjaCmd }

$msvcRoot = $null
$sdkVer   = $null
$sdkRoot  = "C:\Program Files (x86)\Windows Kits\10"

if (-not $useNinja)
{
    # VS 生成器：CMake 自己找工具链，只需确认装了 VS + SDK
    $vswhereAll = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhereAll)
    {
        $vsInstall = & $vswhereAll -latest -prerelease -products * `
                     -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                     -property installationPath
    }
    if (-not $vsInstall) { throw "找不到 Visual Studio（需要 C++ 桌面开发组件）" }
    Write-Host "VS   : $vsInstall"

    $sdkVer = Get-ChildItem (Join-Path $sdkRoot "Include") -Directory |
              Sort-Object Name | Select-Object -Last 1 -ExpandProperty Name
    Write-Host "SDK  : $sdkVer"
}

# ---- 定位 MSVC（Ninja 手动构建环境需要）----
if ($useNinja)
{
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere)
    {
        $vsInstall = & $vswhere -latest -prerelease -products * `
                     -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                     -property installationPath
        if ($vsInstall)
        {
            $cand = Get-ChildItem (Join-Path $vsInstall "VC\Tools\MSVC") -Directory |
                    Sort-Object Name | Select-Object -Last 1
            if ($cand) { $msvcRoot = $cand.FullName }
        }
    }
    if (-not $msvcRoot) { throw "找不到 MSVC 工具集" }

    $sdkVer = Get-ChildItem (Join-Path $sdkRoot "Include") -Directory |
              Sort-Object Name | Select-Object -Last 1 -ExpandProperty Name

    $bin    = Join-Path $msvcRoot "bin\Hostx64\x64"
    $env:INCLUDE = "$msvcRoot\include;$sdkRoot\Include\$sdkVer\ucrt;$sdkRoot\Include\$sdkVer\shared;$sdkRoot\Include\$sdkVer\um;$sdkRoot\Include\$sdkVer\winrt"
    $env:LIB     = "$msvcRoot\lib\x64;$sdkRoot\Lib\$sdkVer\ucrt\x64;$sdkRoot\Lib\$sdkVer\um\x64"
    # rc.exe / mt.exe 必须在 PATH 里，否则 CMake 编译器自检的链接步骤会失败
    $env:PATH    = "$bin;$(Join-Path $sdkRoot "bin\$sdkVer\x64");$env:PATH"

    Write-Host "MSVC : $msvcRoot"
    Write-Host "SDK  : $sdkVer"
}

# ---- JUCE 源码复用（避免重新下载）：环境变量优先，其次本机已知副本 ----
$candidates = @()
if ($env:JUCE_SRC) { $candidates += $env:JUCE_SRC }
$candidates += @(
    (Join-Path $root "_deps\juce-src"),
    "D:\complete\libs\juce",
    "D:\Y2Kmeter2.7\third_party\JUCE",
    "D:\y2kmetergit\third_party\JUCE"
)
$candidates += (Get-ChildItem "D:\*\cmake-build-*\_deps\juce-src" -Directory -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty FullName)

$juceSrc = $candidates | Where-Object { $_ -and (Test-Path (Join-Path $_ "modules\juce_core\juce_core.h")) } |
           Select-Object -First 1
if (-not $juceSrc) { throw "找不到 JUCE 源码目录（可用 -DFETCHCONTENT_SOURCE_DIR_JUCE 或环境变量 JUCE_SRC 指定）" }
Write-Host "JUCE : $juceSrc"

if ($useNinja) { $buildDir = Join-Path $root "cmake-build-ninja" }
else           { $buildDir = Join-Path $root "cmake-build-vs" }

if (-not (Test-Path (Join-Path $buildDir "CMakeCache.txt")))
{
    $cfgArgs = @("-S", $root, "-B", $buildDir, "-DFETCHCONTENT_SOURCE_DIR_JUCE=$juceSrc")
    if ($useNinja)
    {
        $cfgArgs += @("-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                      "-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl")
    }
    else
    {
        $cfgArgs += @("-A", "x64")   # 多配置生成器：构建时再选 Release
    }

    & cmake @cfgArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake 配置失败" }
}

$args = @("--build", $buildDir, "--config", "Release")
if ($Target -ne "") { $args += @("--target", $Target) }
if ($Jobs   -ne "")
{
    if ($useNinja) { $args += @("--", "-j", $Jobs) }
    else           { $args += @("--", "/m", $Jobs) }
}

& cmake @args
if ($LASTEXITCODE -ne 0) { throw "构建失败" }

Write-Host "BUILD_OK"
