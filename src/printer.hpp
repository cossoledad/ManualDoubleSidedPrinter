#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace mdsp {

class PdfDocument;

struct Printers {
    std::vector<std::wstring> names;
    std::wstring default_name;
};

struct PrinterSettings {
    std::wstring name;
    std::vector<std::byte> devmode;
};

Printers list_printers();
PrinterSettings read_printer_settings(const std::wstring& name);
void print_pass(const PdfDocument& document, const PrinterSettings& settings,
                std::span<const int> pages, const std::wstring& job_name);

} // namespace mdsp
