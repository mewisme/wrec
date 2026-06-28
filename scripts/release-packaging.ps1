#Requires -Version 5.1
param(
  [Parameter(Mandatory)]
  [string]$Version,

  [Parameter(Mandatory)]
  [string]$ExePath,

  [Parameter(Mandatory)]
  [string]$DistDir
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path $PSScriptRoot -Parent

function Update-ManifestVersion {
  param(
    [string]$Content,
    [string]$OldVersion,
    [string]$NewVersion
  )
  $escapedOld = [regex]::Escape($OldVersion)
  $content = $Content -replace "PackageVersion: $escapedOld", "PackageVersion: $NewVersion"
  $content = $content -replace "v$escapedOld", "v$NewVersion"
  $content = $content -replace "wrec-v$escapedOld", "wrec-v$NewVersion"
  $placeholderHash = 'InstallerSha256: 0000000000000000000000000000000000000000000000000000000000000000'
  $content = $content -replace 'InstallerSha256: [^\r\n]+', $placeholderHash
  $content = $content -replace 'ReleaseDate: [^\r\n]+', "ReleaseDate: $(Get-Date -Format 'yyyy-MM-dd')"
  return $content
}

function Get-WingetVersionEntries {
  param([string]$BaseDir)
  if (-not (Test-Path -LiteralPath $BaseDir)) {
    return @()
  }
  Get-ChildItem -LiteralPath $BaseDir -Directory | ForEach-Object {
    $parsed = $null
    if ([version]::TryParse($_.Name, [ref]$parsed)) {
      [pscustomobject]@{ Path = $_.FullName; Name = $_.Name; Version = $parsed }
    }
  }
}

function Initialize-WingetTemplateDir {
  param(
    [string]$BaseDir,
    [string]$TargetVersion
  )
  $dest = Join-Path $BaseDir $TargetVersion
  if (Test-Path -LiteralPath $dest) {
    return $dest
  }

  $target = [version]$TargetVersion
  $previous = Get-WingetVersionEntries -BaseDir $BaseDir |
    Where-Object { $_.Version -lt $target } |
    Sort-Object -Property Version -Descending |
    Select-Object -First 1

  if (-not $previous) {
    throw "No previous winget manifests under packaging/winget/manifests/m/Mew/Wrec/ to copy for v$TargetVersion"
  }

  New-Item -ItemType Directory -Force -Path $dest | Out-Null
  Get-ChildItem -LiteralPath $previous.Path -Filter '*.yaml' | ForEach-Object {
    $content = Get-Content -LiteralPath $_.FullName -Raw
    $content = Update-ManifestVersion -Content $content -OldVersion $previous.Name -NewVersion $TargetVersion
    Set-Content -LiteralPath (Join-Path $dest $_.Name) -Value $content -NoNewline
  }

  Write-Host "Created winget templates for v$TargetVersion from v$($previous.Name)"
  return $dest
}

function Write-WingetManifests {
  param(
    [string]$TemplateDir,
    [string]$DestDir,
    [string]$PackageVersion,
    [string]$InstallerUrl,
    [string]$InstallerSha256
  )
  New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
  Get-ChildItem -LiteralPath $TemplateDir -Filter '*.yaml' | ForEach-Object {
    $content = Get-Content -LiteralPath $_.FullName -Raw
    $content = $content -replace 'PackageVersion: [^\r\n]+', "PackageVersion: $PackageVersion"
    $content = $content -replace 'InstallerUrl: [^\r\n]+', "InstallerUrl: $InstallerUrl"
    $content = $content -replace 'InstallerSha256: [^\r\n]+', "InstallerSha256: $InstallerSha256"
    Set-Content -LiteralPath (Join-Path $DestDir $_.Name) -Value $content -NoNewline
  }
}

$zipName = "wrec-v$Version-windows-amd64.zip"
$zipPath = Join-Path $DistDir $zipName
$stagingDir = Join-Path $DistDir "wrec-v$Version-windows-amd64"

New-Item -ItemType Directory -Force -Path $DistDir, $stagingDir | Out-Null
Copy-Item -LiteralPath $ExePath -Destination (Join-Path $stagingDir 'wrec.exe') -Force

if (Test-Path -LiteralPath $zipPath) {
  Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $stagingDir 'wrec.exe') -DestinationPath $zipPath -Force

$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
$hashLine = "$hash  $zipName"
Set-Content -LiteralPath (Join-Path $DistDir 'SHA256SUMS') -Value $hashLine -NoNewline

$releaseUrl = "https://github.com/mewisme/wrec/releases/download/v$Version/$zipName"

# Scoop bucket manifest (bucket/wrec.json + release asset copy)
$scoopTemplate = Get-Content -LiteralPath (Join-Path $RepoRoot 'bucket/wrec.json') -Raw
$scoop = $scoopTemplate -replace '"version": "[^"]+"', "`"version`": `"$Version`""
$scoop = $scoop -replace 'https://github.com/mewisme/wrec/releases/download/v[^/]+/wrec-v[\d.]+-windows-amd64\.zip', $releaseUrl
$scoop = $scoop -replace 'https://github.com/mewisme/wrec/releases/download/v[^/]+/', "https://github.com/mewisme/wrec/releases/download/v$Version/"
$scoop = $scoop -replace '"hash": "[^"]+"', "`"hash`": `"$hash`""
Set-Content -LiteralPath (Join-Path $RepoRoot 'bucket/wrec.json') -Value $scoop -NoNewline
Set-Content -LiteralPath (Join-Path $DistDir 'wrec.json') -Value $scoop -NoNewline

# Winget manifests (wingetcreate layout: packaging/winget/manifests/m/Mew/Wrec/<version>/)
$wingetRel = "winget/manifests/m/Mew/Wrec/$Version"
$wingetBase = Join-Path $RepoRoot 'packaging/winget/manifests/m/Mew/Wrec'
$wingetTemplateDir = Initialize-WingetTemplateDir -BaseDir $wingetBase -TargetVersion $Version

Write-WingetManifests -TemplateDir $wingetTemplateDir -DestDir (Join-Path $RepoRoot "packaging/$wingetRel") `
  -PackageVersion $Version -InstallerUrl $releaseUrl -InstallerSha256 $hash
Write-WingetManifests -TemplateDir $wingetTemplateDir -DestDir (Join-Path $DistDir $wingetRel) `
  -PackageVersion $Version -InstallerUrl $releaseUrl -InstallerSha256 $hash

Write-Host "Packaged $zipName (SHA256: $hash)"
