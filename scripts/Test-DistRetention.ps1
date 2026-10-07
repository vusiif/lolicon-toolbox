# Isolated retention tests: no network, real releases, or project dist mutations.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path (Split-Path -Parent $PSScriptRoot) "out/retention-tests/$([Guid]::NewGuid().ToString('N'))"
$null = New-Item -ItemType Directory -Path $testRoot -Force
$mockPath = Join-Path $testRoot 'Mock-Gh.ps1'
@'
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
$ErrorActionPreference = 'Stop'
$statePath = Join-Path $PSScriptRoot 'remote.json'
$state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
if ($Arguments[0] -eq 'api') { $state | ConvertTo-Json -Depth 10; exit 0 }
if ($Arguments[0] -eq 'release' -and $Arguments[1] -eq 'upload') {
    $file = Get-Item -LiteralPath $Arguments[3]
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($state.badDigest) { $hash = '0' * 64 }
    $state.assets = @($state.assets) + [pscustomobject]@{ name = $file.Name; state = 'uploaded'; size = $file.Length; digest = "sha256:$hash" }
    $state | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $statePath -Encoding utf8
    exit 0
}
throw "Unexpected mocked command: $Arguments"
'@ | Set-Content -LiteralPath $mockPath -Encoding utf8
function Prepare([string]$Name, [bool]$BadDigest) {
    $root = Join-Path $testRoot $Name
    $scripts = Join-Path $root 'scripts'; $dist = Join-Path $root 'dist'
    $null = New-Item -ItemType Directory -Path $scripts, $dist -Force
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Archive-Old-Dist.ps1') -Destination $scripts
    for ($index = 1; $index -le 5; $index++) {
        $name = 'Toolbox-0.2.0-windows-x64-20261001-00000' + $index
        $directory = Join-Path $dist $name
        $null = New-Item -ItemType Directory -Path $directory
        Set-Content -LiteralPath (Join-Path $directory 'payload.txt') -Value "build $index" -Encoding utf8
        Compress-Archive -LiteralPath $directory -DestinationPath "$directory.zip"
        $hash = (Get-FileHash -LiteralPath "$directory.zip" -Algorithm SHA256).Hash
        "$hash  $name.zip" | Set-Content -LiteralPath "$directory.zip.sha256" -Encoding utf8
    }
    @{ body = '<!-- toolbox-dist-archive:v1 -->'; html_url = 'https://example.invalid/archive'; assets = @(); badDigest = $BadDigest } |
        ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $testRoot 'remote.json') -Encoding utf8
    return $root
}
$root = Prepare 'success' $false
& (Join-Path $root 'scripts/Archive-Old-Dist.ps1') -GhPath $mockPath
$left = @(Get-ChildItem -LiteralPath (Join-Path $root 'dist'))
if ($left.Count -ne 9 -or @($left | Where-Object { $_.Name -match '00000[12](\.zip(\.sha256)?)?$' }).Count) { throw 'Newest-three retention failed' }
$state = Get-Content -LiteralPath (Join-Path $testRoot 'remote.json') -Raw | ConvertFrom-Json
if ($state.assets.Count -ne 4) { throw 'Expected two ZIP/checksum pairs remotely' }
$root = Prepare 'wrong-digest' $true
$rejected = $false
try { & (Join-Path $root 'scripts/Archive-Old-Dist.ps1') -GhPath $mockPath }
catch { if ($_.Exception.Message -notlike '*Remote digest mismatch*') { throw }; $rejected = $true }
if (-not $rejected -or @(Get-ChildItem -LiteralPath (Join-Path $root 'dist')).Count -ne 15) { throw 'Digest mismatch did not preserve local builds' }
$root = Prepare 'wrong-checksum' $false
Set-Content -LiteralPath (Join-Path $root 'dist/Toolbox-0.2.0-windows-x64-20261001-000001.zip.sha256') -Value 'invalid'
$rejected = $false
try { & (Join-Path $root 'scripts/Archive-Old-Dist.ps1') -GhPath $mockPath }
catch { if ($_.Exception.Message -notlike '*Invalid local checksum*') { throw }; $rejected = $true }
if (-not $rejected -or @(Get-ChildItem -LiteralPath (Join-Path $root 'dist')).Count -ne 15) { throw 'Bad checksum did not preserve local builds' }
$state = Get-Content -LiteralPath (Join-Path $testRoot 'remote.json') -Raw | ConvertFrom-Json
if ($state.assets.Count -ne 0) { throw 'Uploaded before validating all checksums' }
Write-Output 'PASS: newest-three retention; remote digest mismatch; local checksum failure.'
