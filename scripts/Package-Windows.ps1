# Run in an initialized MSVC x64 environment, using PowerShell 7.
[CmdletBinding()]
param(
    [string]$CMakePath = 'cmake',
    [string]$QtBin = 'C:\Qt\6.11.2\msvc2022_64\bin',
    [int]$Jobs = 6
)
$ErrorActionPreference = 'Stop'
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    throw 'Initialize the MSVC x64 environment before packaging.'
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$deployTool = Join-Path $QtBin 'windeployqt.exe'
if (-not (Test-Path -LiteralPath $deployTool)) { throw "Missing $deployTool" }
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
Push-Location $projectRoot
try {
    Invoke-Checked $CMakePath @('--preset', 'release')
    Invoke-Checked $CMakePath @('--build', '--preset', 'release', '--parallel', "$Jobs")
    $ctest = Join-Path (Split-Path (Get-Command $CMakePath).Source) 'ctest.exe'
    Invoke-Checked $ctest @('--preset', 'release')
    # Each run creates a fresh package; no stale DLLs and no recursive deletion.
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $package = Join-Path $projectRoot "dist/Toolbox-0.2.0-windows-x64-$stamp"
    New-Item -ItemType Directory -Path $package | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot 'cmake-build-release/app/Toolbox.exe') -Destination $package
    Invoke-Checked $deployTool @('--release', '--compiler-runtime', (Join-Path $package 'Toolbox.exe'))
    Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $package
    $packageDocs = Join-Path $package 'docs'
    New-Item -ItemType Directory -Path $packageDocs | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/File-Recovery-Testing.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Filename-Recovery-Testing.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Clipboard-Testing.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Diagnostic-Testing.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Text-Style-Converter.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Text-Style-Testing.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Style-Data-Sources.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'resources/styles/LICENSE.txt') -Destination (Join-Path $package 'Text-Style-Data-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/ElaWidgetTools-main/LICENSE') -Destination (Join-Path $package 'ElaWidgetTools-LICENSE.txt')
    Compress-Archive -LiteralPath $package -DestinationPath "$package.zip"
    Get-FileHash -LiteralPath "$package.zip" -Algorithm SHA256 |
        ForEach-Object { "$($_.Hash)  $([IO.Path]::GetFileName($_.Path))" } |
        Set-Content -LiteralPath "$package.zip.sha256" -Encoding utf8
    Write-Output "Package: $package.zip"
} finally { Pop-Location }
