import { Viewport } from './viewport.mjs';

const $ = (id) => document.getElementById(id);
const tabs = [...document.querySelectorAll('[data-view]')];
const image = $('preview-image');
const viewport = new Viewport($('viewport'), $('preview-paper'), {
  minus: $('zoom-out'), fit: $('zoom-fit'), plus: $('zoom-in'),
});
const key = 'mdsprinter-job';
const printerKey = 'mdsprinter-printer';
let config = { printers: [], defaultPrinter: '', activeJobs: [], maxUploadMiB: 50 };
let job = null;
let destination = null;
let destinationOptions = '';
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
  if (!response.ok) throw Object.assign(new Error(result.error ?? '����ʧ�ܡ�'), { status: response.status });
  return result;
}

function post(path, body) {
  return api(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
}

function endpoint(action = '') { return `/api/jobs/${job.id}${action}`; }

function applyConfig(next) {
  config = next;
  if (destination !== null) return;
  let saved;
  try { saved = localStorage.getItem(printerKey); } catch { /* Storage can be disabled. */ }
  const names = config.printers.filter((printer) => printer.accepting).map((printer) => printer.name);
  destination = saved === '' || names.includes(saved) ? saved :
    names.includes(config.defaultPrinter) ? config.defaultPrinter : names[0] ?? '';
}

function updateDestinations(printer) {
  const options = config.printers.map(({ name, accepting }) => ({ name, accepting }));
  if (printer && !options.some(({ name }) => name === printer)) options.push({ name: printer, accepting: false });
  const signature = JSON.stringify(options);
  const select = $('mode');
  if (signature !== destinationOptions) {
    destinationOptions = signature;
    const choices = options.map(({ name, accepting }) => {
      const option = new Option(accepting ? name : `${name}���ݲ����ã�`, name);
      option.disabled = !accepting;
      return option;
    });
    select.replaceChildren(...choices, new Option('ģ���ӡ������ֽ��', ''));
  }
  select.value = printer;
}

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
  if (job.mode === 'print') {
    config.activeJobs = config.activeJobs.filter(({ id }) => id !== job.id);
    if (['awaiting-reload', 'submitting-first', 'submitting-second', 'uncertain'].includes(job.stage)) {
      config.activeJobs.push({ printer: job.printer, id: job.id });
    }
  }
  try { localStorage.setItem(key, job.id); } catch { /* Reload recovery uses the server's active job as well. */ }
}

function formatPages(pages) {
  if (!pages.length) return '�����ӡ';
  const text = pages.slice(0, 9).map((page) => page || '�հ�').join(', ');
  return pages.length > 9 ? `${text} �� �� ${pages.length} ��` : text;
}

function stageText() {
  if (!job) return 'ѡ�� PDF �󼴿�Ԥ����';
  const simulated = job.mode === 'simulate';
  const queue = Object.values(job.queue).filter(Boolean).join('��');
  switch (job.stage) {
    case 'awaiting-reload':
      return simulated ? 'ģ���һ����ɡ������ֽʾ��������' :
        `��һ�����ύ${queue ? `��${queue}��` : ''}�����ȫ����ֽ���ٰ��·���ʾ�Ż�ֽ����`;
    case 'submitting-first': return '�����ύ��һ�顭';
    case 'submitting-second': return '�����ύ�ڶ��顭';
    case 'complete':
      return simulated ? 'ģ����ɣ�δ�ύ��ӡ����' : `��ӡ�������ύ${queue ? `��${queue}��` : ''}��`;
    case 'uncertain': return `�� ${job.pendingPass} ���ύ�����ȷ�ϡ���˶� CUPS ���к�ֽ�ţ��ٽ�������`;
    case 'abandoned': return '���������ѽ�����';
    default: return destination ? '���ҳ��󼴿ɴ�ӡ��' : 'ģ��ģʽ���������������̡�';
  }
}

function updatePreview() {
  const pages = currentPages();
  index = Math.max(0, Math.min(index, pages.length - 1));
  const page = pages[index];
  $('previous').disabled = index === 0;
  $('next').disabled = index >= pages.length - 1;
  $('page-label').textContent = page === undefined ? '��' : `${index + 1} / ${pages.length}${
    view === 'original' ? '' : page ? ` �� PDF �� ${page} ҳ` : ' �� �հ�ռλ'}`;
  for (const tab of tabs) {
    const value = tab.dataset.view;
    tab.disabled = !job || (value !== 'original' && !job.plan[value].length);
    tab.setAttribute('aria-pressed', String(value === view));
  }

  const nextImage = page ? `${job.id}/${page}` : '';
  const blank = page === 0;
  $('blank-page').hidden = !blank;
  if (!nextImage) {
    const paperChanged = imageKey !== '' || $('preview-paper').hidden === blank;
    imageKey = '';
    image.removeAttribute('src');
    image.hidden = true;
    $('retry-preview').hidden = true;
    $('preview-note').hidden = blank;
    $('preview-note').textContent = '��δѡ���ĵ�';
    if (paperChanged) {
      if (blank) viewport.page(210, 297);
      else viewport.page();
    }
  } else if (nextImage !== imageKey) {
    imageKey = nextImage;
    image.hidden = true;
    viewport.page();
    image.alt = `PDF �� ${page} ҳ`;
    $('retry-preview').hidden = true;
    $('preview-note').hidden = false;
    $('preview-note').textContent = '���ڼ���Ԥ����';
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
  const printer = job?.mode ? job.printer ?? '' : destination ?? '';
  const simulated = !printer;
  const active = config.activeJobs.find((entry) => entry.printer === printer && entry.id !== job?.id);
  const available = config.printers.some(({ name, accepting }) => name === printer && accepting);
  $('choose').disabled = pending || waiting || stage === 'uncertain';
  $('range').disabled = pending || !ready;
  $('apply').disabled = pending || !dirty;
  $('mode').disabled = pending || Boolean(job?.mode);
  $('refresh-printers').disabled = pending;
  updateDestinations(printer);
  $('file-name').textContent = job ? `${job.name} �� ${job.count} ҳ` : `PDF����� ${config.maxUploadMiB} MiB`;
  $('plan').hidden = !job;
  if (job) {
    $('first-pages').textContent = formatPages(job.plan.first);
    $('second-pages').textContent = formatPages(job.plan.second);
  }
  $('guide').hidden = !waiting;
  $('print').hidden = ['complete', 'abandoned', 'uncertain'].includes(stage);
  $('print').disabled = pending || dirty || !(ready || waiting) || (!simulated && (!available || Boolean(active)));
  $('print').textContent = waiting || stage === 'submitting-second' ?
    (simulated ? '����ģ��ڶ���' : '�ѷŻأ���ӡ�ڶ���') :
    (job && !job.plan.second.length ? (simulated ? 'ģ���ӡ' : '��ӡ') : (simulated ? 'ģ���һ��' : '��ӡ��һ��'));
  $('abandon').hidden = !(waiting || stage === 'uncertain');
  $('abandon').disabled = pending;
  $('resume').hidden = !active || Boolean(job?.mode);
  $('resume').disabled = pending;
  $('reload').hidden = !job || !failed;
  $('reload').disabled = pending;
  const printerStatus = active && !job?.mode ? '��̨��ӡ����һ��δ������˫������' :
    !simulated && !available && (ready || waiting) ? '��̨��ӡ���ݲ����ã���ˢ���б���' :
    config.printerError && (!job || ready) ? '��ǰ�޷���ȡ CUPS ���У��Կ�ģ���ӡ��' : '';
  $('status').textContent = busy || message || printerStatus || stageText();
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
  applyConfig(nextConfig);
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
  viewport.page(image.naturalWidth, image.naturalHeight);
});
image.addEventListener('error', () => {
  if (!imageKey) return;
  image.hidden = true;
  viewport.page();
  $('preview-note').hidden = false;
  $('preview-note').textContent = 'Ԥ����ʱ�޷����ء�';
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
  if (file.size > config.maxUploadMiB * 1024 ** 2) return showError(new Error(`PDF ���� ${config.maxUploadMiB} MiB��`));
  action('�����ϴ� PDF��', async () => {
    setJob(await api('/api/jobs', { method: 'POST', body: file, headers: {
      'Content-Type': 'application/pdf', 'X-File-Name': encodeURIComponent(file.name),
    } }));
  });
});
$('range').addEventListener('input', () => { message = ''; failed = false; render(); });
$('range-form').addEventListener('submit', (event) => {
  event.preventDefault();
  if (job?.stage !== 'ready' || $('apply').disabled) return;
  action('���ڸ���ҳ��', async () => {
    setJob(await post(endpoint('/range'), { range: $('range').value, revision: job.revision }));
  });
});
$('mode').addEventListener('change', () => {
  destination = $('mode').value;
  try { localStorage.setItem(printerKey, destination); } catch { /* Storage can be disabled. */ }
  message = '';
  failed = false;
  render();
});
$('refresh-printers').addEventListener('click', () => action('����ˢ�´�ӡ����', refresh));
$('print').addEventListener('click', () => {
  if (!job || $('print').disabled) return;
  const pass = job.stage === 'awaiting-reload' ? 2 : 1;
  const printer = pass === 1 ? destination : job.printer;
  const selected = printer ? 'print' : 'simulate';
  action(selected === 'simulate' ? '����ģ�⡭' : `�����ύ�� ${pass} �顭`, async () => {
    setJob(await post(endpoint('/print'), { pass, mode: selected, revision: job.revision, printer: printer || undefined }));
  });
});
$('abandon').addEventListener('click', () => {
  const question = job?.mode === 'print' ? '��������˫�����̣����ύ�� CUPS ����ҵ���ɴ�ӡ��������' : '��������ģ�⣿';
  if (confirm(question)) action('���ڽ�������', async () => setJob(await post(endpoint('/abandon'))));
});
$('resume').addEventListener('click', () => {
  const active = config.activeJobs.find(({ printer }) => printer === destination);
  if (active) action('���ڶ�ȡ����', async () => setJob(await api(`/api/jobs/${active.id}`)));
});
$('reload').addEventListener('click', () => action('����ˢ��״̬��', refresh));
document.addEventListener('visibilitychange', () => {
  if (!document.hidden && !busy) refresh().catch(showError);
});

action('�������ӡ�', async () => {
  applyConfig(await api('/api/config'));
  let saved;
  try { saved = localStorage.getItem(key); } catch { /* Storage can be disabled. */ }
  if (saved) {
    try { setJob(await api(`/api/jobs/${encodeURIComponent(saved)}`)); }
    catch (error) { if (error.status !== 404) throw error; }
  }
});
