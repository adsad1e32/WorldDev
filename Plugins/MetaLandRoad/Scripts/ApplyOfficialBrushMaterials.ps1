# Apply official Water/Landmass brush materials via UE Python.
# Close other UE instances first.

param(
    [switch]$DuplicateToMetaLandRoad,
    [string]$ProjectPath = "D:\UE_Project\WorldDev\WorldDev.uproject",
    [string]$EditorPath = ""
)

$ErrorActionPreference = "Stop"

function Find-UnrealEditor {
    param([string]$Override)
    if ($Override -and (Test-Path $Override)) { return $Override }

    $candidates = @(
        "J:\UnrealEngine-release\UnrealEngine-release\Engine\Binaries\Win64\UnrealEditor.exe",
        "C:\Program Files\Epic Games\UE_5.4\Engine\Binaries\Win64\UnrealEditor.exe",
        "C:\Program Files\Epic Games\UE_5.3\Engine\Binaries\Win64\UnrealEditor.exe"
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) { return $path }
    }
    throw "UnrealEditor.exe not found. Pass -EditorPath."
}

$Editor = Find-UnrealEditor -Override $EditorPath
$Script = if ($DuplicateToMetaLandRoad) {
    Join-Path $PSScriptRoot "ApplyOfficialBrushMaterialsDuplicate.py"
} else {
    Join-Path $PSScriptRoot "ApplyOfficialBrushMaterials.py"
}

if (-not (Test-Path $Script)) { throw "Missing script: $Script" }
if (-not (Test-Path $ProjectPath)) { throw "Missing project: $ProjectPath" }

Write-Host "=== Apply Official Brush Materials ===" -ForegroundColor Cyan
Write-Host "Editor:  $Editor"
Write-Host "Project: $ProjectPath"
Write-Host "Script:  $Script"
Write-Host ""

& $Editor $ProjectPath `
    -ExecutePythonScript="$Script" `
    -stdout `
    -unattended `
    -nosplash `
    -NullRHI

Write-Host ""
Write-Host "If headless run failed, open UE manually:" -ForegroundColor Yellow
Write-Host "  Tools -> Execute Python Script -> $Script"
