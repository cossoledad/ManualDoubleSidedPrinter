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

        var oddPages = Enumerable.Range(1, totalPages)
            .Where(page => page % 2 == 1)
            .ToList();

        var evenPages = Enumerable.Range(1, totalPages)
            .Where(page => page % 2 == 0)
            .ToList();

        var usesSecondPassLeadingBlank = totalPages > 1 && totalPages % 2 == 1;

        var firstPass = oddPages;

        var secondPass = evenPages
            .OrderByDescending(page => page)
            .ToList();
        if (usesSecondPassLeadingBlank)
        {
            // Blank page marker(0) means "print blank on the first fed sheet in pass 2".
            // This keeps the last odd page on front side instead of moving it to back side.
            secondPass.Insert(0, 0);
        }

        return new DuplexPlan(firstPass, secondPass);
    }
}
