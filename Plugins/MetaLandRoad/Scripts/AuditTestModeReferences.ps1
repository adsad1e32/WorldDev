# Scans MetaLandRoad Content uassets for embedded "/TestMode/" path strings.
# Run: powershell -File Plugins/MetaLandRoad/Scripts/AuditTestModeReferences.ps1

$ContentRoot = Join-Path $PSScriptRoot "..\Content" | Resolve-Path
$AllAssets = Get-ChildItem -Path $ContentRoot -Filter "*.uasset" -Recurse -File
$WithTestMode = @()
$Clean = @()

foreach ($Asset in $AllAssets) {
    $Bytes = [System.IO.File]::ReadAllBytes($Asset.FullName)
    $Text = [System.Text.Encoding]::ASCII.GetString($Bytes)
    if ($Text -match '/TestMode/|/Script/TestMode') {
        $WithTestMode += $Asset.FullName.Substring($ContentRoot.Path.Length + 1)
    }
    else {
        $Clean += $Asset.FullName.Substring($ContentRoot.Path.Length + 1)
    }
}

Write-Host "=== MetaLandRoad TestMode Reference Audit ===" -ForegroundColor Cyan
Write-Host "Total uassets: $($AllAssets.Count)"
Write-Host "Still contain /TestMode/: $($WithTestMode.Count)" -ForegroundColor Yellow
Write-Host "Clean: $($Clean.Count)" -ForegroundColor Green
Write-Host ""

if ($WithTestMode.Count -gt 0) {
    Write-Host "--- STILL REFERENCE TestMode ---" -ForegroundColor Yellow
    $WithTestMode | Sort-Object | ForEach-Object { Write-Host "  $_" }
}

Write-Host ""
Write-Host "--- CLEAN (no /TestMode/ string) ---" -ForegroundColor Green
$Clean | Sort-Object | ForEach-Object { Write-Host "  $_" }

Write-Host ""
Write-Host "Critical landscape pipeline assets:" -ForegroundColor Cyan
$Critical = @(
    "Landscape\BlueprintBrushes\MPC\MPC_Landscape.uasset",
    "Material\Landscape\Brush\CompositeMeshBodyTexture.uasset",
    "Material\Landscape\RenderMeshDepths.uasset",
    "Material\Landscape\MF\ReadCachedDistanceField.uasset",
    "Material\Landscape\DummyTexture.uasset",
    "Material\Landscape\CacheDistanceField.uasset",
    "Material\Landscape\LandmassBrush_Blender.uasset",
    "Material\Landscape\Brush\RoadMeshBrush_Weightmap.uasset"
)
foreach ($Rel in $Critical) {
    $Status = if ($WithTestMode -contains $Rel) { "BROKEN" } else { "OK" }
    $Color = if ($Status -eq "BROKEN") { "Red" } else { "Green" }
    Write-Host "  [$Status] $Rel" -ForegroundColor $Color
}
