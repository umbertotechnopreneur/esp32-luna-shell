#pragma once

// Portable host checks exercise Debug scenes without importing a firmware build.
// A caller may override this definition to check the Release scene catalog.
#ifndef CONFIG_LUNASHELL_BUILD_MODE_DEBUG
#define CONFIG_LUNASHELL_BUILD_MODE_DEBUG 1
#endif
