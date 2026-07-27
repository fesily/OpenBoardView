# Build web/dist if needed and start obv_server with data + SPA static root.
# Usage: .\scripts\run_obv_server.ps1 [-Host 127.0.0.1] [-Port 8080] [-Data path] [-Www path]
param(
    [string]$BindHost = $(if ($env:OBV_HOST) { $env:OBV_HOST } else { "127.0.0.1" }),
    [int]$Port = $(if ($env:OBV_PORT) { [int]$env:OBV_PORT } else { 8080 }),
    [string]$Data = $(if ($env:OBV_DATA) { $env:OBV_DATA } else { "" }),
    [string]$Www = $(if ($env:OBV_WWW) { $env:OBV_WWW } else { "" }),
    [string]$BuildDir = $(if ($env:OBV_BUILD_DIR) { $env:OBV_BUILD_DIR } else { "" })
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Data) { $Data = Join-Path $Root "data" }
if (-not $Www) { $Www = Join-Path $Root "web\dist" }
if (-not $BuildDir) { $BuildDir = Join-Path $Root "build-web" }

$index = Join-Path $Www "index.html"
if (-not (Test-Path $index)) {
    Write-Host "web dist missing; running npm run build in web/"
    Push-Location (Join-Path $Root "web")
    try {
        npm run build
    } finally {
        Pop-Location
    }
}

$candidates = @(
    (Join-Path $BuildDir "src\obv_server\Release\obv_server.exe"),
    (Join-Path $BuildDir "src\obv_server\Debug\obv_server.exe"),
    (Join-Path $BuildDir "src\obv_server\obv_server.exe"),
    (Join-Path $BuildDir "src\obv_server\obv_server"),
    (Join-Path $Root "openboardview")
)

$server = $null
foreach ($c in $candidates) {
    if (Test-Path $c) {
        $server = $c
        break
    }
}
if (-not $server) {
    Write-Error "obv_server binary not found under $BuildDir. Build with: cmake --build $BuildDir --config Release --target obv_server"
}

New-Item -ItemType Directory -Force -Path (Join-Path $Data "boards") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Data "overlays") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Data "config") | Out-Null

$exampleSrc = Join-Path $Root "data\config\keys.example.json"
$exampleDst = Join-Path $Data "config\keys.example.json"
if ((Test-Path $exampleSrc) -and -not (Test-Path $exampleDst)) {
    Copy-Item $exampleSrc $exampleDst -ErrorAction SilentlyContinue
}

Write-Host "Starting: $server --host $BindHost --port $Port --data $Data --www $Www"
& $server --host $BindHost --port $Port --data $Data --www $Www @args
