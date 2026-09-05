#include "path_utils.h"

#include <windows.h>

namespace DeusExHumanRevolutionHeadTracking {

static void DummyAddress() {}

std::string GetModuleDirectory() {
    char modulePath[MAX_PATH];
    HMODULE hModule = nullptr;

    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&DummyAddress),
            &hModule) || hModule == nullptr) {
        return {};
    }

    if (GetModuleFileNameA(hModule, modulePath, MAX_PATH) == 0) {
        return {};
    }

    std::string path(modulePath);
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash == std::string::npos) {
        return {};
    }
    return path.substr(0, lastSlash + 1);
}

std::string GetModulePath(const char* filename) {
    std::string dir = GetModuleDirectory();
    if (dir.empty()) {
        return filename;
    }
    return dir + filename;
}

std::wstring GetModulePathW(const char* filename) {
    std::string narrow = GetModulePath(filename);
    int len = MultiByteToWideChar(CP_ACP, 0, narrow.c_str(), -1, nullptr, 0);
    if (len <= 1) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_ACP, 0, narrow.c_str(), -1, &wide[0], len);
    wide.pop_back();
    return wide;
}

}
