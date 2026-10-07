# PowerShell 7: archive verified historical builds before pruning local copies.
[CmdletBinding()]
param(
    [ValidateRange(1, 100)][int]$KeepLast = 3,
    [string]$Repository = 'vusiif/lolicon-toolbox',
    [string]$ArchiveTag = 'dist-archive',
    [string]$GhPath = '',
    [switch]$PlanOnly
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$distRoot = Join-Path $projectRoot 'dist'
if (-not (Test-Path -LiteralPath $distRoot)) { return }
$distRoot = (Resolve-Path -LiteralPath $distRoot).Path
if ($distRoot -ne (Join-Path $projectRoot 'dist') -or
    ((Get-Item -LiteralPath $distRoot).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'dist must be a real directory directly inside the project.'
}
function Assert-LocalPath([string]$Path) {
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    if (-not $resolved.StartsWith($distRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing an operation outside dist: $resolved"
    }
    if ((Get-Item -LiteralPath $resolved).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Refusing a linked path: $resolved"
    }
    return $resolved
}
$builds = @(Get-ChildItem -LiteralPath $distRoot -File |
    Where-Object { $_.Name -match '^Toolbox-[0-9]+\.[0-9]+\.[0-9]+-windows-x64-[0-9]{8}-[0-9]{6}\.zip$' } |
    Sort-Object @{ Expression = { $_.Name.Substring($_.Name.Length - 19, 15) }; Descending = $true })
$older = @($builds | Select-Object -Skip $KeepLast)
Write-Output "Builds: $($builds.Count); keep newest $KeepLast; archive $($older.Count)."
foreach ($build in $builds | Select-Object -First $KeepLast) { Write-Output "Keep: $($build.Name)" }
if ($PlanOnly) { foreach ($build in $older) { Write-Output "Archive then prune: $($build.Name)" }; return }
if ($older.Count -eq 0) { return }
if (-not $GhPath) {
    $command = Get-Command gh -ErrorAction SilentlyContinue
    if ($command) { $GhPath = $command.Source }
    elseif (Test-Path -LiteralPath 'C:\Program Files\GitHub CLI\gh.exe') { $GhPath = 'C:\Program Files\GitHub CLI\gh.exe' }
    else { throw 'GitHub CLI is required to archive old builds. Local files were retained.' }
}
function Invoke-Gh([string[]]$Arguments) {
    $output = & $GhPath @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "GitHub CLI failed: $($output -join [Environment]::NewLine)" }
    return ($output -join [Environment]::NewLine)
}
function Read-Release {
    return (Invoke-Gh @('api', "repos/$Repository/releases/tags/$ArchiveTag")) | ConvertFrom-Json
}
function File-Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
# Check every local archive before publishing or deleting any of them.
$verified = @()
foreach ($build in $older) {
    $zip = Assert-LocalPath $build.FullName
    $checksum = Assert-LocalPath "$zip.sha256"
    $hash = File-Hash $zip
    $line = (Get-Content -LiteralPath $checksum -Raw).Trim()
    if ($line -notmatch '^([a-fA-F0-9]{64})\s+(.+)$' -or
        $Matches[1].ToLowerInvariant() -ne $hash -or $Matches[2] -ne $build.Name) {
        throw "Invalid local checksum: $checksum"
    }
    $directory = $zip.Substring(0, $zip.Length - 4)
    if (Test-Path -LiteralPath $directory) {
        $directory = Assert-LocalPath $directory
        if (-not (Get-Item -LiteralPath $directory).PSIsContainer) { throw "Expected build directory: $directory" }
        foreach ($entry in Get-ChildItem -LiteralPath $directory -Recurse -Force) { $null = Assert-LocalPath $entry.FullName }
    }
    $verified += [pscustomobject]@{ Zip = $zip; Checksum = $checksum; Directory = $directory; Hash = $hash; ChecksumHash = (File-Hash $checksum) }
}
$marker = '<!-- toolbox-dist-archive:v1 -->'
try { $release = Read-Release }
catch {
    if ($_.Exception.Message -notmatch 'HTTP 404') { throw }
    # Push only a new archive tag, without updating the user's branch.
    Push-Location $projectRoot
    try {
        $remote = & git remote get-url origin
        if ($LASTEXITCODE -ne 0 -or $remote -notin @("https://github.com/$Repository.git", "git@github.com:$Repository.git")) {
            throw 'Archive repository does not match origin.'
        }
        $null = & git rev-parse --verify --quiet "refs/tags/$ArchiveTag"
        if ($LASTEXITCODE -ne 0) {
            & git tag $ArchiveTag HEAD
            if ($LASTEXITCODE -ne 0) { throw 'Could not create archive tag.' }
        }
        & git push origin "refs/tags/$ArchiveTag"
        if ($LASTEXITCODE -ne 0) { throw 'Could not publish archive tag; local builds retained.' }
    } finally { Pop-Location }
    $null = Invoke-Gh @('release', 'create', $ArchiveTag, '--repo', $Repository, '--verify-tag', '--prerelease', '--latest=false',
        '--title', 'Toolbox historical development builds', '--notes', "$marker`nHistorical Windows x64 build archives. Each filename records its own build time; this is a collection, not one unified source build. ZIPs include Qt/MSVC runtime files; SHA-256 files accompany them.")
    $release = Read-Release
}
if ($release.body -notlike "*$marker*") { throw 'Existing release is not a Toolbox dist archive; refusing to modify it.' }
function Assert-RemoteAsset($Release, [string]$Path) {
    $file = Get-Item -LiteralPath $Path
    $assets = @($Release.assets | Where-Object { $_.name -ceq $file.Name })
    if ($assets.Count -ne 1 -or $assets[0].state -ne 'uploaded' -or $assets[0].size -ne $file.Length) {
        throw "Remote asset missing, unfinished, or wrong size: $($file.Name)"
    }
    $expected = File-Hash $Path
    if ($assets[0].digest) {
        if ($assets[0].digest.ToLowerInvariant() -ne "sha256:$expected") { throw "Remote digest mismatch: $($file.Name)" }
    } else {
        # Older GitHub assets may lack digest. Download and hash before pruning.
        $verifyDir = Join-Path $projectRoot "out/release-verification/$([Guid]::NewGuid().ToString('N'))"
        $null = New-Item -ItemType Directory -Path $verifyDir -Force
        $null = Invoke-Gh @('release', 'download', $ArchiveTag, '--repo', $Repository, '--pattern', $file.Name, '--dir', $verifyDir)
        if ((File-Hash (Join-Path $verifyDir $file.Name)) -ne $expected) { throw "Downloaded asset hash mismatch: $($file.Name)" }
    }
}
foreach ($build in $verified) {
    foreach ($path in @($build.Zip, $build.Checksum)) {
        $name = [IO.Path]::GetFileName($path)
        if (-not @($release.assets | Where-Object { $_.name -ceq $name }).Count) {
            Write-Output "Uploading: $name"
            $null = Invoke-Gh @('release', 'upload', $ArchiveTag, $path, '--repo', $Repository)
            $release = Read-Release
        }
        Assert-RemoteAsset $release $path
    }
    # Recheck local files and paths immediately before deleting a verified build.
    $zip = Assert-LocalPath $build.Zip; $checksum = Assert-LocalPath $build.Checksum
    if ((File-Hash $zip) -ne $build.Hash -or (File-Hash $checksum) -ne $build.ChecksumHash) { throw 'Local archive changed during upload; retained.' }
    if (Test-Path -LiteralPath $build.Directory) {
        $directory = Assert-LocalPath $build.Directory
        foreach ($entry in Get-ChildItem -LiteralPath $directory -Recurse -Force) { $null = Assert-LocalPath $entry.FullName }
        # Do not partially prune a package whose executable is still in use.
        foreach ($process in Get-Process -Name Toolbox -ErrorAction SilentlyContinue) {
            if (-not $process.Path -or $process.Path.StartsWith($directory + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Archive uploaded and verified, but build is running (PID $($process.Id)); local files retained: $directory"
            }
        }
        Remove-Item -LiteralPath $directory -Recurse -Force
    }
    Remove-Item -LiteralPath $zip, $checksum -Force
    Write-Output "Archived and pruned: $([IO.Path]::GetFileName($zip))"
}
Write-Output "Archive release: $($release.html_url)"
