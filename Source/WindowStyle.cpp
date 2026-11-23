// Windows only: paint native title bars black (Windows 11) / dark (Windows 10).
// Kept in its own file so <windows.h> never meets the JUCE headers.
#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <dwmapi.h>

void styleNativeTitleBar (void* nativeHandle)
{
    auto hwnd = static_cast<HWND> (nativeHandle);
    if (hwnd == nullptr) return;
    const BOOL dark = TRUE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE: 20 on current Windows 10/11, 19 on early Windows 10 builds
    if (FAILED (DwmSetWindowAttribute (hwnd, 20, &dark, sizeof (dark))))
        DwmSetWindowAttribute (hwnd, 19, &dark, sizeof (dark));
    const COLORREF black = RGB (0, 0, 0), white = RGB (255, 255, 255), border = RGB (0, 0, 0);
    DwmSetWindowAttribute (hwnd, 35 /* DWMWA_CAPTION_COLOR */, &black, sizeof (black));
    DwmSetWindowAttribute (hwnd, 36 /* DWMWA_TEXT_COLOR */,    &white, sizeof (white));
    DwmSetWindowAttribute (hwnd, 34 /* DWMWA_BORDER_COLOR */,  &border, sizeof (border));
}
#else
void styleNativeTitleBar (void*) {}
#endif
