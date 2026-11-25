import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { access, mkdtemp, rm } from 'node:fs/promises';
import { constants } from 'node:fs';
import { once } from 'node:events';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { test } from 'node:test';
import { numberedPdf } from './fixtures.mjs';

const executable = process.env.MDS_TEST_PACKAGE;

test('packaged ARM64 runtime serves the site, PDF previews and simulation over HTTP', {
  skip: !executable, timeout: 20_000,
}, async (t) => {
  await access(join(dirname(executable), 'install.sh'), constants.X_OK);
  const data = await mkdtemp(join(tmpdir(), 'mds-package-test-'));
  const port = '30128';
  const server = spawn(executable, [], {
    env: { ...process.env, MDS_DATA: data, MDS_BIND: '127.0.0.1', MDS_PORT: port, MDS_PRINTER: '' },
    stdio: ['ignore', 'pipe', 'inherit'],
  });
  const closed = once(server, 'close');
  t.after(async () => {
    server.kill();
    await closed;
    await rm(data, { recursive: true, force: true });
  });
  const ready = await Promise.race([
    once(server.stdout, 'data').then(([text]) => text.toString()),
    closed.then(([code]) => { throw new Error(`Packaged server exited with code ${code}`); }),
  ]);
  assert.match(ready, /127\.0\.0\.1:30128/);
  const root = `http://127.0.0.1:${port}`;
  const command = { 'X-MDS-Request': '1' };
  assert.match(await (await fetch(root)).text(), /<title>MDSPrinter<\/title>/);
  const uploaded = await fetch(`${root}/api/jobs`, {
    method: 'POST', body: numberedPdf(5), headers: { ...command, 'Content-Type': 'application/pdf' },
  });
  assert.equal(uploaded.status, 201);
  const job = await uploaded.json();
  assert.deepEqual(job.plan, { first: [1, 3, 5], second: [0, 4, 2] });
  const preview = await fetch(`${root}/api/jobs/${job.id}/preview/1`);
  assert.equal(preview.headers.get('content-type'), 'image/png');
  assert.equal(Buffer.from(await preview.arrayBuffer()).subarray(0, 8).toString('hex'), '89504e470d0a1a0a');
  for (const [pass, stage] of [[1, 'awaiting-reload'], [2, 'complete']]) {
    const result = await fetch(`${root}/api/jobs/${job.id}/print`, {
      method: 'POST', headers: { ...command, 'Content-Type': 'application/json' },
      body: JSON.stringify({ pass, mode: 'simulate', revision: job.revision }),
    });
    assert.equal(result.status, 200);
    assert.equal((await result.json()).stage, stage);
  }
});
