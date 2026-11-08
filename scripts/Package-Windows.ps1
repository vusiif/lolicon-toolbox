# Run in an initialized MSVC x64 environment, using PowerShell 7.
# The Qt kit and the version come from the configured Release build, so the
# package always deploys the same Qt that compiled it. Pass -QtBin only to
# override windeployqt's kit deliberately.
[CmdletBinding()]
param(
    [string]$CMakePath = 'cmake',
    [string]$QtBin,
    [int]$Jobs = 6,
    [switch]$SkipArchive
)
$ErrorActionPreference = 'Stop'
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    throw 'Initialize the MSVC x64 environment before packaging.'
}
$projectRoot = Split-Path -Parent $PSScriptRoot
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Get-CacheValue([string]$Cache, [string]$Name) {
    $line = Select-String -LiteralPath $Cache -Pattern "^$([regex]::Escape($Name)):[A-Z]+=(.*)$" | Select-Object -First 1
    if (-not $line) { throw "$Name is missing from $Cache" }
    $line.Matches[0].Groups[1].Value
}
Push-Location $projectRoot
try {
    Invoke-Checked $CMakePath @('--preset', 'release')
    $cache = Join-Path $projectRoot 'cmake-build-release/CMakeCache.txt'
    $version = Get-CacheValue $cache 'CMAKE_PROJECT_VERSION'
    if (-not $QtBin) {
        # Qt6_DIR is <kit>/lib/cmake/Qt6.
        $QtBin = Join-Path (Split-Path (Split-Path (Split-Path (Get-CacheValue $cache 'Qt6_DIR')))) 'bin'
    }
    $deployTool = Join-Path $QtBin 'windeployqt.exe'
    if (-not (Test-Path -LiteralPath $deployTool)) { throw "Missing $deployTool" }
    $qtVersion = (& (Join-Path $QtBin 'qtpaths.exe') -query QT_VERSION).Trim()
    Write-Output "Packaging Toolbox $version with Qt $qtVersion from $QtBin"
    Invoke-Checked $CMakePath @('--build', '--preset', 'release', '--parallel', "$Jobs")
    $ctest = Join-Path (Split-Path (Get-Command $CMakePath).Source) 'ctest.exe'
    Invoke-Checked $ctest @('--preset', 'release')
    # Each run creates a fresh package; retention runs only after ZIP completion.
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $package = Join-Path $projectRoot "dist/Toolbox-$version-windows-x64-$stamp"
    New-Item -ItemType Directory -Path $package | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot 'cmake-build-release/app/Toolbox.exe') -Destination $package
    Invoke-Checked $deployTool @('--release', '--compiler-runtime', (Join-Path $package 'Toolbox.exe'))
    Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $package
    Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination (Join-Path $package 'LICENSE.txt')
    $commit = (git -C $projectRoot rev-parse HEAD 2>$null) ?? 'unknown'
    $dirty = (git -C $projectRoot status --porcelain --untracked-files=no 2>$null) ? ' (uncommitted changes)' : ''
    "Toolbox $version`ncommit: $commit$dirty`nqt: $qtVersion`nbuilt: $stamp" |
        Set-Content -LiteralPath (Join-Path $package 'VERSION.txt') -Encoding utf8
    $packageDocs = Join-Path $package 'docs'
    New-Item -ItemType Directory -Path $packageDocs | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/File-Recovery-Testing.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Filename-Recovery-Testing.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Clipboard-Testing.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Diagnostic-Testing.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Text-Style-Converter.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Text-Style-Testing.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Text-Style-Restoration-AI-Testing.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Voice-Designer-Testing.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Voice-Designer-Implementation.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Character-Art-Testing.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/MapleMonoNL-NF-CN-unhinted/OFL.txt') -Destination (Join-Path $package 'Maple-OFL.txt')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/MapleMonoNL-NF-CN-unhinted/FONT-NOTICE.md') -Destination (Join-Path $package 'Maple-FONT-NOTICE.md')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/Style-Data-Sources.md') -Destination $packageDocs
    Copy-Item -LiteralPath (Join-Path $projectRoot 'resources/styles/LICENSE.txt') -Destination (Join-Path $package 'Text-Style-Data-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'resources/styles/DATA-NOTICES.txt') -Destination (Join-Path $package 'Text-Style-Data-NOTICES.txt')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'resources/styles/licenses/marspeak-LICENSE.txt') -Destination (Join-Path $package 'marspeak-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/ElaWidgetTools-main/LICENSE') -Destination (Join-Path $package 'ElaWidgetTools-LICENSE.txt')
    Compress-Archive -LiteralPath $package -DestinationPath "$package.zip"
    Get-FileHash -LiteralPath "$package.zip" -Algorithm SHA256 |
        ForEach-Object { "$($_.Hash)  $([IO.Path]::GetFileName($_.Path))" } |
        Set-Content -LiteralPath "$package.zip.sha256" -Encoding utf8
    Write-Output "Package: $package.zip"
    if ($SkipArchive) { Write-Output 'Skipped Archive-Old-Dist.ps1; older builds were left in dist/.' }
    else {
        try { & (Join-Path $PSScriptRoot 'Archive-Old-Dist.ps1') -KeepLast 3 }
        catch { Write-Warning "Package completed, but old builds were retained: $($_.Exception.Message)" }
    }
} finally { Pop-Location }
