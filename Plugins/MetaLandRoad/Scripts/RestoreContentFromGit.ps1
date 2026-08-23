# Restores MetaLandRoad Content uassets from git (reverts corrupted binary patches).
# CLOSE Unreal Editor and Visual Studio before running.
# Usage: powershell -ExecutionPolicy Bypass -File RestoreContentFromGit.ps1

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path

Write-Host "Restoring Plugins/MetaLandRoad/Content from git..." -ForegroundColor Cyan
Write-Host "Repo: $RepoRoot"
Write-Host ""

Push-Location $RepoRoot
try {
    git checkout -- "Plugins/MetaLandRoad/Content"
    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "If checkout failed with 'unable to unlink', close Unreal Editor and retry." -ForegroundColor Yellow
        exit $LASTEXITCODE
    }

    # Git LFS: re-fetch pointer targets if repo uses LFS for uassets
    $lfs = git lfs version 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Running git lfs checkout for Content..." -ForegroundColor Cyan
        git lfs checkout "Plugins/MetaLandRoad/Content"
    }

    Write-Host ""
    Write-Host "Restore complete. Restart Unreal Editor." -ForegroundColor Green
}
finally {
    Pop-Location
}
