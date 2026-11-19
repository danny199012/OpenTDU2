# Android build (arm64-v8a APK). Mirrors build.ps1's conventions where they
# apply: the shared FetchContent cache (C:\otdu2-deps, seed-deps-cache.ps1)
# is passed to CMake, and OTDU_RELEASE_TAG flows into the build stamp /
# versionName. Gradle drives AGP -> CMake (SDK cmake 3.31.1 + NDK 28.2, see
# android/app/build.gradle.kts); android/local.properties holds sdk.dir.
#
# Usage: .\build_android.ps1 [-Variant Debug|Release]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Variant = "Debug"
)

$ErrorActionPreference = "Stop"

if ( -not ( Test-Path "$PSScriptRoot\android\local.properties" ) ) {
    Write-Error "android/local.properties missing (sdk.dir=C:\\Android\\Sdk expected)"
}

# OTDU_RELEASE_TAG is optional; when set the APK versionName + the build stamp
# (cmake/StampBuildInfo.cmake) carry the tag, same as the desktop builds.
if ( $env:OTDU_RELEASE_TAG ) {
    Write-Host "OTDU_RELEASE_TAG = $env:OTDU_RELEASE_TAG"
}

$gradlew = Join-Path $PSScriptRoot "android\gradlew.bat"
& $gradlew -p ( Join-Path $PSScriptRoot "android" ) "--console=plain" "assemble$Variant" @args
if ( $LASTEXITCODE -ne 0 ) {
    Write-Error "gradlew assemble$Variant failed ($LASTEXITCODE)"
}

$apk = Join-Path $PSScriptRoot "android\app\build\outputs\apk\$Variant\app-$Variant.apk"
if ( Test-Path $apk ) {
    Write-Host ""
    Write-Host "APK: $apk"
    Write-Host "Install: copy to the phone (USB) and open it, or: adb install -r `"$apk`""
}
