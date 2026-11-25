export function numberedPdf(count) {
  const objects = [
    '<< /Type /Catalog /Pages 2 0 R >>',
    `<< /Type /Pages /Count ${count} /Kids [${Array.from({ length: count }, (_, page) => `${4 + page * 2} 0 R`).join(' ')}] >>`,
    '<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
  ];
  for (let page = 1; page <= count; page += 1) {
    const contents = `BT /F 28 Tf 80 700 Td (PAGE${page}) Tj ET\n`;
    objects.push(`<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F 3 0 R >> >> /Contents ${objects.length + 2} 0 R >>`,
      `<< /Length ${contents.length} >>\nstream\n${contents}endstream`);
  }
  let pdf = '%PDF-1.4\n';
  const offsets = objects.map((object, i) => {
    const offset = pdf.length;
    pdf += `${i + 1} 0 obj\n${object}\nendobj\n`;
    return offset;
  });
  const xref = pdf.length;
  return Buffer.from(`${pdf}xref\n0 ${objects.length + 1}\n0000000000 65535 f \n${
    offsets.map((offset) => `${String(offset).padStart(10, '0')} 00000 n \n`).join('')
  }trailer\n<< /Size ${objects.length + 1} /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF\n`);
}
