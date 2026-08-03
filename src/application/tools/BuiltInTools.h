#pragma once

#include "ToolRegistry.h"

// The registry the application runs on: builtInToolCatalog() paired with the
// factory for each tool in it.
//
// Adding a tool means one entry in BuiltInToolCatalog.h and one factory in
// BuiltInTools.cpp. Nothing in the shell needs to change.
[[nodiscard]] const ToolRegistry& builtInToolRegistry();
