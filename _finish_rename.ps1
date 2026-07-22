$ErrorActionPreference = 'Continue'
$ProjectRoot = 'D:\UE_Project\WorldDev'
$PluginRoot = Join-Path $ProjectRoot 'Plugins/MetaLandRoad'
$ModuleRoot = Join-Path $PluginRoot 'Source/MetaLandRoad'

Get-ChildItem -Path $ModuleRoot -Recurse -File | Where-Object { $_.Name -match 'TestMode' } | ForEach-Object {
    $newName = $_.Name -replace 'TestMode', 'MetaLandRoad'
    $dest = Join-Path $_.DirectoryName $newName
    if (-not (Test-Path $dest)) {
        Rename-Item $_.FullName $newName -Force
        Write-Host "Renamed $($_.Name) -> $newName"
    }
}

$uplugin = Join-Path $PluginRoot 'MetaLandRoad.uplugin'
$c = [System.IO.File]::ReadAllText($uplugin)
$c = $c.Replace('TestMode', 'MetaLandRoad')
[System.IO.File]::WriteAllText($uplugin, $c)
Write-Host 'Updated MetaLandRoad.uplugin'

$replaceTargets = @(
    (Join-Path $PluginRoot 'Source'),
    (Join-Path $PluginRoot 'Config'),
    (Join-Path $ProjectRoot 'Config'),
    (Join-Path $PluginRoot 'Content')
) | Where-Object { Test-Path $_ }

$replacements = @(
    @('TESTMODE_API', 'METALANDROAD_API'),
    @('/Script/TestMode', '/Script/MetaLandRoad'),
    @('/TestMode/', '/MetaLandRoad/'),
    @('FindPlugin(TEXT("TestMode"))', 'FindPlugin(TEXT("MetaLandRoad"))'),
    @('EM_TestModeEditorMode', 'EM_MetaLandRoadEditorMode'),
    @('TestModeEditorModeToolkit', 'MetaLandRoadEditorModeToolkit'),
    @('TestModeEditorModeCommands', 'MetaLandRoadEditorModeCommands'),
    @('TestModeEditorMode', 'MetaLandRoadEditorMode'),
    @('TestModeInteractiveTool', 'MetaLandRoadInteractiveTool'),
    @('TestModeStyle', 'MetaLandRoadStyle'),
    @('FTestMode', 'FMetaLandRoad'),
    @('UTestMode', 'UMetaLandRoad'),
    @('TestMode', 'MetaLandRoad')
)

foreach ($target in $replaceTargets) {
    Get-ChildItem -Path $target -Recurse -File -Include *.cpp,*.h,*.cs,*.ini,*.uplugin,*.json -ErrorAction SilentlyContinue | ForEach-Object {
        $content = [System.IO.File]::ReadAllText($_.FullName)
        $original = $content
        foreach ($pair in $replacements) { $content = $content.Replace($pair[0], $pair[1]) }
        if ($content -ne $original) { [System.IO.File]::WriteAllText($_.FullName, $content) }
    }
}

Set-Location $ProjectRoot
git add -A
git status --short
git commit -m "Rename TestMode plugin to MetaLandRoad"
git remote -v
$branch = git branch --show-current
if (-not $branch) { git checkout -b main; $branch = 'main' }
if ((git remote) -contains 'origin') {
    git push -u origin $branch
} else {
    Write-Host 'NO_REMOTE'
}
