using System;
using System.Collections.Generic;
using System.Linq;

namespace ManualDoubleSidedPrinter.Core;

public sealed record DuplexPlan(
    IReadOnlyList<int> FirstPassPages,
    IReadOnlyList<int> SecondPassPages);

public static class DuplexPlanner
{
    public static DuplexPlan BuildForM126a(int totalPages)
    {
        if (totalPages <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(totalPages), "Page count must be greater than 0.");
        }

        var pages = Enumerable.Range(1, totalPages).ToList();
        return BuildForM126a(pages);
    }

    public static DuplexPlan BuildForM126a(IReadOnlyList<int> pages)
    {
        if (pages.Count <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(pages), "Page list must contain at least one item.");
        }

        var indexed = pages
            .Select((page, index) => new { page, index })
            .ToList();

        var firstPass = indexed
            .Where(item => item.index % 2 == 0)
            .Select(item => item.page)
            .ToList();

        var secondPass = indexed
            .Where(item => item.index % 2 == 1)
            .Select(item => item.page)
            .Reverse()
            .ToList();

        var usesSecondPassLeadingBlank = pages.Count > 1 && pages.Count % 2 == 1;
        if (usesSecondPassLeadingBlank)
        {
            // Blank page marker(0) means "print blank on the first fed sheet in pass 2".
            // This keeps the last odd page on front side instead of moving it to back side.
            secondPass.Insert(0, 0);
        }

        return new DuplexPlan(firstPass, secondPass);
    }
}
