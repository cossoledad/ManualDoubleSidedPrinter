import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { promisify } from 'node:util';
import { test } from 'node:test';
import { countPages, preparePass, renderPage } from '../printing.mjs';
import { numberedPdf } from './fixtures.mjs';

test('qpdf and Poppler create ordered PDFs and a real blank page', { skip: !process.env.MDS_TEST_PDF }, async (t) => {
  const dir = await mkdtemp(join(tmpdir(), 'mds-pdf-test-'));
  t.after(() => rm(dir, { recursive: true, force: true }));
  const input = join(dir, 'source.pdf');
  await writeFile(input, numberedPdf(5));
  assert.equal(await countPages(input), 5);
  const preview = await renderPage(input, 3);
  assert.equal(preview.readUInt32BE(20), 1400);
  const text = async (file) => {
    assert.equal(await countPages(file), 3);
    const { stdout } = await promisify(execFile)('pdftotext', [file, '-']);
    return stdout.split('\f').slice(0, 3).map((page) => page.trim());
  };
  assert.deepEqual(await text(await preparePass(dir, [1, 3, 5], 1, 5)), ['PAGE1', 'PAGE3', 'PAGE5']);
  assert.deepEqual(await text(await preparePass(dir, [0, 4, 2], 2, 5)), ['', 'PAGE4', 'PAGE2']);
});
