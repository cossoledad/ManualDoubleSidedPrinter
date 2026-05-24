using System;
using System.Collections.Generic;
using System.IO;
using PdfSharpCore.Pdf;
using PdfSharpCore.Pdf.IO;

namespace ManualDoubleSidedPrinter.Core;

public static class PdfSubsetComposer
{
    public static string CreateSubsetPdf(string sourcePdfPath, IReadOnlyList<int> pages)
    {
        if (!File.Exists(sourcePdfPath))
        {
            throw new FileNotFoundException("Source PDF not found.", sourcePdfPath);
        }

        if (pages.Count == 0)
        {
            throw new InvalidOperationException("No pages provided for subset composition.");
        }

        using var source = PdfReader.Open(sourcePdfPath, PdfDocumentOpenMode.Import);
        using var subset = new PdfDocument();

        foreach (var pageNumber in pages)
        {
            if (pageNumber == 0)
            {
                var template = source.Pages[0];
                var blank = subset.AddPage();
                blank.Width = template.Width;
                blank.Height = template.Height;
                blank.Orientation = template.Orientation;
                continue;
            }

            if (pageNumber < 1 || pageNumber > source.PageCount)
            {
                throw new InvalidOperationException($"Page {pageNumber} is out of range 1..{source.PageCount}.");
            }

            subset.AddPage(source.Pages[pageNumber - 1]);
        }

        var tempFile = Path.Combine(Path.GetTempPath(), $"m126a_subset_{Guid.NewGuid():N}.pdf");
        subset.Save(tempFile);
        return tempFile;
    }
}
