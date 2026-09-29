param([string]$Libraries = '')
$ErrorActionPreference = 'Stop'
$petRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $Libraries) { $Libraries = Join-Path $petRoot 'vendor\libraries' }
$Libraries = [IO.Path]::GetFullPath($Libraries)
$petCommit = '25fed2f7e8411f2522448b58906774db99a14a08'
$petRemote = 'https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.69.git'
$petCache = Join-Path $petRoot ('.cache\dependencies\waveshare-' + $petCommit)
$petLibraryNames = @('GFX_Library_for_Arduino', 'SensorLib')
$petUpstreamPrefix = 'examples/arduino/libraries/'
$petGit = (Get-Command git -ErrorAction Stop).Source
function Invoke-PetGit {
    param([string[]]$GitArguments)
    & $petGit @GitArguments
    if ($LASTEXITCODE -ne 0) { throw "Git failed ($LASTEXITCODE)." }
}
New-Item -ItemType Directory -Force -Path $petCache | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $petCache '.git') -PathType Container)) {
    Invoke-PetGit @('-C', $petCache, 'init', '--quiet')
    Invoke-PetGit @('-C', $petCache, 'remote', 'add', 'origin', $petRemote)
} else {
    $petActualRemote = & $petGit -C $petCache remote get-url origin
    if ($LASTEXITCODE -ne 0 -or $petActualRemote -ne $petRemote) {
        throw 'Dependency cache has a different origin. Choose a clean checkout; no files were replaced.'
    }
    $petChanges = & $petGit -C $petCache status --porcelain
    if ($LASTEXITCODE -ne 0 -or $petChanges) { throw 'Dependency cache has local changes; preserve them before retrying.' }
}
Invoke-PetGit @('-C', $petCache, 'sparse-checkout', 'init', '--cone')
Invoke-PetGit @('-C', $petCache, 'sparse-checkout', 'set', ($petUpstreamPrefix + $petLibraryNames[0]), ($petUpstreamPrefix + $petLibraryNames[1]))
Invoke-PetGit @('-C', $petCache, 'fetch', '--depth=1', '--filter=blob:none', 'origin', $petCommit)
Invoke-PetGit @('-C', $petCache, 'checkout', '--detach', 'FETCH_HEAD')
$petHead = & $petGit -C $petCache rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $petHead -ne $petCommit) { throw 'Dependency commit verification failed.' }
New-Item -ItemType Directory -Force -Path $Libraries | Out-Null
foreach ($petName in $petLibraryNames) {
    $petSource = Join-Path $petCache ($petUpstreamPrefix + $petName)
    $petTarget = Join-Path $Libraries $petName
    if (Test-Path -LiteralPath $petTarget) {
        $petSourceFiles = @(Get-ChildItem -LiteralPath $petSource -Recurse -File)
        $petTargetFiles = @(Get-ChildItem -LiteralPath $petTarget -Recurse -File)
        if ($petSourceFiles.Count -ne $petTargetFiles.Count) { throw "Existing $petName differs from the pinned source. No files were replaced." }
        foreach ($petFile in $petSourceFiles) {
            $petRelative = $petFile.FullName.Substring($petSource.Length).TrimStart('\', '/')
            $petPeer = Join-Path $petTarget $petRelative
            if (-not (Test-Path -LiteralPath $petPeer -PathType Leaf) -or
                (Get-FileHash -LiteralPath $petFile.FullName -Algorithm SHA256).Hash -ne
                (Get-FileHash -LiteralPath $petPeer -Algorithm SHA256).Hash) {
                throw "Existing $petName differs from the pinned source. No files were replaced."
            }
        }
        Write-Output "Verified existing $petName against $petCommit."
    } else {
        Copy-Item -LiteralPath $petSource -Destination $petTarget -Recurse
        Write-Output "Fetched $petName at $petCommit."
    }
}
# Keep the parent project's license alongside the libraries' own notices.
$petParentLicense = Join-Path $Libraries 'WAVESHARE-LICENSE'
if (-not (Test-Path -LiteralPath $petParentLicense)) {
    Copy-Item -LiteralPath (Join-Path $petCache 'LICENSE') -Destination $petParentLicense
}
Write-Output "Dependencies ready: $Libraries"
