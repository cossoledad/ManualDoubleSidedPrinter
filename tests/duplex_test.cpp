#include "duplex.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void check(bool condition, const char* context)
{
    if (!condition) {
        throw std::runtime_error(context);
    }
}

void check_plan(const mdsp::DuplexPlan& plan, const std::vector<int>& first,
                const std::vector<int>& second, const char* context)
{
    check(plan.first_pass == first && plan.second_pass == second, context);
}

template <typename Exception, typename Callback>
void check_throws(Callback callback, const char* context)
{
    try {
        callback();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(context);
}

} // namespace

int main()
try {
    check_plan(mdsp::build_for_m126a(1), {1}, {}, "single page");
    check_plan(mdsp::build_for_m126a(2), {1}, {2}, "one sheet");
    check_plan(mdsp::build_for_m126a(6), {1, 3, 5}, {6, 4, 2}, "even document");
    check_plan(mdsp::build_for_m126a(5), {1, 3, 5}, {0, 4, 2}, "odd document");
    check_plan(mdsp::build_for_m126a(std::vector{8, 3, 6}), {8, 6}, {0, 3}, "positional parity");
    check_plan(mdsp::build_for_m126a(mdsp::select_pages(L"2, 4-5, 8, 10", 10)),
               {2, 5, 10}, {0, 8, 4}, "sparse selection");
    check(mdsp::select_pages(L" 9, 4-6，5, 2 ", 10) == std::vector{2, 4, 5, 6, 9}, "sort and deduplicate");
    check(mdsp::select_pages(L" AlL ", 3) == std::vector{1, 2, 3}, "all pages");
    check(mdsp::select_pages(L" \t", 2) == std::vector{1, 2}, "blank expression");
    check_throws<std::invalid_argument>([] { mdsp::build_for_m126a(0); }, "zero-page document");
    check_throws<std::invalid_argument>([] { mdsp::build_for_m126a(std::span<const int>{}); }, "empty selection");
    check_throws<std::invalid_argument>([] { mdsp::select_pages(L"2--3", 5); }, "malformed interval");
    check_throws<std::invalid_argument>([] { mdsp::select_pages(L"3-2", 5); }, "descending interval");
    check_throws<std::out_of_range>([] { mdsp::select_pages(L"1-6", 5); }, "out-of-bounds interval");
    check_throws<std::out_of_range>([] { mdsp::select_pages(L"2147483648", 5); }, "overflowing number");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
