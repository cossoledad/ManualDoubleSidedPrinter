import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Readable } from 'node:stream';
import { test } from 'node:test';
import { planPages } from '../duplex.mjs';
import { createApplication } from '../server.mjs';
import { parsePrinters } from '../printing.mjs';
import { numberedPdf } from './fixtures.mjs';

function request(app, url, data, headers = {}) {
  const pdf = Buffer.isBuffer(data);
  const req = Readable.from(data === undefined ? [] : [pdf ? data : Buffer.from(JSON.stringify(data))]);
  Object.assign(req, { url, method: data === undefined ? 'GET' : 'POST', headers: {
    host: '127.0.0.1:30128', 'x-mds-request': '1', 'content-type': pdf ? 'application/pdf' : 'application/json', ...headers,
  } });
  const response = {
    writeHead(status, responseHeaders) { this.status = status; this.headers = responseHeaders; this.headersSent = true; },
    end(body) { this.body = this.headers['content-type'].startsWith('application/json') ? JSON.parse(body) : body; },
  };
  return app.handle(req, response).then(() => response);
}

test('C++ desktop page order cases and selection syntax', () => {
  assert.deepEqual(planPages('', 1), { first: [1], second: [] });
  assert.deepEqual(planPages('all', 6), { first: [1, 3, 5], second: [6, 4, 2] });
  assert.deepEqual(planPages('', 5), { first: [1, 3, 5], second: [0, 4, 2] });
  assert.deepEqual(planPages('2, 4-5��8,10,5', 10), { first: [2, 5, 10], second: [0, 8, 4] });
  for (const input of ['2--3', '3-2', '1-6', '2147483648', ',��']) assert.throws(() => planPages(input, 5), RangeError);
});

test('CUPS default printer and queues are discovered from status output', () => {
  assert.deepEqual(parsePrinters('M126a accepting requests since Tue Nov 25\nOther not accepting requests since Tue Nov 25\n\tPaused\n',
    'system default destination: M126a\n'), {
    printers: [{ name: 'M126a', accepting: true }, { name: 'Other', accepting: false }], defaultPrinter: 'M126a',
  });
  assert.deepEqual(parsePrinters('', 'no system default destination'), { printers: [], defaultPrinter: '' });
});

test('upload, preview, simulated and real passes, duplicate requests, recovery and isolation', async (t) => {
  const root = await mkdtemp(join(tmpdir(), 'mds-web-test-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  const pdf = numberedPdf(5);
  const submitted = [];
  let breakSubmit = false;
  let accepting = true;
  let releaseFirst;
  let notify;
  const firstStarted = new Promise((resolve) => { notify = resolve; });
  const hold = new Promise((resolve) => { releaseFirst = resolve; });
  const backend = {
    async listPrinters() { return { printers: [{ name: 'queue', accepting }, { name: 'other', accepting: true }], defaultPrinter: 'other' }; },
    async countPages(file) { assert.deepEqual(await readFile(file), pdf); return 5; },
    async renderPage(_file, page) { return Buffer.from(`preview ${page}`); },
    async preparePass(_dir, pages, pass) { return { pages, pass }; },
    async submitPass(printer, file) {
      const number = submitted.push({ printer, ...file });
      if (number === 1) { notify(); await hold; }
      if (breakSubmit) throw new Error('simulated loss of spooler response');
      return `queue-${number}`;
    },
  };
  const options = { root, printer: 'queue', backend };
  let app = await createApplication(options);
  const upload = () => request(app, '/api/jobs', pdf, { 'x-file-name': encodeURIComponent('����.pdf') });
  assert.equal((await request(app, '/')).status, 200);
  assert.equal((await request(app, '/viewport.mjs')).status, 200);
  assert.equal((await request(app, '/api/config')).body.defaultPrinter, 'queue');
  assert.equal((await request(app, '/api/config', undefined, { host: 'unapproved.example:30128' })).status, 403);
  assert.equal((await request(app, '/api/jobs', pdf, { origin: 'http://other.example' })).status, 403);
  assert.equal((await request(app, '/api/jobs', Buffer.from('invalid'))).status, 422);
  const initial = await upload();
  assert.equal(initial.status, 201);
  const { id, revision } = initial.body;
  const base = `/api/jobs/${id}`;
  const pass = (n, mode = 'print', version = revision, url = base, printer = 'queue') =>
    request(app, `${url}/print`, { pass: n, mode, revision: version, printer: mode === 'print' ? printer : undefined });
  assert.equal(initial.body.name, '����.pdf');
  assert.equal((await request(app, `${base}/preview/2`)).body.toString(), 'preview 2');
  assert.equal((await request(app, `${base}/preview/6`)).status, 404);
  assert.equal((await pass(2)).status, 409);

  const first = pass(1);
  await firstStarted;
  assert.equal((await pass(1)).status, 409);
  const simulated = await upload();
  const second = `/api/jobs/${simulated.body.id}`;
  assert.equal((await pass(1, 'print', 1, second)).status, 409);
  const independent = `/api/jobs/${(await upload()).body.id}`;
  assert.equal((await pass(1, 'print', 1, independent, 'unknown')).status, 409);
  assert.equal((await pass(1, 'print', 1, independent, 'other')).body.stage, 'awaiting-reload');
  assert.equal((await request(app, '/api/config')).body.activeJobs.length, 2);
  assert.equal((await request(app, `${independent}/abandon`, {})).body.stage, 'abandoned');
  assert.equal((await request(app, `${second}/range`, { range: '5,1,2', revision: 1 })).body.revision, 2);
  assert.equal((await pass(1, 'simulate', 1, second)).status, 409);
  assert.equal((await pass(1, 'simulate', 2, second)).body.stage, 'awaiting-reload');
  assert.equal((await pass(2, 'simulate', 2, second)).body.stage, 'complete');
  assert.equal(submitted.length, 2);
  releaseFirst();
  assert.equal((await first).body.queue[1], 'queue-1');
  app = await createApplication(options);
  assert.deepEqual((await request(app, '/api/config')).body.activeJobs, [{ printer: 'queue', id }]);
  assert.equal((await pass(1)).body.stage, 'awaiting-reload');
  assert.equal((await pass(2, 'print', 1, base, 'other')).status, 409);
  accepting = false;
  assert.equal((await pass(2)).status, 409);
  assert.deepEqual((await request(app, '/api/config')).body.activeJobs, [{ printer: 'queue', id }]);
  accepting = true;
  assert.equal((await pass(2)).body.stage, 'complete');
  assert.equal((await pass(2)).body.stage, 'complete');
  assert.deepEqual((await request(app, '/api/config')).body.activeJobs, []);
  assert.deepEqual(submitted.filter(({ printer }) => printer === 'queue').map(({ pages }) => pages), [[1, 3, 5], [0, 4, 2]]);

  const third = `/api/jobs/${(await upload()).body.id}`;
  breakSubmit = true;
  assert.equal((await pass(1, 'print', 1, third)).body.stage, 'uncertain');
  app = await createApplication(options);
  assert.equal((await pass(1, 'print', 1, third)).status, 409);
  assert.equal((await request(app, `${third}/abandon`, {})).body.stage, 'abandoned');
  assert.deepEqual((await request(app, '/api/config')).body.activeJobs, []);
});
