#pragma once

#include <windows.h>

#include <string>

namespace mdsp {

struct UserError {
    std::wstring message;
};

[[noreturn]] inline void windows_error(const wchar_t* message, DWORD code = GetLastError())
{
    throw UserError{std::wstring(message) + L"（错误 " + std::to_wstring(code) + L"）"};
}

} // namespace mdsp
