# DO NOT USE — binary patching corrupts .uasset NameMap and crashes Asset Registry.
#
# Safe fix for TestMode -> MetaLandRoad references:
#   1. Keep CoreRedirects in Config/DefaultMetaLandRoad.ini (PackageRedirects only)
#   2. Close UE, run RestoreContentFromGit.ps1 if assets were already patched
#   3. Open UE -> Fix Up Redirectors on Content/Material/Landscape
#   4. Manually set Collection Param -> MPC_Landscape on landscape materials, Save
#   5. Or: UnrealEditor-Cmd -run=ResavePackages -FixUpRedirects -Package=/MetaLandRoad/...

Write-Error @"
FixTestModeUassetPaths.ps1 is DISABLED.

Binary string replacement breaks uasset NameMap headers and causes crashes in:
  FPackageReader::SerializeNameMap -> FNamePool::Store

Use CoreRedirects + UE editor Fix Up Redirectors / ResavePackages instead.
See RestoreContentFromGit.ps1 to revert corrupted files from git.
"@

exit 1
