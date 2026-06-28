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

# Winget manifests (from packaging/winget templates)
$wingetTemplateDir = Join-Path $RepoRoot 'packaging/winget/Mew.Wrec'
$wingetDir = Join-Path $DistDir 'winget/Mew.Wrec'
New-Item -ItemType Directory -Force -Path $wingetDir | Out-Null

Get-ChildItem -LiteralPath $wingetTemplateDir -Filter '*.yaml' | ForEach-Object {
  $content = Get-Content -LiteralPath $_.FullName -Raw
  $content = $content -replace 'PackageVersion: [^\r\n]+', "PackageVersion: $Version"
  $content = $content -replace 'InstallerUrl: [^\r\n]+', "InstallerUrl: $releaseUrl"
  $content = $content -replace 'InstallerSha256: [^\r\n]+', "InstallerSha256: $hash"
  Set-Content -LiteralPath (Join-Path $wingetDir $_.Name) -Value $content -NoNewline
}

Write-Host "Packaged $zipName (SHA256: $hash)"
