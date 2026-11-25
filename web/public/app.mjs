const $ = (id) => document.getElementById(id);
const tabs = [...document.querySelectorAll('[data-view]')];
const image = $('preview-image');
const key = 'mdsprinter-job';
let config = { printer: '', activeId: null, maxUploadMiB: 50 };
let job = null;
let mode = 'simulate';
let view = 'original';
let index = 0;
let busy = '';
let message = '';
let failed = false;
let imageKey = '';
let refreshTimer;

async function api(path, options = {}) {
  const method = options.method ?? 'GET';
  const headers = { ...options.headers };
  if (method === 'POST') headers['X-MDS-Request'] = '1';
  const response = await fetch(path, { ...options, method, headers });
  const result = await response.json();
  if (!response.ok) throw Object.assign(new Error(result.error ?? '请求失败。'), { status: response.status });
  return result;
}

function post(path, body) {
  return api(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
}

function endpoint(action = '') { return `/api/jobs/${job.id}${action}`; }

function currentPages() {
  if (!job) return [];
  if (view === 'original') return Array.from({ length: job.count }, (_, i) => i + 1);
  return job.plan[view];
}

function setJob(next) {
  const isNew = job?.id !== next.id;
  if (isNew || job.revision !== next.revision) $('range').value = next.range;
  if (next.stage === 'awaiting-reload' && (isNew || job.stage !== next.stage)) {
    view = 'second';
    index = 0;
  } else if (isNew) {
    view = 'original';
    index = 0;
  }
  if (view !== 'original' && !next.plan[view].length) view = 'first';
  job = next;
  if (job.mode) mode = job.mode;
  if (job.mode === 'print') {
    if (['awaiting-reload', 'submitting-first', 'submitting-second', 'uncertain'].includes(job.stage)) config.activeId = job.id;
    if (['complete', 'abandoned'].includes(job.stage) && config.activeId === job.id) config.activeId = null;
  }
  try { localStorage.setItem(key, job.id); } catch { /* Reload recovery uses the server's active job as well. */ }
}

function formatPages(pages) {
  if (!pages.length) return '无需打印';
  const text = pages.slice(0, 9).map((page) => page || '空白').join(', ');
  return pages.length > 9 ? `${text} … 共 ${pages.length} 面` : text;
}

function stageText() {
  if (!job) return '选择 PDF 后即可预览。';
  const simulated = job.mode === 'simulate';
  const queue = Object.values(job.queue).filter(Boolean).join('、');
  switch (job.stage) {
    case 'awaiting-reload':
      return simulated ? '模拟第一遍完成。看完放纸示意后继续。' :
        `第一遍已提交${queue ? `（${queue}）` : ''}。请等全部出纸，再按下方所示放回纸叠。`;
    case 'submitting-first': return '正在提交第一遍…';
    case 'submitting-second': return '正在提交第二遍…';
    case 'complete':
      return simulated ? '模拟完成，未提交打印任务。' : `打印任务已提交${queue ? `（${queue}）` : ''}。`;
    case 'uncertain': return `第 ${job.pendingPass} 遍提交结果待确认。请核对 CUPS 队列和纸张，再结束任务。`;
    case 'abandoned': return '本次任务已结束。';
    default: return mode === 'simulate' ? '模拟模式用于演练两遍流程。' : '检查页序后即可打印。';
  }
}

function updatePreview() {
  const pages = currentPages();
  index = Math.max(0, Math.min(index, pages.length - 1));
  const page = pages[index];
  $('previous').disabled = index === 0;
  $('next').disabled = index >= pages.length - 1;
  $('page-label').textContent = page === undefined ? '—' : `${index + 1} / ${pages.length}${
    view === 'original' ? '' : page ? ` · PDF 第 ${page} 页` : ' · 空白占位'}`;
  for (const tab of tabs) {
    const value = tab.dataset.view;
    tab.disabled = !job || (value !== 'original' && !job.plan[value].length);
    tab.setAttribute('aria-pressed', String(value === view));
  }

  const nextImage = page ? `${job.id}/${page}` : '';
  $('blank-page').hidden = page !== 0;
  if (!nextImage) {
    imageKey = '';
    image.removeAttribute('src');
    image.hidden = true;
    $('retry-preview').hidden = true;
    $('preview-note').hidden = page === 0;
    $('preview-note').textContent = '尚未选择文档';
  } else if (nextImage !== imageKey) {
    imageKey = nextImage;
    image.hidden = true;
    image.alt = `PDF 第 ${page} 页`;
    $('retry-preview').hidden = true;
    $('preview-note').hidden = false;
    $('preview-note').textContent = '正在加载预览…';
    image.src = endpoint(`/preview/${page}`);
  }
}

function render() {
  const stage = job?.stage;
  const ready = stage === 'ready';
  const waiting = stage === 'awaiting-reload';
  const submitting = stage?.startsWith('submitting-');
  const pending = Boolean(busy || submitting);
  const dirty = ready && $('range').value.trim() !== job.range;
  const simulated = (ready ? mode : job?.mode) === 'simulate';
  const reserving = config.activeId && config.activeId !== job?.id;
  $('choose').disabled = pending || waiting || stage === 'uncertain';
  $('range').disabled = pending || !ready;
  $('apply').disabled = pending || !dirty;
  $('mode').disabled = pending || !ready;
  $('mode').options[0].disabled = !config.printer;
  $('mode').options[0].textContent = config.printer || '未配置 CUPS 队列';
  $('mode').value = ready ? mode : (job?.mode ?? mode);
  $('file-name').textContent = job ? `${job.name} · ${job.count} 页` : `PDF，最大 ${config.maxUploadMiB} MiB`;
  $('plan').hidden = !job;
  if (job) {
    $('first-pages').textContent = formatPages(job.plan.first);
    $('second-pages').textContent = formatPages(job.plan.second);
  }
  $('guide').hidden = !waiting;
  $('print').hidden = ['complete', 'abandoned', 'uncertain'].includes(stage);
  $('print').disabled = pending || dirty || !(ready || waiting) || (!simulated && (!config.printer || reserving));
  $('print').textContent = waiting || stage === 'submitting-second' ?
    (simulated ? '继续模拟第二遍' : '已放回，打印第二遍') :
    (job && !job.plan.second.length ? (simulated ? '模拟打印' : '打印') : (simulated ? '模拟第一遍' : '打印第一遍'));
  $('abandon').hidden = !(waiting || stage === 'uncertain');
  $('abandon').disabled = pending;
  $('resume').hidden = !reserving;
  $('resume').disabled = pending;
  $('reload').hidden = !job || !failed;
  $('reload').disabled = pending;
  $('status').textContent = busy || message || (reserving && ready && mode === 'print' ? '打印机有一项未结束的双面任务。' : stageText());
  $('status').classList.toggle('error', failed && !busy);
  updatePreview();

  clearTimeout(refreshTimer);
  if (submitting && !busy) refreshTimer = setTimeout(() => refresh().catch(showError), 800);
}

function showError(error) {
  message = error.message;
  failed = true;
  render();
}

async function refresh() {
  const previous = job;
  const nextConfig = await api('/api/config');
  const nextJob = previous && await api(`/api/jobs/${previous.id}`).catch((error) => {
    if (error.status !== 404) throw error;
    return null;
  });
  if (job !== previous) return;
  config = nextConfig;
  if (nextJob) setJob(nextJob);
  else if (previous) { job = null; view = 'original'; index = 0; }
  render();
}

async function action(label, perform) {
  if (busy) return;
  busy = label;
  message = '';
  failed = false;
  render();
  try { await perform(); }
  catch (error) {
    failed = true;
    message = error.message;
    try { await refresh(); } catch { /* Preserve the original error for the user. */ }
  } finally {
    busy = '';
    render();
  }
}

image.addEventListener('load', () => {
  if (!imageKey) return;
  image.hidden = false;
  $('preview-note').hidden = true;
});
image.addEventListener('error', () => {
  if (!imageKey) return;
  image.hidden = true;
  $('preview-note').hidden = false;
  $('preview-note').textContent = '预览暂时无法加载。';
  $('retry-preview').hidden = false;
});
$('retry-preview').addEventListener('click', () => { imageKey = ''; updatePreview(); });
for (const tab of tabs) tab.addEventListener('click', () => { view = tab.dataset.view; index = 0; updatePreview(); });
$('previous').addEventListener('click', () => { index -= 1; updatePreview(); });
$('next').addEventListener('click', () => { index += 1; updatePreview(); });
$('choose').addEventListener('click', () => $('file').click());
$('file').addEventListener('change', () => {
  const [file] = $('file').files;
  $('file').value = '';
  if (!file) return;
  if (file.size > config.maxUploadMiB * 1024 ** 2) return showError(new Error(`PDF 超过 ${config.maxUploadMiB} MiB。`));
  action('正在上传 PDF…', async () => {
    setJob(await api('/api/jobs', { method: 'POST', body: file, headers: {
      'Content-Type': 'application/pdf', 'X-File-Name': encodeURIComponent(file.name),
    } }));
  });
});
$('range').addEventListener('input', () => { message = ''; failed = false; render(); });
$('range-form').addEventListener('submit', (event) => {
  event.preventDefault();
  if (job?.stage !== 'ready' || $('apply').disabled) return;
  action('正在更新页序…', async () => {
    setJob(await post(endpoint('/range'), { range: $('range').value, revision: job.revision }));
  });
});
$('mode').addEventListener('change', () => { mode = $('mode').value; message = ''; failed = false; render(); });
$('print').addEventListener('click', () => {
  if (!job || $('print').disabled) return;
  const pass = job.stage === 'awaiting-reload' ? 2 : 1;
  const selected = pass === 1 ? mode : job.mode;
  action(selected === 'simulate' ? '正在模拟…' : `正在提交第 ${pass} 遍…`, async () => {
    setJob(await post(endpoint('/print'), { pass, mode: selected, revision: job.revision }));
  });
});
$('abandon').addEventListener('click', () => {
  const question = job?.mode === 'print' ? '结束本次双面流程？已提交到 CUPS 的作业仍由打印机处理。' : '结束本次模拟？';
  if (confirm(question)) action('正在结束任务…', async () => setJob(await post(endpoint('/abandon'))));
});
$('resume').addEventListener('click', () => action('正在读取任务…', async () => setJob(await api(`/api/jobs/${config.activeId}`))));
$('reload').addEventListener('click', () => action('正在刷新状态…', refresh));
document.addEventListener('visibilitychange', () => {
  if (!document.hidden && !busy) refresh().catch(showError);
});

action('正在连接…', async () => {
  config = await api('/api/config');
  mode = config.printer ? 'print' : 'simulate';
  let saved;
  try { saved = localStorage.getItem(key); } catch { /* Storage can be disabled. */ }
  if (saved) {
    try { setJob(await api(`/api/jobs/${encodeURIComponent(saved)}`)); }
    catch (error) { if (error.status !== 404) throw error; }
  }
  if (!job && config.activeId) setJob(await api(`/api/jobs/${config.activeId}`));
});
