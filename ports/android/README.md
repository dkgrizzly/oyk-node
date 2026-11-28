<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (C) 2026 KR4OYK - David Kuder -->

# OYK Packet Node — Android Port (`ports/android/`)

Jetpack Compose / Material 3 UI paired with the native C++20 `NodeEngine` via JNI (`ports/android/app/src/main/cpp/`).

## Prerequisites

- **JDK 17** (`sourceCompatibility = JavaVersion.VERSION_17`)
- **Android SDK** (`compileSdk = 36`, `minSdk = 26`, `targetSdk = 36`)
- **Android NDK** `30.0.16248370` and **CMake** `4.1.2`
- Environment variable `ANDROID_HOME` (or `ANDROID_SDK_ROOT`) pointing to your Android SDK installation
- `adb` (Android Platform Tools) on `PATH` or at `$ANDROID_HOME/platform-tools/adb`

## Product Flavors

| Flavor | Application ID | Notes |
| --- | --- | --- |
| `sideload` | `com.kr4oyk.oyknode.sideload` | `BuildConfig.IS_SIDELOAD = true`, `Sideload` label, independent package ID for direct ADB/APK installs |
| `playStore` | `com.kr4oyk.oyknode` | `BuildConfig.IS_SIDELOAD = false`, `Play Store` label |

Native ABI targets configured in `app/build.gradle.kts`: `arm64-v8a`, `armeabi-v7a`, `x86_64`.

## Building APKs, Bundles & Running Unit Tests

Run all Gradle commands from `ports/android/`:

```bash
cd ports/android
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"

# Debug APKs
./gradlew :app:assembleSideloadDebug --no-daemon
./gradlew :app:assemblePlayStoreDebug --no-daemon

# Release APKs / Play App Bundle
./gradlew :app:assembleSideloadRelease --no-daemon
./gradlew :app:bundlePlayStoreRelease --no-daemon

# Host Robolectric & Jetpack Compose Unit Tests (all 8 Settings tabs, MHEARD, modal preemption, gallery captures)
./gradlew :app:testPlayStoreDebugUnitTest --no-daemon
```

Default build outputs:
- Sideload Debug APK: `app/build/outputs/apk/sideload/debug/app-sideload-debug.apk`
- Play Store Debug APK: `app/build/outputs/apk/playStore/debug/app-playStore-debug.apk`
- Sideload Release APK: `app/build/outputs/apk/sideload/release/app-sideload-release.apk`
- Play Store Release AAB: `app/build/outputs/bundle/playStoreRelease/app-playStore-release.aab`

For release signing, inject credentials via environment variables (never commit keystores or passwords):
- `OYK_ANDROID_KEYSTORE`
- `OYK_ANDROID_KEYSTORE_PASS`
- `OYK_ANDROID_KEY_ALIAS`
- `OYK_ANDROID_KEY_PASS`

## Sideloading & Verifying via ADB

```bash
# Confirm target device is attached and authorized
adb devices -l

# Install or update the sideload debug build on a specific device serial
adb -s <SERIAL> install -r app/build/outputs/apk/sideload/debug/app-sideload-debug.apk

# Launch MainActivity and wait for first frame
adb -s <SERIAL> shell am start -W -n com.kr4oyk.oyknode.sideload/com.kr4oyk.oyknode.MainActivity

# Inspect process & logs (check for any Java/JNI errors)
adb -s <SERIAL> shell pidof com.kr4oyk.oyknode.sideload
adb -s <SERIAL> logcat -d -s AndroidRuntime:E OykNode:D

# Uninstall when finished testing
adb -s <SERIAL> uninstall com.kr4oyk.oyknode.sideload
```
