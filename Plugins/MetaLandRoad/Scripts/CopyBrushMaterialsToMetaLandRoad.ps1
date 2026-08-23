# Copy Water/Landmass brush materials into MetaLandRoad Content (run once, UE closed or use editor script directly).

param(
    [string]$ProjectPath = "D:\UE_Project\WorldDev\WorldDev.uproject",
    [string]$EditorPath = ""
)

$ErrorActionPreference = "Stop"
$Script = Join-Path $PSScriptRoot "CopyBrushMaterialsToMetaLandRoad.py"

Write-Host "Run this INSIDE UE Editor (recommended):" -ForegroundColor Cyan
Write-Host "  Tools -> Execute Python Script -> $Script"
Write-Host ""
Write-Host "Headless attempt (may not work for all projects):" -ForegroundColor Yellow

if (-not (Test-Path $Script)) { throw "Missing: $Script" }

$candidates = @(
    "J:\UnrealEngine-release\UnrealEngine-release\Engine\Binaries\Win64\UnrealEditor.exe",
    "C:\Program Files\Epic Games\UE_5.4\Engine\Binaries\Win64\UnrealEditor.exe"
)
$Editor = if ($EditorPath -and (Test-Path $EditorPath)) { $EditorPath } else { ($candidates | Where-Object { Test-Path $_ } | Select-Object -First 1) }
if (-not $Editor) { throw "UnrealEditor.exe not found" }

& $Editor $ProjectPath -ExecutePythonScript="$Script" -stdout -unattended -nosplash -NullRHI
