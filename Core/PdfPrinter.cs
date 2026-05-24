using System;
using System.Collections.Generic;
using System.Drawing.Printing;
using System.Linq;
using PdfiumViewer;

namespace ManualDoubleSidedPrinter.Core;

public static class PdfPrinter
{
    public static IReadOnlyList<string> GetInstalledPrinters()
    {
        return PrinterSettings.InstalledPrinters
            .Cast<string>()
            .OrderBy(name => name, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    public static void PrintPdfFile(string pdfPath, string printerName, string jobName)
    {
        if (string.IsNullOrWhiteSpace(printerName))
        {
            throw new InvalidOperationException("Printer is required.");
        }

        using var document = PdfDocument.Load(pdfPath);
        if (document.PageCount <= 0)
        {
            throw new InvalidOperationException("PDF does not contain printable pages.");
        }

        using var printDocument = document.CreatePrintDocument(PdfPrintMode.ShrinkToMargin);
        printDocument.DocumentName = jobName;
        printDocument.PrintController = new StandardPrintController();
        printDocument.PrinterSettings = new PrinterSettings
        {
            PrinterName = printerName,
            MinimumPage = 1,
            MaximumPage = document.PageCount,
            FromPage = 1,
            ToPage = document.PageCount,
            PrintRange = PrintRange.SomePages
        };

        if (!printDocument.PrinterSettings.IsValid)
        {
            throw new InvalidOperationException($"Printer is invalid or unavailable: {printerName}");
        }

        printDocument.Print();
    }
}
