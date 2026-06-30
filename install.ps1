#Requires -Version 5.1
<#
.SYNOPSIS
  Download and install wrec from GitHub Releases.

.EXAMPLE
  irm https://raw.githubusercontent.com/mewisme/wrec/main/install.ps1 | iex

.EXAMPLE
  & ([ScriptBlock]::Create((irm https://raw.githubusercontent.com/mewisme/wrec/main/install.ps1))) -Version v1.2.3

.EXAMPLE
  $env:WREC_VERSION = 'v1.2.3'; irm https://raw.githubusercontent.com/mewisme/wrec/main/install.ps1 | iex
#>
[CmdletBinding()]
param(
  [string]$Version
)

$ErrorActionPreference = 'Stop'

$RepoOwner = 'mewisme'
$RepoName = 'wrec'
$Repo = "$RepoOwner/$RepoName"
$AssetGlob = 'wrec-v*-windows-amd64.zip'

function Write-Step([string]$Message) {
  Write-Host "==> $Message"
}

function Fail([string]$Message) {
  Write-Error $Message
  exit 1
}

function Test-WindowsPlatform {
  if ($PSVersionTable.PSPlatform -eq 'Unix') {
    return $false
  }
  if ($env:OS -eq 'Windows_NT') {
    return $true
  }
  return [bool]$IsWindows
}

function Normalize-Tag([string]$InputVersion) {
  $trimmed = $InputVersion.Trim().TrimStart('v', 'V')
  if ($trimmed -notmatch '^(\d+\.\d+\.\d+)(?:[-+].*)?$') {
    Fail "Invalid version '$InputVersion'. Expected semver like 1.2.3 or v1.2.3."
  }
  return "v$($Matches[1])"
}

function Get-GitHubRelease {
  param([string]$Tag)

  $uri = if ($Tag) {
    "https://api.github.com/repos/$Repo/releases/tags/$Tag"
  }
  else {
    "https://api.github.com/repos/$Repo/releases/latest"
  }

  Write-Step "Fetching release metadata from GitHub ($uri)"
  try {
    return Invoke-RestMethod -Uri $uri -Headers @{ 'User-Agent' = 'wrec-installer' }
  }
  catch {
    $status = $null
    if ($_.Exception.Response) {
      $status = [int]$_.Exception.Response.StatusCode
    }
    if ($status -eq 404) {
      if ($Tag) {
        Fail "Release '$Tag' was not found. Check https://github.com/$Repo/releases"
      }
      Fail "No releases found for https://github.com/$Repo"
    }
    if ($status -eq 403) {
      Fail "GitHub API request was denied (rate limit or network policy). Try again later or download manually from https://github.com/$Repo/releases"
    }
    Fail "Failed to fetch release metadata: $($_.Exception.Message)"
  }
}

function Get-ReleaseAsset {
  param(
    [Parameter(Mandatory)]
    $Release
  )

  $asset = $Release.assets | Where-Object { $_.name -like $AssetGlob } | Select-Object -First 1
  if (-not $asset) {
    $names = ($Release.assets | ForEach-Object { $_.name }) -join ', '
    if ($names) {
      Fail "Release '$($Release.tag_name)' has no asset matching '$AssetGlob'. Found: $names"
    }
    Fail "Release '$($Release.tag_name)' has no assets matching '$AssetGlob'."
  }
  return $asset
}

if (-not (Test-WindowsPlatform)) {
  Fail 'wrec supports Windows only.'
}

if (-not $Version -and $env:WREC_VERSION) {
  $Version = $env:WREC_VERSION
}

$tag = if ($Version) { Normalize-Tag $Version } else { $null }
$release = Get-GitHubRelease -Tag $tag
$asset = Get-ReleaseAsset -Release $release
$tag = $release.tag_name

Write-Step "Selected release $tag ($($asset.name))"

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("wrec-install-{0}" -f [Guid]::NewGuid().ToString('N'))
$zipPath = Join-Path $tempRoot $asset.name
$extractDir = Join-Path $tempRoot 'extract'

try {
  New-Item -ItemType Directory -Force -Path $tempRoot, $extractDir | Out-Null

  Write-Step "Downloading $($asset.browser_download_url)"
  try {
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zipPath -UseBasicParsing
  }
  catch {
    Fail "Download failed: $($_.Exception.Message)"
  }

  Write-Step "Extracting archive"
  try {
    Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force
  }
  catch {
    Fail "Extraction failed: $($_.Exception.Message)"
  }

  $exePath = Join-Path $extractDir 'wrec.exe'
  if (-not (Test-Path -LiteralPath $exePath)) {
    Fail "Archive did not contain wrec.exe at the root."
  }

  Write-Step "Running wrec install"
  & $exePath install
  if ($LASTEXITCODE -ne 0) {
    Fail "wrec install exited with code $LASTEXITCODE."
  }

  Write-Step "Done. Desktop and Start menu shortcuts named wrec were created."
  Write-Step "Restart your terminal, then run: wrec --help"
}
finally {
  if (Test-Path -LiteralPath $tempRoot) {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
  }
}
