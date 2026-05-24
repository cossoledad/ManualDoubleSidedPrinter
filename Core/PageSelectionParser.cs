using System;
using System.Collections.Generic;
using System.Linq;

namespace ManualDoubleSidedPrinter.Core;

public static class PageSelectionParser
{
    public static bool TryParse(string? input, int maxPage, out IReadOnlyList<int> pages, out string? error)
    {
        pages = Array.Empty<int>();
        error = null;

        if (maxPage <= 0)
        {
            error = "文档页数无效。";
            return false;
        }

        var text = (input ?? string.Empty).Trim();
        if (string.IsNullOrWhiteSpace(text) || string.Equals(text, "all", StringComparison.OrdinalIgnoreCase))
        {
            pages = Enumerable.Range(1, maxPage).ToList();
            return true;
        }

        var result = new SortedSet<int>();
        var tokens = text.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);

        foreach (var token in tokens)
        {
            if (token.Contains('-', StringComparison.Ordinal))
            {
                var bounds = token.Split('-', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
                if (bounds.Length != 2 ||
                    !int.TryParse(bounds[0], out var start) ||
                    !int.TryParse(bounds[1], out var end))
                {
                    error = $"无效区间: {token}";
                    return false;
                }

                if (start < 1 || end < 1 || start > end || end > maxPage)
                {
                    error = $"区间越界: {token}";
                    return false;
                }

                for (var i = start; i <= end; i++)
                {
                    result.Add(i);
                }

                continue;
            }

            if (!int.TryParse(token, out var page))
            {
                error = $"无效页码: {token}";
                return false;
            }

            if (page < 1 || page > maxPage)
            {
                error = $"页码越界: {token}";
                return false;
            }

            result.Add(page);
        }

        if (result.Count == 0)
        {
            error = "未解析到有效页码。";
            return false;
        }

        pages = result.ToList();
        return true;
    }
}
