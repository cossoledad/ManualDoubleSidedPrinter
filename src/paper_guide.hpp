#pragma once

#include <windows.h>

#include <optional>

namespace mdsp {

inline constexpr double paper_guide_duration = 4.8;

const wchar_t* paper_guide_caption(std::optional<double> seconds);
void draw_paper_guide(HDC dc, const RECT& bounds, std::optional<double> seconds);

} // namespace mdsp
