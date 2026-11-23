#!/bin/bash
# ============================================================================
#  Crate XL - Android APK
#  Needs: Android Studio (it brings the Android SDK and Java), and Git.
#  The first build downloads JUCE, the Android NDK and Gradle: 15-30 minutes.
# ============================================================================
set -e
cd "$(dirname "$0")"

# JUCE sources, next to this script (the Android project expects them in ./JUCE)
if [ ! -f JUCE/modules/juce_core/juce_core.h ]; then
    echo "Downloading JUCE 9.0.4..."
    rm -rf JUCE
    git clone --depth 1 --branch 9.0.4 https://github.com/juce-framework/JUCE.git JUCE
fi

# Android SDK
if [ -z "$ANDROID_HOME" ]; then
    for d in "$HOME/Library/Android/sdk" "$HOME/Android/Sdk"; do [ -d "$d" ] && export ANDROID_HOME="$d"; done
fi
if [ -z "$ANDROID_HOME" ]; then
    echo "Can't find the Android SDK. Install Android Studio, open it once, then run this again"
    echo "(or set ANDROID_HOME to your SDK folder)."
    exit 1
fi

# Java: use the one inside Android Studio if there isn't one already
if [ -z "$JAVA_HOME" ]; then
    for d in "/Applications/Android Studio.app/Contents/jbr/Contents/Home" "/opt/android-studio/jbr" "$HOME/android-studio/jbr"; do
        [ -d "$d" ] && export JAVA_HOME="$d"
    done
fi

cd Builds/Android
echo "sdk.dir=$ANDROID_HOME" > local.properties
chmod +x gradlew
./gradlew assembleDebug

mkdir -p ../../dist
cp app/build/outputs/apk/debug/*.apk "../../dist/Crate XL.apk"
echo
echo "Done: dist/Crate XL.apk"
echo "Copy it to your phone or tablet and open it to install (allow installs from this source when asked),"
echo "or with the device plugged in:  \$ANDROID_HOME/platform-tools/adb install -r \"dist/Crate XL.apk\""
