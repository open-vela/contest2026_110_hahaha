param(
    [string]$Key = $env:MAPTILER_KEY,
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\assets\map'),
    [double]$MinLon = 114.06005859,
    [double]$MinLat = 32.11514862,
    [double]$MaxLon = 114.10949707,
    [double]$MaxLat = 32.17096284,
    [int[]]$Zoom = @(16, 17),
    [string]$Style = 'streets-v4',
    [int]$DelayMs = 100,
    [int]$Parallel = 8,
    [switch]$Overwrite,
    [switch]$NormalizeToJpg,
    [int]$JpegQuality = 82
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Convert-LonToTileX {
    param(
        [double]$Lon,
        [int]$Z
    )

    return [int][Math]::Floor(($Lon + 180.0) / 360.0 * [Math]::Pow(2.0, $Z))
}

function Convert-LatToTileY {
    param(
        [double]$Lat,
        [int]$Z
    )

    $latRad = $Lat * [Math]::PI / 180.0
    return [int][Math]::Floor((1.0 - [Math]::Log([Math]::Tan($latRad) + (1.0 / [Math]::Cos($latRad))) / [Math]::PI) / 2.0 * [Math]::Pow(2.0, $Z))
}

if ([string]::IsNullOrWhiteSpace($Key)) {
    throw 'MapTiler key is empty. Set $env:MAPTILER_KEY or pass -Key.'
}

$outputPath = (New-Item -ItemType Directory -Force $OutputRoot).FullName
$tileJobs = @()

foreach ($z in $Zoom) {
    $x0 = Convert-LonToTileX -Lon $MinLon -Z $z
    $x1 = Convert-LonToTileX -Lon $MaxLon -Z $z
    $y0 = Convert-LatToTileY -Lat $MaxLat -Z $z
    $y1 = Convert-LatToTileY -Lat $MinLat -Z $z

    Write-Host "z$z x=$x0..$x1 y=$y0..$y1"

    for ($x = $x0; $x -le $x1; $x++) {
        for ($y = $y0; $y -le $y1; $y++) {
            $tileDir = Join-Path $outputPath "$z\$x\$y"
            $tilePath = Join-Path $tileDir 'tile.png'

            if ((Test-Path -LiteralPath $tilePath) -and -not $Overwrite) {
                continue
            }

            $url = "https://api.maptiler.com/maps/$Style/256/$z/$x/$y.png?key=$Key"
            $tileJobs += [pscustomobject]@{
                Z = $z
                X = $x
                Y = $y
                Url = $url
                Dir = $tileDir
                Path = $tilePath
            }
        }
    }
}

$skipped = 0
$downloaded = 0
$failed = 0
$total = $tileJobs.Count
$completed = 0
$throttleLimit = [Math]::Max(1, $Parallel)
$runningJobs = @()

Write-Host "Pending: $total"
Write-Host "Parallel: $throttleLimit"

foreach ($tileJob in $tileJobs) {
    while ($runningJobs.Count -ge $throttleLimit) {
        $finishedJobs = $runningJobs | Where-Object { $_.State -ne 'Running' }
        foreach ($finishedJob in $finishedJobs) {
            $result = Receive-Job -Job $finishedJob
            Remove-Job -Job $finishedJob
            $runningJobs = @($runningJobs | Where-Object { $_.Id -ne $finishedJob.Id })
            $completed++

            if ($result.Status -eq 'downloaded') {
                $downloaded++
            } else {
                $failed++
                Write-Warning $result.Message
            }

            Write-Progress -Activity 'Downloading MapTiler tiles' -Status "$completed / $total" -PercentComplete (($completed * 100) / [Math]::Max(1, $total))
        }

        if ($runningJobs.Count -ge $throttleLimit) {
            Start-Sleep -Milliseconds 100
        }
    }

    $runningJobs += Start-Job -ArgumentList $tileJob, $DelayMs -ScriptBlock {
        param($TileJob, $WorkerDelayMs)

        try {
            New-Item -ItemType Directory -Force $TileJob.Dir | Out-Null
            Invoke-WebRequest -Uri $TileJob.Url -OutFile $TileJob.Path -Headers @{ 'User-Agent' = 'map-demo-offline-map/0.1' }
            if ($WorkerDelayMs -gt 0) {
                Start-Sleep -Milliseconds $WorkerDelayMs
            }

            [pscustomobject]@{
                Status = 'downloaded'
                Message = "Downloaded z=$($TileJob.Z) x=$($TileJob.X) y=$($TileJob.Y)"
            }
        } catch {
            [pscustomobject]@{
                Status = 'failed'
                Message = "Failed to download z=$($TileJob.Z) x=$($TileJob.X) y=$($TileJob.Y) : $($_.Exception.Message)"
            }
        }
    }
}

while ($runningJobs.Count -gt 0) {
    $finishedJobs = $runningJobs | Where-Object { $_.State -ne 'Running' }
    foreach ($finishedJob in $finishedJobs) {
        $result = Receive-Job -Job $finishedJob
        Remove-Job -Job $finishedJob
        $runningJobs = @($runningJobs | Where-Object { $_.Id -ne $finishedJob.Id })
        $completed++

        if ($result.Status -eq 'downloaded') {
            $downloaded++
        } else {
            $failed++
            Write-Warning $result.Message
        }

        Write-Progress -Activity 'Downloading MapTiler tiles' -Status "$completed / $total" -PercentComplete (($completed * 100) / [Math]::Max(1, $total))
    }

    if ($runningJobs.Count -gt 0) {
        Start-Sleep -Milliseconds 100
    }
}

Write-Progress -Activity 'Downloading MapTiler tiles' -Completed

Write-Host "Output: $outputPath"
Write-Host "Downloaded: $downloaded"
Write-Host "Skipped: $skipped"
Write-Host "Failed: $failed"

if ($failed -gt 0) {
    exit 1
}

if ($NormalizeToJpg) {
    $normalizeScript = Join-Path $PSScriptRoot 'normalize_map_tiles_to_jpg.ps1'
    & powershell -ExecutionPolicy Bypass -File $normalizeScript -Root $outputPath -Quality $JpegQuality -RemovePng
}
