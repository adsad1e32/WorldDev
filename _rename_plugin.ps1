$ErrorActionPreference = 'Stop'
$ProjectRoot = 'D:\UE_Project\WorldDev'
$OldPlugin = Join-Path $ProjectRoot 'Plugins\TestMode'
$NewPlugin = Join-Path $ProjectRoot 'Plugins/MetaLandRoad'
$Log = Join-Path $ProjectRoot 'RENAME_SUMMARY.txt'

function Log($msg) {
    $line = "[$(Get-Date -Format 'HH:mm:ss')] $msg"
    Write-Host $line
    Add-Content -Path $Log -Value $line
}

Remove-Item $Log -ErrorAction SilentlyContinue
Log 'Starting TestMode -> MetaLandRoad migration'

if (-not (Test-Path $OldPlugin) -and -not (Test-Path $NewPlugin)) {
    throw 'Neither TestMode nor MetaLandRoad plugin folder found'
}

if (-not (Test-Path $NewPlugin)) {
    New-Item -ItemType Directory -Path $NewPlugin -Force | Out-Null
    Log 'Created MetaLandRoad folder'

    robocopy $OldPlugin $NewPlugin /E /XD Intermediate Binaries Saved /NFL /NDL /NJH /NJS /NC /NS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }
    Log 'Copied plugin files (excluding build artifacts)'
} else {
    Log 'MetaLandRoad folder already exists, continuing in-place updates'
}

$PluginRoot = $NewPlugin
$srcOld = Join-Path $PluginRoot 'Source/TestMode'
$srcNew = Join-Path $PluginRoot 'Source/MetaLandRoad'
if (Test-Path $srcOld) {
    if (Test-Path $srcNew) { Remove-Item $srcNew -Recurse -Force }
    Rename-Item $srcOld $srcNew
    Log 'Renamed Source/TestMode -> Source/MetaLandRoad'
}

$upluginOld = Join-Path $PluginRoot 'TestMode.uplugin'
$upluginNew = Join-Path $PluginRoot 'MetaLandRoad.uplugin'
if (Test-Path $upluginOld) {
    if (Test-Path $upluginNew) { Remove-Item $upluginNew -Force }
    Rename-Item $upluginOld $upluginNew
    Log 'Renamed TestMode.uplugin -> MetaLandRoad.uplugin'
}

$ModuleRoot = Join-Path $PluginRoot 'Source/MetaLandRoad'
$fileRenames = @(
    'TestMode.Build.cs', 'MetaLandRoad.Build.cs',
    'TestModeModule.h', 'MetaLandRoadModule.h',
    'TestModeModule.cpp', 'MetaLandRoadModule.cpp',
    'TestModeStyle.h', 'MetaLandRoadStyle.h',
    'TestModeStyle.cpp', 'MetaLandRoadStyle.cpp',
    'TestModeEditorMode.h', 'MetaLandRoadEditorMode.h',
    'TestModeEditorMode.cpp', 'MetaLandRoadEditorMode.cpp',
    'TestModeEditorModeToolkit.h', 'MetaLandRoadEditorModeToolkit.h',
    'TestModeEditorModeToolkit.cpp', 'MetaLandRoadEditorModeToolkit.cpp',
    'TestModeEditorModeCommands.h', 'MetaLandRoadEditorModeCommands.h',
    'TestModeEditorModeCommands.cpp', 'MetaLandRoadEditorModeCommands.cpp',
    'Tools/TestModeInteractiveTool.h', 'Tools/MetaLandRoadInteractiveTool.h',
    'Tools/TestModeInteractiveTool.cpp', 'Tools/MetaLandRoadInteractiveTool.cpp'
)

for ($i = 0; $i -lt $fileRenames.Count; $i += 2) {
    $from = Join-Path $ModuleRoot $fileRenames[$i]
    $toName = Split-Path $fileRenames[$i + 1] -Leaf
    $toDir = Split-Path (Join-Path $ModuleRoot $fileRenames[$i + 1]) -Parent
    $to = Join-Path $toDir $toName
    if (Test-Path $from) {
        if (Test-Path $to) { Remove-Item $to -Force }
        Rename-Item $from $toName -Force
        Log "Renamed file $($fileRenames[$i])"
    }
}

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

$extensions = @('*.cpp', '*.h', '*.cs', '*.ini', '*.uplugin', '*.json')
foreach ($target in $replaceTargets) {
    foreach ($ext in $extensions) {
        Get-ChildItem -Path $target -Filter $ext -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
            $content = [System.IO.File]::ReadAllText($_.FullName)
            $original = $content
            foreach ($pair in $replacements) {
                $content = $content.Replace($pair[0], $pair[1])
            }
            if ($content -ne $original) {
                [System.IO.File]::WriteAllText($_.FullName, $content)
            }
        }
    }
}
Log 'Applied text replacements'

Remove-Item (Join-Path $PluginRoot 'Intermediate') -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $PluginRoot 'Binaries') -Recurse -Force -ErrorAction SilentlyContinue

if (Test-Path $OldPlugin) {
    try {
        Remove-Item $OldPlugin -Recurse -Force
        Log 'Removed old TestMode folder'
    } catch {
        Log "Could not remove old TestMode folder (likely locked by UE/VS): $($_.Exception.Message)"
    }
}

$gitignore = Join-Path $ProjectRoot '.gitignore'
if (-not (Test-Path $gitignore)) {
    @'
# Unreal Engine
Binaries/
DerivedDataCache/
Intermediate/
Saved/
Build/
.vs/
*.VC.db
*.opensdf
*.opendb
*.sdf
*.sln
*.suo
*.xcodeproj
*.xcworkspace

# OS
Thumbs.db
Desktop.ini
.DS_Store

# Logs
*.log
'@ | Set-Content -Path $gitignore -Encoding UTF8
    Log 'Created .gitignore'
}

Set-Location $ProjectRoot
if (-not (Test-Path (Join-Path $ProjectRoot '.git'))) {
    git init | Out-Null
    Log 'Initialized git repository'
}

git add -A 2>&1 | Out-String | Add-Content $Log
$commit = git commit -m "Rename TestMode plugin to MetaLandRoad" 2>&1 | Out-String
Add-Content $Log $commit
Log 'Git commit attempted'

$remote = git remote -v 2>&1 | Out-String
Add-Content $Log "REMOTES:`n$remote"
if ($remote.Trim()) {
    $branch = git branch --show-current
    if (-not $branch) { $branch = 'master' }
    $push = git push -u origin $branch 2>&1 | Out-String
    Add-Content $Log "PUSH:`n$push"
} else {
    Add-Content $Log 'No git remote configured; push skipped'
}

$remaining = Select-String -Path (Join-Path $PluginRoot 'Source\*') -Pattern 'TestMode|TESTMODE' -SimpleMatch -ErrorAction SilentlyContinue
if ($remaining) {
    Log "WARNING: Remaining TestMode references: $($remaining.Count)"
    $remaining | Select-Object -First 20 | ForEach-Object { Add-Content $Log $_.ToString() }
} else {
    Log 'No remaining TestMode references in Source'
}

Remove-Item (Join-Path $ProjectRoot '_rename_plugin.ps1') -Force -ErrorAction SilentlyContinue
Log 'Done'
