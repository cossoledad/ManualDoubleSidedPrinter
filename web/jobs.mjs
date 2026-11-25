import { randomUUID } from 'node:crypto';
import { open, mkdir, readdir, readFile, rename, rm, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { planPages } from './duplex.mjs';

export const jobIdPattern = /^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/;
const retainedStages = new Set(['submitting-first', 'awaiting-reload', 'submitting-second', 'uncertain']);
const maxAge = 24 * 60 * 60 * 1000;

export class HttpError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

function holdsPrinter(job) {
  return job.mode === 'print' && retainedStages.has(job.stage);
}

async function syncDirectory(path) {
  if (process.platform === 'win32') return;
  const handle = await open(path, 'r');
  try { await handle.sync(); } finally { await handle.close(); }
}

export class Jobs {
  #jobs = new Map();
  #busy = new Set();
  #uploads = 0;
  #active = new Map();

  constructor(root, backend) {
    this.root = root;
    this.backend = backend;
  }

  get activeJobs() { return Array.from(this.#active, ([printer, id]) => ({ printer, id })); }
  folder(id) { return join(this.root, id); }
  file(id) { return join(this.folder(id), 'source.pdf'); }

  async load() {
    await mkdir(this.root, { recursive: true, mode: 0o700 });
    for (const entry of await readdir(this.root, { withFileTypes: true })) {
      if (!entry.isDirectory() || !jobIdPattern.test(entry.name)) continue;
      let job;
      try {
        job = JSON.parse(await readFile(join(this.folder(entry.name), 'job.json'), 'utf8'));
      } catch (error) {
        if (error.code !== 'ENOENT') throw error;
        await rm(this.folder(entry.name), { recursive: true, force: true });
        continue;
      }
      if (job.id !== entry.name) throw new Error(`Invalid print job directory: ${entry.name}`);
      if (job.stage.startsWith('submitting-')) {
        job = await this.#save({ ...job, stage: job.mode === 'print' ? 'uncertain' :
          job.stage === 'submitting-first' ? 'ready' : 'awaiting-reload' });
      }
      if (holdsPrinter(job)) {
        if (!job.printer || this.#active.has(job.printer)) throw new Error(`Conflicting print jobs for queue ${job.printer}`);
        this.#active.set(job.printer, job.id);
      }
      this.#jobs.set(job.id, job);
    }
    await this.sweep();
  }

  async #save(job) {
    const current = { ...job, updated: Date.now() };
    const folder = this.folder(job.id);
    const temporary = join(folder, 'job.tmp');
    await writeFile(temporary, JSON.stringify(current), { mode: 0o600, flush: true });
    await rename(temporary, join(folder, 'job.json'));
    await syncDirectory(folder);
    this.#jobs.set(job.id, current);
    return current;
  }

  get(id) {
    const job = this.#jobs.get(id);
    if (!job) throw new HttpError(404, '这份文档已过期，请重新选择 PDF。');
    return job;
  }

  async upload(request, name, limit) {
    if (this.#uploads >= 2 || this.#jobs.size + this.#uploads >= 32) {
      throw new HttpError(429, '上传空间或并发数已达上限，请稍后重试。');
    }
    this.#uploads += 1;
    const id = randomUUID();
    const folder = this.folder(id);
    try {
      await mkdir(folder, { mode: 0o700 });
      await syncDirectory(this.root);
      const file = await open(this.file(id), 'wx+', 0o600);
      try {
        let total = 0;
        for await (const chunk of request.iterator({ destroyOnReturn: false })) {
          total += chunk.length;
          if (total > limit) throw new HttpError(413, 'PDF 超过 50 MiB。');
          await file.writeFile(chunk);
        }
        const header = Buffer.alloc(1024);
        const { bytesRead } = await file.read(header, 0, header.length, 0);
        if (!header.subarray(0, bytesRead).includes('%PDF-')) throw new HttpError(422, '请选择有效的 PDF。');
        await file.sync();
      } finally {
        await file.close();
      }
      const count = await this.backend.countPages(this.file(id)).catch((error) => {
        if (error.code === 'ENOENT') throw new HttpError(503, '服务端缺少 qpdf。');
        throw new HttpError(422, '无法读取 PDF，请检查文件是否损坏或需要密码。');
      });
      if (count > 500) throw new HttpError(422, 'PDF 最多支持 500 页。');
      return await this.#save({ id, name, count, range: '', plan: planPages('', count), revision: 1,
        stage: 'ready', mode: null, printer: null, queue: {}, pendingPass: null });
    } catch (error) {
      await rm(folder, { recursive: true, force: true });
      throw error;
    } finally {
      this.#uploads -= 1;
    }
  }

  #enter(job) {
    if (this.#busy.has(job.id)) throw new HttpError(409, '该任务正在处理，请稍候。');
    this.#busy.add(job.id);
  }

  async range(id, expression, revision) {
    const job = this.get(id);
    if (job.stage !== 'ready' || job.revision !== revision) throw new HttpError(409, '计划已变化，请刷新后重试。');
    this.#enter(job);
    try {
      let plan;
      try { plan = planPages(expression, job.count); } catch (error) { throw new HttpError(422, error.message); }
      return await this.#save({ ...job, range: expression.trim(), plan, revision: revision + 1 });
    } finally {
      this.#busy.delete(id);
    }
  }

  async restartSimulation(id, revision) {
    const job = this.get(id);
    if (job.revision !== revision || job.mode !== 'simulate' || !['complete', 'abandoned'].includes(job.stage)) {
      throw new HttpError(409, '当前任务无法重新开始，请刷新后查看状态。');
    }
    this.#enter(job);
    try {
      return await this.#save({ ...job, revision: revision + 1, stage: 'ready', mode: null,
        printer: null, queue: {}, pendingPass: null });
    } finally {
      this.#busy.delete(id);
    }
  }

  async print(id, { pass, mode, revision, printer: selected }) {
    const job = this.get(id);
    if (job.revision !== revision) throw new HttpError(409, '页码已变化，请重新核对计划。');
    const first = pass === 1;
    const actual = mode === 'print';
    if (actual && job.printer && selected && job.printer !== selected) {
      throw new HttpError(409, '两遍打印必须使用同一台打印机。');
    }
    if (first && ['awaiting-reload', 'complete'].includes(job.stage) && job.mode === mode) return job;
    if (!first && job.stage === 'complete' && job.mode === mode && job.plan.second.length) return job;
    if (first ? job.stage !== 'ready' : job.stage !== 'awaiting-reload' || job.mode !== mode) {
      throw new HttpError(409, '当前阶段无法提交这一遍，请刷新任务状态。');
    }
    const printer = actual ? (first ? selected : job.printer) : null;
    if (actual && !printer) throw new HttpError(409, '请选择打印机。');
    if (actual && this.#active.has(printer) && this.#active.get(printer) !== id) {
      throw new HttpError(409, '这台打印机有一项未结束的双面任务。');
    }

    this.#enter(job);
    if (actual) this.#active.set(printer, id);
    let submitting;
    try {
      if (actual) {
        const { printers } = await this.backend.listPrinters().catch(() => {
          throw new HttpError(503, '无法读取 CUPS 打印机，请稍后刷新。');
        });
        if (!printers.some((entry) => entry.name === printer && entry.accepting)) {
          throw new HttpError(409, '所选打印机目前无法接收作业，请刷新打印机列表。');
        }
      }
      const pages = first ? job.plan.first : job.plan.second;
      const file = actual ? await this.backend.preparePass(this.folder(id), pages, pass, job.plan.first.at(-1)) : null;
      submitting = await this.#save({ ...job, mode, printer, pendingPass: pass,
        stage: first ? 'submitting-first' : 'submitting-second' });
      const queueId = actual ? await this.backend.submitPass(printer, file, id, pass) : null;
      const stage = first && job.plan.second.length ? 'awaiting-reload' : 'complete';
      const result = await this.#save({ ...submitting, stage, pendingPass: null, queue: { ...job.queue, [pass]: queueId } });
      if (actual && stage === 'complete') this.#active.delete(printer);
      return result;
    } catch (error) {
      if (!(error instanceof HttpError)) console.error(`Pass ${pass} (${id}): ${error.message}`);
      if (submitting && actual) {
        const uncertain = { ...submitting, stage: 'uncertain' };
        this.#jobs.set(id, uncertain);
        try { await this.#save(uncertain); } catch (persistError) { console.error(persistError.message); }
        return uncertain;
      }
      if (actual && first) this.#active.delete(printer);
      if (submitting) {
        this.#jobs.set(id, job);
        try { await this.#save(job); } catch (persistError) { console.error(persistError.message); }
      }
      if (error instanceof HttpError) throw error;
      throw new HttpError(503, actual ? '未能生成打印文件，请检查 PDF 和服务日志。' : '模拟暂时无法完成。');
    } finally {
      this.#busy.delete(id);
    }
  }

  async abandon(id) {
    const job = this.get(id);
    if (!['ready', 'awaiting-reload', 'uncertain'].includes(job.stage)) {
      throw new HttpError(409, '当前阶段无法结束任务。');
    }
    this.#enter(job);
    try {
      const result = await this.#save({ ...job, stage: 'abandoned' });
      if (this.#active.get(job.printer) === id) this.#active.delete(job.printer);
      return result;
    } finally {
      this.#busy.delete(id);
    }
  }

  async sweep(now = Date.now()) {
    for (const job of this.#jobs.values()) {
      if (this.#busy.has(job.id) || holdsPrinter(job) || now - job.updated < maxAge) continue;
      this.#busy.add(job.id);
      try {
        await rm(this.folder(job.id), { recursive: true, force: true });
        this.#jobs.delete(job.id);
      } finally {
        this.#busy.delete(job.id);
      }
    }
  }
}
