# Builds the Android APK and stages it for a GitHub release.
#   .\release_android.ps1            builds, copies to _release\opentdu2-android-b<build>.apk
#   .\release_android.ps1 -Upload    also runs `gh release create` (needs the GitHub CLI + login)
# The launcher's Updates page reads the number after "-b" and compares it with the installed build,
# so the asset name must keep that shape.
param( [switch]$Upload )
$ErrorActionPreference = "Stop"
& "$PSScriptRoot\build_android.ps1"
$apk = Join-Path $PSScriptRoot "android\app\build\outputs\apk\debug\app-debug.apk"
$build = & python -c "import zipfile,re,sys; d=zipfile.ZipFile(sys.argv[1]).read('lib/arm64-v8a/libopentdu2.so'); print(max(int(x) for x in re.findall(rb'build (\d+) \(', d)))" $apk
$dir = Join-Path $PSScriptRoot "_release"
New-Item -ItemType Directory -Force $dir | Out-Null
$dest = Join-Path $dir "opentdu2-android-b$build.apk"
Copy-Item $apk $dest -Force
Write-Host "Staged $dest"
if ( $Upload ) {
    if ( -not ( Get-Command gh -ErrorAction SilentlyContinue ) ) { Write-Error "gh (GitHub CLI) not found: upload $dest by hand at https://github.com/danny199012/OpenTDU2/releases/new" }
    gh release create "android-b$build" $dest --title "Android build $build" --notes "OpenTDU2 Android build $build (debug-signed APK)."
} else {
    Write-Host "Upload it at https://github.com/danny199012/OpenTDU2/releases/new (tag android-b$build)."
}
