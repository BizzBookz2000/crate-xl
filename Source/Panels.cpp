#include "Views.h"
std::unique_ptr<ViewBase> createBrowserPanel() { return std::make_unique<ViewBase>(); }
std::unique_ptr<ViewBase> createFxPanel() { return std::make_unique<ViewBase>(); }
std::unique_ptr<ViewBase> createMacrosPanel() { return std::make_unique<ViewBase>(); }
std::unique_ptr<ViewBase> createKeyboardPanel() { return std::make_unique<ViewBase>(); }
