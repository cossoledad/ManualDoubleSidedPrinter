#pragma once

#include <span>
#include <string_view>
#include <vector>

namespace mdsp {

inline constexpr int blank_page = 0;

struct DuplexPlan {
    std::vector<int> first_pass;
    std::vector<int> second_pass;
};

DuplexPlan build_for_m126a(int total_pages);
DuplexPlan build_for_m126a(std::span<const int> pages);

// An empty expression or "all" selects the entire document.
std::vector<int> select_pages(std::wstring_view expression, int total_pages);

} // namespace mdsp
