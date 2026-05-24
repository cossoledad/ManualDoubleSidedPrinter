using System;
using UglyToad.PdfPig;

namespace ManualDoubleSidedPrinter.Core;

public static class PdfPageReader
{
    public static int ReadPageCount(string pdfPath)
    {
        using var document = PdfDocument.Open(pdfPath);
        var count = document.NumberOfPages;

        if (count <= 0)
        {
            throw new InvalidOperationException("PDF does not contain printable pages.");
        }

        return count;
    }
}
