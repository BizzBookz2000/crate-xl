@echo off
REM ============================================================================
REM  Crate XL - Android APK (Windows)
REM  Needs: Android Studio (it brings the Android SDK and Java), and Git.
REM  The first build downloads JUCE, the Android NDK and Gradle: 15-30 minutes.
REM ============================================================================
setlocal
cd /d "%~dp0"

if not exist "JUCE\modules\juce_core\juce_core.h" (
    echo Downloading JUCE 9.0.4...
    if exist JUCE rmdir /s /q JUCE
    git clone --depth 1 --branch 9.0.4 https://github.com/juce-framework/JUCE.git JUCE || goto :fail
)

if "%ANDROID_HOME%"=="" if exist "%LOCALAPPDATA%\Android\Sdk" set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
if "%ANDROID_HOME%"=="" (
    echo Can't find the Android SDK. Install Android Studio, open it once, then run this again
    echo ^(or set ANDROID_HOME to your SDK folder^).
    goto :fail
)
if "%JAVA_HOME%"=="" if exist "%ProgramFiles%\Android\Android Studio\jbr" set "JAVA_HOME=%ProgramFiles%\Android\Android Studio\jbr"

if not exist "%USERPROFILE%\.android\debug.keystore" (
    if not exist "%USERPROFILE%\.android" mkdir "%USERPROFILE%\.android"
    "%JAVA_HOME%\bin\keytool.exe" -genkeypair -keystore "%USERPROFILE%\.android\debug.keystore" -storetype JKS -storepass android -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
)

cd Builds\Android
set "SDKDIR=%ANDROID_HOME:\=\\%"
> local.properties echo sdk.dir=%SDKDIR%
call gradlew.bat assembleDebug || goto :fail

if not exist "..\..\dist" mkdir "..\..\dist"
for %%f in (app\build\outputs\apk\debug\*.apk) do copy /y "%%f" "..\..\dist\Crate XL.apk" >nul
echo.
echo Done: dist\Crate XL.apk
echo Copy it to your phone or tablet and open it to install, or with the device plugged in:
echo   "%ANDROID_HOME%\platform-tools\adb.exe" install -r "dist\Crate XL.apk"
pause
exit /b 0

:fail
echo.
echo The Android build stopped with an error (see above).
pause
exit /b 1
