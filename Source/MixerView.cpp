#include "Views.h"
std::unique_ptr<ViewBase> createMixerView() { return std::make_unique<ViewBase>(); }
