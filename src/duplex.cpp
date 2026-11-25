#include "duplex.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace mdsp {
namespace {

constexpr std::wstring_view whitespace = L" \t\r\n\v\f\u00a0\u3000";

std::wstring_view trim(std::wstring_view text)
{
    const auto first = text.find_first_not_of(whitespace);
    return first == text.npos ? std::wstring_view{} :
        text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

int page_number(std::wstring_view text, int total_pages)
{
    text = trim(text);
    if (text.starts_with(L'+')) {
        text.remove_prefix(1);
    }
    if (text.empty()) {
        throw std::invalid_argument("Missing page number.");
    }

    int number = 0;
    for (wchar_t character : text) {
        if (character < L'0' || character > L'9') {
            throw std::invalid_argument("Invalid page number.");
        }
        const int digit = character - L'0';
        if (number > (std::numeric_limits<int>::max() - digit) / 10) {
            throw std::out_of_range("Page number is outside the document.");
        }
        number = number * 10 + digit;
    }
    if (number < 1 || number > total_pages) {
        throw std::out_of_range("Page number is outside the document.");
    }
    return number;
}

std::vector<int> all_pages(int total_pages)
{
    if (total_pages <= 0) {
        throw std::invalid_argument("Page count must be greater than zero.");
    }
    std::vector<int> pages(total_pages);
    int page = 0;
    std::generate(pages.begin(), pages.end(), [&page] { return ++page; });
    return pages;
}

} // namespace

DuplexPlan build_for_m126a(int total_pages)
{
    return build_for_m126a(all_pages(total_pages));
}

DuplexPlan build_for_m126a(std::span<const int> pages)
{
    if (pages.empty()) {
        throw std::invalid_argument("Page list must contain at least one item.");
    }

    DuplexPlan plan;
    for (std::size_t index = 0; index < pages.size(); ++index) {
        auto& pass = index % 2 == 0 ? plan.first_pass : plan.second_pass;
        pass.push_back(pages[index]);
    }
    std::reverse(plan.second_pass.begin(), plan.second_pass.end());
    if (pages.size() > 1 && pages.size() % 2 == 1) {
        // The last first-pass sheet feeds first when the stack is reinserted.
        plan.second_pass.insert(plan.second_pass.begin(), blank_page);
    }
    return plan;
}

std::vector<int> select_pages(std::wstring_view expression, int total_pages)
{
    if (total_pages <= 0) {
        throw std::invalid_argument("Page count must be greater than zero.");
    }
    expression = trim(expression);
    const auto lowercase = [](wchar_t c) { return c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c; };
    if (expression.empty() || std::ranges::equal(expression, std::wstring_view(L"all"), {}, lowercase)) {
        return all_pages(total_pages);
    }

    std::vector<bool> selected(total_pages);
    while (!expression.empty()) {
        const auto separator = expression.find_first_of(L",，");
        const auto token = trim(expression.substr(0, separator));
        if (!token.empty()) {
            const auto dash = token.find(L'-');
            const int first = page_number(token.substr(0, dash), total_pages);
            const int last = dash == token.npos ? first : page_number(token.substr(dash + 1), total_pages);
            if (first > last) {
                throw std::invalid_argument("Page range must be ascending.");
            }
            std::fill(selected.begin() + (first - 1), selected.begin() + last, true);
        }
        if (separator == expression.npos) {
            break;
        }
        expression.remove_prefix(separator + 1);
    }

    std::vector<int> pages;
    for (int index = 0; index < total_pages; ++index) {
        if (selected[index]) {
            pages.push_back(index + 1);
        }
    }
    if (pages.empty()) {
        throw std::invalid_argument("No pages selected.");
    }
    return pages;
}

} // namespace mdsp
