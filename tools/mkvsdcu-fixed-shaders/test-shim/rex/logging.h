// Shader-container checks use the production parser without a running SDK app.
#pragma once
#define FMT_HEADER_ONLY
#include <fmt/format.h>
#define REXLOG_INFO(...) ((void)0)
#define REXLOG_WARN(...) ((void)0)
#define REXLOG_ERROR(...) ((void)0)
