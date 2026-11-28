#!/bin/bash
# Installs Crate XL on the emulator, launches it, pokes the UI and saves logcat.
set -x
APK=$(find Builds/Android/app/build/outputs/apk -name "*.apk" | head -n 1)
adb install -r -g "$APK"
adb logcat -c
adb shell am start -W -n com.illahtainment.cratexl/com.rmsl.juce.JuceActivity
sleep 25
adb shell pidof com.illahtainment.cratexl && echo "ALIVE after launch" || echo "DIED after launch"
adb exec-out screencap -p > screen1.png
for xy in "100 300" "100 420" "100 520" "640 400" "100 200"; do adb shell input tap $xy; sleep 4; done
adb shell pidof com.illahtainment.cratexl && echo "ALIVE after taps" || echo "DIED after taps"
adb exec-out screencap -p > screen2.png
adb logcat -d -v threadtime > logcat.txt
grep -E "FATAL|SIGSEGV|SIGABRT|AndroidRuntime" logcat.txt | head -40
exit 0
