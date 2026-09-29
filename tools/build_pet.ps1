param(
    [string]$ArduinoCli = 'arduino-cli',
    [string]$ConfigFile = '',
    [string]$Libraries = '',
    [ValidateRange(1, 64)][int]$Jobs = 6
)
$ErrorActionPreference = 'Stop'
$petRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $Libraries) { $Libraries = Join-Path $petRoot 'vendor\libraries' }
if (-not (Test-Path -LiteralPath $Libraries -PathType Container)) {
    throw 'Libraries missing. Run tools/fetch_dependencies.ps1 or pass -Libraries.'
}
$petCli = (Get-Command $ArduinoCli -ErrorAction Stop).Source
$petConfigArgs = @()
if ($ConfigFile) { $petConfigArgs = @('--config-file', $ConfigFile) }
$petCoreOutput = & $petCli core list @petConfigArgs --format json
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect installed Arduino cores.' }
$petCores = ($petCoreOutput -join "`n") | ConvertFrom-Json
$petCore = @($petCores.platforms) | Where-Object { $_.id -eq 'esp32:esp32' }
if (-not $petCore -or $petCore.installed_version -ne '3.3.11') {
    throw 'This firmware requires Arduino-ESP32 3.3.11. Install esp32:esp32@3.3.11 with Arduino CLI.'
}
$petFqbn = 'esp32:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=default,PartitionScheme=app3M_fat9M_16MB'
& $petCli compile @petConfigArgs --fqbn $petFqbn --jobs $Jobs `
    --libraries $Libraries `
    --build-path (Join-Path $petRoot 'build\firmware-work') `
    --output-dir (Join-Path $petRoot 'firmware') `
    (Join-Path $petRoot 'sketch\DeskPetV01')
if ($LASTEXITCODE -ne 0) { throw "Firmware compilation failed ($LASTEXITCODE)." }
Write-Output 'Compilation complete. No serial port opened and no firmware flashed.'
