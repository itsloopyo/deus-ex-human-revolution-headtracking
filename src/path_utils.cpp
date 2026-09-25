#include "path_utils.h"

#include <windows.h>

#include <stdexcept>
#include <vector>

namespace DeusExHumanRevolutionHeadTracking {

static void DummyAddress() {}

std::wstring GetModulePathW(const char* filename) {
    HMODULE hModule = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&DummyAddress),
            &hModule)) {
        throw std::runtime_error("GetModuleHandleExW failed with error " + std::to_string(GetLastError()));
    }

    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(hModule, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0) {
            throw std::runtime_error("GetModuleFileNameW failed with error " + std::to_string(GetLastError()));
        }
        if (n < buffer.size()) {
            buffer.resize(n);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }

    std::wstring path(buffer.begin(), buffer.end());
    const size_t lastSlash = path.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) {
        throw std::runtime_error("the module path has no folder");
    }
    std::wstring name;
    for (const char* p = filename; *p != '\0'; ++p) name.push_back(static_cast<wchar_t>(*p));
    return path.substr(0, lastSlash + 1) + name;
}

}
