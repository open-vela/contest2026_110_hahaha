param(
    [string]$Root = (Join-Path $PSScriptRoot '..\assets\map'),
    [int]$Quality = 82,
    [switch]$RemovePng
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

function Get-TileSignature {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $buffer = New-Object byte[] 8
        $read = $stream.Read($buffer, 0, $buffer.Length)

        if ($read -ge 8 -and $buffer[0] -eq 0x89 -and $buffer[1] -eq 0x50 -and
            $buffer[2] -eq 0x4E -and $buffer[3] -eq 0x47) {
            return 'png'
        }

        if ($read -ge 2 -and $buffer[0] -eq 0xFF -and $buffer[1] -eq 0xD8) {
            return 'jpeg'
        }

        return 'other'
    } finally {
        $stream.Dispose()
    }
}

function Get-JpegCodec {
    $codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() |
        Where-Object { $_.MimeType -eq 'image/jpeg' } |
        Select-Object -First 1

    if ($null -eq $codec) {
        throw 'JPEG encoder is not available on this Windows environment.'
    }

    return $codec
}

function Convert-ToTileJpg {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourcePath,
        [Parameter(Mandatory = $true)]
        [string]$TargetPath,
        [Parameter(Mandatory = $true)]
        [System.Drawing.Imaging.ImageCodecInfo]$JpegCodec,
        [Parameter(Mandatory = $true)]
        [System.Drawing.Imaging.EncoderParameters]$EncoderParameters
    )

    $tmpPath = "$TargetPath.tmp"
    if (Test-Path -LiteralPath $tmpPath) {
        Remove-Item -LiteralPath $tmpPath -Force
    }

    $image = [System.Drawing.Image]::FromFile($SourcePath)
    try {
        $image.Save($tmpPath, $JpegCodec, $EncoderParameters)
    } finally {
        $image.Dispose()
    }

    [System.IO.File]::Copy($tmpPath, $TargetPath, $true)
    Remove-Item -LiteralPath $tmpPath -Force
}

$rootPath = (Resolve-Path -LiteralPath $Root).Path
$jpegCodec = Get-JpegCodec
$encoderParameters = New-Object System.Drawing.Imaging.EncoderParameters 1
$encoderParameters.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), ([int64]$Quality)
$converted = 0
$copied = 0
$removed = 0
$skipped = 0
$failed = 0

Get-ChildItem -LiteralPath $rootPath -Recurse -File -Filter 'tile.png' | ForEach-Object {
    $targetPath = Join-Path $_.DirectoryName 'tile.jpg'
    $signature = Get-TileSignature -Path $_.FullName

    try {
        if ($signature -eq 'jpeg') {
            [System.IO.File]::Copy($_.FullName, $targetPath, $true)
            $script:copied++
        } else {
            Convert-ToTileJpg -SourcePath $_.FullName -TargetPath $targetPath -JpegCodec $jpegCodec -EncoderParameters $encoderParameters
            $script:converted++
        }

        if ($RemovePng) {
            Remove-Item -LiteralPath $_.FullName -Force
            $script:removed++
        }
    } catch {
        $script:failed++
        Write-Warning "Failed to normalize $($_.FullName): $($_.Exception.Message)"
    }
}

Get-ChildItem -LiteralPath $rootPath -Recurse -File -Filter 'tile.jpg' | ForEach-Object {
    if ((Get-TileSignature -Path $_.FullName) -eq 'jpeg') {
        $script:skipped++
    } else {
        try {
            Convert-ToTileJpg -SourcePath $_.FullName -TargetPath $_.FullName -JpegCodec $jpegCodec -EncoderParameters $encoderParameters
            $script:converted++
        } catch {
            $script:failed++
            Write-Warning "Failed to normalize $($_.FullName): $($_.Exception.Message)"
        }
    }
}

$encoderParameters.Dispose()

Write-Host "Root: $rootPath"
Write-Host "Quality: $Quality"
Write-Host "Converted: $converted"
Write-Host "Copied: $copied"
Write-Host "RemovedPng: $removed"
Write-Host "SkippedJpg: $skipped"
Write-Host "Failed: $failed"

if ($failed -gt 0) {
    exit 1
}
