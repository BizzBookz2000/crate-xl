#include "Views.h"
std::unique_ptr<ViewBase> createSequencerView() { return std::make_unique<ViewBase>(); }
