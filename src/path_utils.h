#pragma once

#include <string>

namespace DeusExHumanRevolutionHeadTracking {

std::string GetModuleDirectory();
std::string GetModulePath(const char* filename);

// Wide variant for APIs that take wide paths (core logging::Open). Converts
// the ANSI module path via CP_ACP so non-ASCII install paths stay intact.
std::wstring GetModulePathW(const char* filename);

}
