#!/bin/bash
# Installs Crate XL on the emulator, launches it like a user would (permission dialogs
# answered by tapping "Allow"), and prints the crash log.
set -x
PKG=com.illahtainment.cratexl
APK="$1"
adb uninstall $PKG >/dev/null 2>&1
adb install -r "$APK" || { echo "INSTALL FAILED"; exit 0; }
adb logcat -c
adb shell am start -W -n $PKG/com.rmsl.juce.JuceActivity
tapAllow() {
  for i in 1 2 3 4; do
    adb shell uiautomator dump /sdcard/ui.xml >/dev/null 2>&1
    adb shell cat /sdcard/ui.xml > ui.xml 2>/dev/null
    B=$(grep -oE 'text="(While using the app|Allow|ALLOW|Only this time)"[^>]*bounds="\[[0-9]+,[0-9]+\]\[[0-9]+,[0-9]+\]"' ui.xml | head -1 | grep -oE '\[[0-9]+,[0-9]+\]\[[0-9]+,[0-9]+\]')
    [ -z "$B" ] && return
    read x1 y1 x2 y2 <<< $(echo "$B" | tr -d '[]' | tr ',' ' ' | sed 's/\]\[/ /')
    nums=($(echo "$B" | grep -oE '[0-9]+'))
    adb shell input tap $(( (nums[0]+nums[2])/2 )) $(( (nums[1]+nums[3])/2 ))
    echo "tapped allow at $B"
    sleep 3
  done
}
sleep 3
PID=$(adb shell pidof $PKG)
echo "app pid $PID"
sleep 5
tapAllow
sleep 15
adb shell pidof $PKG && echo "ALIVE after launch" || echo "DIED after launch"
for xy in "100 300" "100 420" "100 520" "640 400" "100 200"; do adb shell input tap $xy; sleep 3; done
adb shell pidof $PKG && echo "ALIVE after taps" || echo "DIED after taps"
adb logcat -d -v threadtime > logcat.txt
echo "===== CRASH ====="
grep -nE "FATAL|SIGSEGV|SIGABRT|SIGILL|SIGBUS|AndroidRuntime|Abort message|backtrace|#[0-9]+ pc|Caused by|at com\.|at android\.|UnsatisfiedLink|dlopen|libjuce|native_bridge|houdini|ndk_translation" logcat.txt | head -150
echo "===== LAST APP LINES ====="
[ -n "$PID" ] && grep -E "^[0-9-]+ [0-9:.]+ +$PID " logcat.txt | grep -vE "AAudioStream: setState|PlayerBase" | tail -40
echo "===== APP LOG ====="
grep -iE "juce|crate|oboe|aaudio|opensl" logcat.txt | head -80
exit 0
