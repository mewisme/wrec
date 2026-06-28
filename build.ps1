#Requires -Version 5.1
<#
.SYNOPSIS
  Configure and build wrec (Ninja by default, matches CI).

.EXAMPLE
  .\build.ps1
  .\build.ps1 -Generator VisualStudio
  .\build.ps1 -Clean
  .\build.ps1 -Version 1.2.3
#>
param(
  [ValidateSet('Ninja', 'VisualStudio')]
  [string]$Generator = 'Ninja',
  [ValidateSet('Debug', 'Release')]
  [string]$Config = 'Release',
  [string]$Version = '',
  [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot

function Find-VsWhere {
  $path = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path $path)) {
    throw 'vswhere not found. Install Visual Studio 2022+ or Build Tools with Desktop C++.'
  }
  return $path
}

function Find-VsInstallPath {
  $vswhere = Find-VsWhere
  $install = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath 2>$null
  if (-not $install) {
    throw 'No Visual Studio installation with C++ tools found.'
  }
  return $install
}

function Find-CMake {
  $cmd = Get-Command cmake -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  $install = Find-VsInstallPath
  $bundled = Join-Path $install 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
  if (Test-Path $bundled) { return $bundled }
  throw 'cmake not found. Install CMake or Visual Studio with C++ workload.'
}

function Find-Ninja {
  $cmd = Get-Command ninja -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  $install = Find-VsInstallPath
  $bundled = Join-Path $install 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
  if (Test-Path $bundled) { return $bundled }
  throw @'
ninja not found. Install it once, then re-run:

  winget install Ninja-build.Ninja

Or use: .\build.ps1 -Generator VisualStudio
'@
}

$cmake = Find-CMake
$vsDevCmd = Join-Path (Find-VsInstallPath) 'Common7\Tools\VsDevCmd.bat'
$versionArg = if ($Version) { " -DWREC_VERSION=$Version" } else { ' -UWREC_VERSION' }

if ($Generator -eq 'Ninja') {
  $buildDir = Join-Path $Root 'build'
  $ninja = Find-Ninja
  $exePath = Join-Path $buildDir 'wrec.exe'
  if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
  }
  $configure = "`"$cmake`" -B `"$buildDir`" -G Ninja -DCMAKE_BUILD_TYPE=$Config -DCMAKE_MAKE_PROGRAM=`"$ninja`"$versionArg"
  $build = "`"$cmake`" --build `"$buildDir`" --parallel"
}
else {
  $buildDir = Join-Path $Root 'build-vs'
  $exePath = Join-Path $buildDir "$Config\wrec.exe"
  if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
  }
  # Use latest installed VS generator (17 2022, 18 2026, ...)
  $vswhere = Find-VsWhere
  $version = & $vswhere -latest -property catalog_buildVersion 2>$null
  $genYear = if ($version -match '^(\d+)') { [int]$Matches[1] } else { 2022 }
  $vsGen = "Visual Studio 17 2022"
  if ($genYear -ge 2026) { $vsGen = 'Visual Studio 18 2026' }
  elseif ($genYear -ge 2025) { $vsGen = 'Visual Studio 17 2022' }
  $configure = "`"$cmake`" -B `"$buildDir`" -G `"$vsGen`" -A x64$versionArg"
  $build = "`"$cmake`" --build `"$buildDir`" --config $Config --parallel"
}

Write-Host "Generator: $Generator" -ForegroundColor Cyan
Write-Host "Build dir: $buildDir" -ForegroundColor Cyan

cmd /c "`"$vsDevCmd`" -arch=amd64 -host_arch=amd64 && cd /d `"$Root`" && $configure && $build"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Built: $exePath" -ForegroundColor Green        Write-Host "Built: $exePath" -ForegroundColreenor G

Write-Host "Built: $exePath" -ForegroundColor Greenreen
