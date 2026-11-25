import { execFile } from 'node:child_process';
import { join } from 'node:path';
import { promisify } from 'node:util';
import { writeFile } from 'node:fs/promises';
import { blank } from './duplex.mjs';

const execute = promisify(execFile);

async function run(command, args, options = {}) {
  return execute(command, args, {
    timeout: 45_000,
    maxBuffer: 12 * 1024 * 1024,
    windowsHide: true,
    env: { ...process.env, LC_ALL: 'C' },
    ...options,
  });
}

export async function countPages(file) {
  const { stdout } = await run('qpdf', ['--warning-exit-0', '--show-npages', file]);
  const count = Number(stdout.trim());
  if (!Number.isSafeInteger(count) || count < 1) throw new Error('Invalid PDF page count');
  return count;
}

export async function renderPage(file, page) {
  const { stdout } = await run('pdftoppm', [
    '-f', String(page), '-l', String(page), '-singlefile', '-scale-to', '1400', '-png', file,
  ], { encoding: 'buffer' });
  if (stdout.subarray(0, 8).toString('hex') !== '89504e470d0a1a0a') throw new Error('Invalid preview');
  return stdout;
}

function blankPdf(width, height) {
  const objects = [
    '<< /Type /Catalog /Pages 2 0 R >>',
    '<< /Type /Pages /Count 1 /Kids [3 0 R] >>',
    `<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ${width} ${height}] /Resources << >> >>`,
  ];
  let pdf = '%PDF-1.4\n';
  const offsets = objects.map((object, index) => {
    const offset = pdf.length;
    pdf += `${index + 1} 0 obj\n${object}\nendobj\n`;
    return offset;
  });
  const xref = pdf.length;
  pdf += `xref\n0 4\n0000000000 65535 f \n`;
  pdf += offsets.map((offset) => `${String(offset).padStart(10, '0')} 00000 n \n`).join('');
  pdf += `trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF\n`;
  return pdf;
}

export async function preparePass(dir, pages, pass, unmatchedFront) {
  const source = join(dir, 'source.pdf');
  const output = join(dir, `pass-${pass}.pdf`);
  const selection = [];
  if (pages[0] === blank) {
    const page = String(unmatchedFront);
    const { stdout } = await run('pdfinfo', ['-f', page, '-l', page, source]);
    const size = /^Page(?:\s+\d+)?\s+size:\s+(\d+(?:\.\d+)?)\s+x\s+(\d+(?:\.\d+)?)\s+pts/m.exec(stdout);
    if (!size) throw new Error('Cannot determine blank page dimensions');
    const blankFile = join(dir, 'blank.pdf');
    await writeFile(blankFile, blankPdf(size[1], size[2]), { mode: 0o600 });
    selection.push(blankFile, '1');
  }
  selection.push(source, pages.filter((page) => page !== blank).join(','));
  await run('qpdf', ['--warning-exit-0', '--empty', '--pages', ...selection, '--', output]);
  return output;
}

export async function submitPass(printer, file, id, pass) {
  const { stdout } = await run('lp', [
    '-d', printer, '-n', '1', '-o', 'sides=one-sided', '-o', 'number-up=1',
    '-o', 'outputorder=normal', '-o', 'job-sheets=none', '-t', `MDSPrinter ${id.slice(0, 8)} P${pass}`, '--', file,
  ]);
  return /request id is (\S+)/.exec(stdout)?.[1] ?? null;
}
