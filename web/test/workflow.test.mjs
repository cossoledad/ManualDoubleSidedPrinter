import assert from 'node:assert/strict';
import { once } from 'node:events';
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
  assert.deepEqual(planPages('2, 4-5，8,10,5', 10), { first: [2, 5, 10], second: [0, 8, 4] });
  for (const input of ['2--3', '3-2', '1-6', '2147483648', ',，']) assert.throws(() => planPages(input, 5), RangeError);
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
  const upload = () => request(app, '/api/jobs', pdf, { 'x-file-name': encodeURIComponent('测试.pdf') });
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
  assert.equal(initial.body.name, '测试.pdf');
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
  const restart = (url, revision) => request(app, `${url}/restart`, { revision });
  assert.equal((await restart(second, 2)).status, 409);
  assert.equal((await pass(2, 'simulate', 2, second)).body.stage, 'complete');
  assert.equal(submitted.length, 2);
  assert.equal((await restart(second, 1)).status, 409);
  const restarted = await restart(second, 2);
  assert.equal(restarted.status, 200);
  assert.deepEqual(restarted.body, { ...simulated.body, range: '5,1,2',
    plan: { first: [1, 5], second: [0, 2] }, revision: 3 });
  assert.equal((await restart(second, 2)).status, 409);
  assert.equal((await pass(1, 'simulate', 2, second)).status, 409);
  assert.equal((await pass(2, 'simulate', 2, second)).status, 409);
  assert.equal((await pass(1, 'print', 3, second, 'other')).body.stage, 'awaiting-reload');
  assert.equal((await pass(2, 'print', 3, second, 'other')).body.stage, 'complete');
  assert.equal((await restart(second, 3)).status, 409);
  assert.deepEqual(submitted.slice(2), [
    { printer: 'other', pass: 1, pages: [1, 5] },
    { printer: 'other', pass: 2, pages: [0, 2] },
  ]);
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

test('Web controls allow printing the same PDF after simulation and only restore unfinished print work', { timeout: 10_000 }, async (t) => {
  const root = await mkdtemp(join(tmpdir(), 'mds-web-controls-'));
  t.after(() => rm(root, { recursive: true, force: true }));
  const submitted = [];
  const app = await createApplication({ root, backend: {
    async listPrinters() { return { printers: ['default', 'other'].map((name) => ({ name, accepting: true })), defaultPrinter: 'default' }; },
    async countPages() { return 2; },
    async preparePass(_folder, _pages, pass) { return pass; },
    async submitPass(printer, pass) { submitted.push([printer, pass]); return `${printer}-${pass}`; },
  } });
  const storage = new Map();
  t.mock.method(globalThis, 'fetch', async (url, { body, headers }) => {
    const { status, body: result } = await request(app, url, body === undefined ? undefined : JSON.parse(body),
      Object.fromEntries(Object.entries(headers).map(([name, value]) => [name.toLowerCase(), value])));
    return { status, ok: status < 400, json: async () => result };
  });

  class Element extends EventTarget {
    hidden = true;
    value = '';
    classList = { toggle() {} };
    #text = '';
    get textContent() { return this.#text; }
    set textContent(value) { this.#text = value; this.dispatchEvent(new Event('render')); }
    replaceChildren() {}
    removeAttribute() {}
    toggleAttribute() {}
  }
  const globals = {
    localStorage: {
      getItem: (key) => storage.get(key) ?? null,
      setItem: (key, value) => storage.set(key, value),
      removeItem: (key) => storage.delete(key),
    },
    ResizeObserver: class { observe() {} },
    Option: class {},
  };
  for (const key of [...Object.keys(globals), 'document']) {
    const previous = Object.getOwnPropertyDescriptor(globalThis, key);
    t.after(() => {
      if (previous) Object.defineProperty(globalThis, key, previous);
      else delete globalThis[key];
    });
  }
  Object.assign(globalThis, globals);
  let bootCount = 0;
  let elements;
  const control = (id) => {
    if (!elements.has(id)) elements.set(id, new Element());
    return elements.get(id);
  };
  async function waitForStatus(pattern) {
    const status = control('status');
    while (!pattern.test(status.textContent)) await once(status, 'render');
  }
  async function boot(pattern) {
    elements = new Map();
    globalThis.document = Object.assign(new EventTarget(), { getElementById: control, querySelectorAll: () => [] });
    await import(`../public/app.mjs?boot=${++bootCount}`);
    await waitForStatus(pattern);
  }
  function select(printer) {
    assert.equal(control('mode').disabled, false);
    control('mode').value = printer;
    control('mode').dispatchEvent(new Event('change'));
  }
  async function print(pattern) {
    assert.equal(control('print').disabled, false);
    assert.equal(control('print').hidden, false);
    control('print').dispatchEvent(new Event('click'));
    await waitForStatus(pattern);
  }

  const { body: draft } = await request(app, '/api/jobs', numberedPdf(2), { 'x-file-name': 'sample.pdf' });
  storage.set('mdsprinter-job', draft.id);
  storage.set('mdsprinter-printer', '');
  await boot(/检查页序/);
  assert.equal(control('mode').value, 'default');
  select('other');
  select('');
  await print(/模拟第一遍完成/);
  assert.equal(control('mode').disabled, true);
  assert.equal(storage.has('mdsprinter-job'), false);
  await print(/模拟完成/);
  assert.equal(control('file-name').textContent, 'sample.pdf · 2 页');
  assert.equal(storage.has('mdsprinter-job'), false);
  assert.equal(storage.get('mdsprinter-printer'), 'other');
  assert.deepEqual(submitted, []);

  select('other');
  assert.equal(control('print').textContent, '打印第一遍');
  assert.deepEqual(submitted, []);
  await print(/第一遍已提交/);
  await boot(/第一遍已提交/);
  assert.equal(control('mode').disabled, true);
  assert.equal(control('mode').value, 'other');
  await print(/打印任务已提交/);
  assert.deepEqual(submitted, [['other', 1], ['other', 2]]);

  const { body: old } = await request(app, '/api/jobs', numberedPdf(2), { 'x-file-name': 'old.pdf' });
  for (const pass of [1, 2]) {
    const result = await request(app, `/api/jobs/${old.id}/print`, { pass, mode: 'simulate', revision: old.revision });
    assert.equal(result.status, 200);
  }
  storage.set('mdsprinter-job', old.id);
  storage.set('mdsprinter-printer', '');
  await boot(/选择 PDF 后即可预览/);
  assert.equal(control('mode').disabled, false);
  assert.equal(control('mode').value, 'default');
  assert.equal(control('plan').hidden, true);
  assert.equal(storage.has('mdsprinter-job'), false);
});
