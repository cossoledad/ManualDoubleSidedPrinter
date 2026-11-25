import { createServer } from 'node:http';
import { isIP } from 'node:net';
import { resolve } from 'node:path';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { HttpError, Jobs, jobIdPattern } from './jobs.mjs';
import * as printing from './printing.mjs';

const maxPdf = 50 * 1024 * 1024;
const assetTypes = new Map([
  ['/', ['index.html', 'text/html; charset=utf-8']],
  ['/app.mjs', ['app.mjs', 'text/javascript; charset=utf-8']],
  ['/style.css', ['style.css', 'text/css; charset=utf-8']],
]);
const securityHeaders = {
  'content-security-policy': "default-src 'self'; frame-ancestors 'none'; object-src 'none'; base-uri 'none'; form-action 'self'",
  'x-content-type-options': 'nosniff',
  'referrer-policy': 'no-referrer',
  'cache-control': 'no-store',
};

function send(response, status, content, type = 'application/json; charset=utf-8', extra = {}) {
  const body = type.startsWith('application/json') ? JSON.stringify(content) : content;
  response.writeHead(status, { ...securityHeaders, ...extra, 'content-type': type });
  response.end(body);
}

function allowHost(request, hostname) {
  try {
    const host = new URL(`http://${request.headers.host}`);
    if (host.host !== request.headers.host?.toLowerCase()) return false;
    const name = host.hostname.replace(/^\[|\]$/g, '');
    return name === 'localhost' || name === hostname || isIP(name) !== 0;
  } catch { return false; }
}

function allowMutation(request) {
  if (request.headers['x-mds-request'] !== '1') return false;
  const origin = request.headers.origin;
  if (!origin) return true;
  try { return new URL(origin).host === request.headers.host?.toLowerCase(); } catch { return false; }
}

async function parseJson(request) {
  if (request.headers['content-type']?.split(';')[0] !== 'application/json') {
    throw new HttpError(415, '请求格式无效。');
  }
  let bytes = 0;
  const chunks = [];
  for await (const chunk of request.iterator({ destroyOnReturn: false })) {
    bytes += chunk.length;
    if (bytes > 2048) throw new HttpError(413, '请求过大。');
    chunks.push(chunk);
  }
  try {
    const data = JSON.parse(Buffer.concat(chunks).toString('utf8'));
    if (data === null || typeof data !== 'object' || Array.isArray(data)) throw new Error();
    return data;
  } catch { throw new HttpError(400, '请求格式无效。'); }
}

function fileName(request) {
  try { return decodeURIComponent(request.headers['x-file-name'] ?? '').trim().slice(0, 180) || '文档.pdf'; }
  catch { return '文档.pdf'; }
}

function publicJob({ id, name, count, range, plan, revision, stage, mode, queue, pendingPass }) {
  return { id, name, count, range, plan, revision, stage, mode, queue, pendingPass };
}

export async function createApplication({ root, printer = '', hostname = '', backend = printing }) {
  const jobs = new Jobs(root, printer, backend);
  await jobs.load();
  let rendering = 0;

  async function handle(request, response) {
    try {
      if (!allowHost(request, hostname.toLowerCase())) throw new HttpError(403, '请使用局域网 IP 或已配置的主机名访问。');
      const { pathname } = new URL(request.url, 'http://localhost');
      const { method } = request;
      if (method === 'GET' && assetTypes.has(pathname)) {
        const [file, type] = assetTypes.get(pathname);
        return send(response, 200, await readFile(new URL(`./public/${file}`, import.meta.url)), type);
      }
      if (method === 'GET' && pathname === '/api/config') {
        return send(response, 200, { printer, activeId: jobs.activeId, maxUploadMiB: maxPdf / 1024 ** 2 });
      }
      if (method === 'POST' && !allowMutation(request)) throw new HttpError(403, '请求来源无效。');
      if (method === 'POST' && pathname === '/api/jobs') {
        if (request.headers['content-type']?.split(';')[0] !== 'application/pdf') throw new HttpError(415, '请选择 PDF 文件。');
        if (Number(request.headers['content-length']) > maxPdf) throw new HttpError(413, 'PDF 超过 50 MiB。');
        return send(response, 201, publicJob(await jobs.upload(request, fileName(request), maxPdf)));
      }

      const [, api, resource, id, action, page, rest] = pathname.split('/');
      if (api !== 'api' || resource !== 'jobs' || !jobIdPattern.test(id) || rest !== undefined) {
        throw new HttpError(404, '请求的页面不存在。');
      }
      if (method === 'GET' && action === undefined) return send(response, 200, publicJob(jobs.get(id)));
      if (method === 'GET' && action === 'preview' && /^\d{1,3}$/.test(page)) {
        const number = Number(page);
        if (number < 1 || number > jobs.get(id).count) throw new HttpError(404, '这一页不存在。');
        if (rendering >= 2) throw new HttpError(429, '预览正在处理，请稍后重试。');
        rendering += 1;
        try {
          const png = await backend.renderPage(jobs.file(id), number);
          return send(response, 200, png, 'image/png', { 'cache-control': 'private, max-age=600' });
        } catch (error) {
          console.error(`Preview (${id}): ${error.message}`);
          throw new HttpError(503, '无法生成预览，请重试。');
        } finally { rendering -= 1; }
      }
      if (method === 'POST' && page === undefined) {
        if (action === 'abandon') return send(response, 200, publicJob(await jobs.abandon(id)));
        if (action === 'range') {
          const { range, revision } = await parseJson(request);
          if (typeof range !== 'string' || range.length > 1024) throw new HttpError(400, '页码格式无效。');
          return send(response, 200, publicJob(await jobs.range(id, range, revision)));
        }
        if (action === 'print') {
          const { pass, mode, revision } = await parseJson(request);
          if (![1, 2].includes(pass) || !['print', 'simulate'].includes(mode)) throw new HttpError(400, '请选择打印方式。');
          return send(response, 200, publicJob(await jobs.print(id, { pass, mode, revision })));
        }
      }
      throw new HttpError(404, '请求的页面不存在。');
    } catch (error) {
      if (!(error instanceof HttpError)) console.error(error);
      if (!response.destroyed && !response.headersSent) {
        send(response, error.status ?? 500, { error: error.status ? error.message : '服务暂时不可用，请稍后重试。' },
          undefined, { connection: 'close' });
      }
    }
  }

  return { handle, sweep: () => jobs.sweep() };
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const port = Number(process.env.MDS_PORT ?? 30128);
  const host = process.env.MDS_BIND ?? '127.0.0.1';
  const printer = process.env.MDS_PRINTER?.trim() ?? '';
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('Invalid MDS_PORT');
  const app = await createApplication({
    root: resolve(process.env.MDS_DATA ?? fileURLToPath(new URL('./data', import.meta.url))),
    printer, hostname: process.env.MDS_HOSTNAME,
  });
  const server = createServer({ headersTimeout: 15_000, requestTimeout: 120_000 }, app.handle);
  server.listen(port, host, () => console.log(`MDSPrinter listening on ${host}:${port} (${printer || 'simulation only'})`));
  const sweep = setInterval(() => app.sweep().catch(console.error), 60 * 60 * 1000);
  sweep.unref();
  process.on('SIGTERM', () => server.close());
}
