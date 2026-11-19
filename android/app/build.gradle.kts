import java.util.Properties

plugins {
    id("com.android.application")
}

// Version stamp: OTDU_RELEASE_TAG (set by CI / build_android.ps1) wins, the
// launcher fallback (0.2.0) otherwise.
val releaseTag = System.getenv("OTDU_RELEASE_TAG") ?: ""

android {
    namespace = "org.opentdu2.game"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "org.opentdu2.game"
        minSdk = 29
        targetSdk = 36
        versionCode = 1
        versionName = if (releaseTag.isNotEmpty()) releaseTag.removePrefix("v") else "0.2.0"

        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    // Same offline FetchContent cache the desktop builds seed
                    // (seed-deps-cache.ps1 / build.ps1).
                    "-DOTDU_DEPS_DIR=C:/otdu2-deps",
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            // The repo-root CMakeLists: the ANDROID branch builds the engine
            // into libopentdu2.so (SDL activity entry) and skips host tools.
            path = file("../../CMakeLists.txt")
            version = "3.31.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
