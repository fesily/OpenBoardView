# Run Agent pin/part API tests (auto-starts obv_server unless -NoStart).
# Usage:
#   .\scripts\test_agent_api.ps1
#   .\scripts\test_agent_api.ps1 -Port 18080 -Boards data\boards
#   .\scripts\test_agent_api.ps1 -NoStart -Base http://127.0.0.1:8080
#   .\scripts\test_agent_api.ps1 -Filter pin,conditions
param(
    [string]$Base = $(if ($env:OBV_TEST_BASE) { $env:OBV_TEST_BASE } else { "" }),
    [string]$Boards = $(if ($env:OBV_TEST_BOARDS) { $env:OBV_TEST_BOARDS } else { "" }),
    [string]$Server = $(if ($env:OBV_TEST_SERVER) { $env:OBV_TEST_SERVER } else { "" }),
    [int]$Port = $(if ($env:OBV_TEST_PORT) { [int]$env:OBV_TEST_PORT } else { 18080 }),
    [string]$Filter = "",
    [switch]$NoStart,
    [switch]$List
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Py = Join-Path $Root "scripts\test_agent_api.py"

$argsList = @()
if ($List) { $argsList += "--list" }
if ($Base) { $argsList += @("--base", $Base) }
if ($Boards) { $argsList += @("--boards", $Boards) }
if ($Server) { $argsList += @("--server", $Server) }
if ($Port) { $argsList += @("--port", "$Port") }
if ($Filter) { $argsList += @("-k", $Filter) }
if ($NoStart) { $argsList += "--no-start" }

Write-Host "python $Py $($argsList -join ' ')"
& python $Py @argsList
exit $LASTEXITCODE
