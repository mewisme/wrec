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
$scoop = $scoop -replace 'https://github.com/mewisme/wrec/releases/download/v[^/]+/', "https://github.com/mewisme/wrec/releases/download/v$Version/"
$scoop = $scoop -replace '"hash": "[^"]+"', "`"hash`": `"$hash`""
Set-Content -LiteralPath (Join-Path $RepoRoot 'bucket/wrec.json') -Value $scoop -NoNewline
Set-Content -LiteralPath (Join-Path $DistDir 'wrec.json') -Value $scoop -NoNewline

# Winget manifests
$wingetDir = Join-Path $DistDir 'winget/Mew.Wrec'
New-Item -ItemType Directory -Force -Path $wingetDir | Out-Null

$versionYaml = @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.version.1.6.0.schema.json

PackageIdentifier: Mew.Wrec
PackageVersion: $Version
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.6.0
"@
Set-Content -LiteralPath (Join-Path $wingetDir 'Mew.Wrec.yaml') -Value $versionYaml

$installerYaml = @"
# yaml-language-server: `$schema=https://aka.ms/winget-manifest.installer.1.6.0.schema.json

PackageIdentifier: Mew.Wrec
PackageVersion: $Version
InstallerType: zip
NestedInstallerType: portable
NestedInstallerFiles:
  - RelativeFilePath: wrec.exe
    PortableCommandAlias: wrec
Commands:
  - wrec
Installers:
  - Architecture: x64
    InstallerUrl: $releaseUrl
    InstallerSha256: $hash
ManifestType: installer
ManifestVersion: 1.6.0
"@
Set-Content -LiteralPath (Join-Path $wingetDir 'Mew.Wrec.installer.yaml') -Value $installerYaml

$localeSrc = Join-Path $RepoRoot 'packaging/winget/Mew.Wrec/Mew.Wrec.locale.en-US.yaml'
$localeDst = Join-Path $wingetDir 'Mew.Wrec.locale.en-US.yaml'
Copy-Item -LiteralPath $localeSrc -Destination $localeDst -Force
(Get-Content -LiteralPath $localeDst -Raw) -replace 'PackageVersion: [^\r\n]+', "PackageVersion: $Version" |
Set-Content -LiteralPath $localeDst -NoNewline

Write-Host "Packaged $zipName (SHA256: $hash)"
