const minZoom = 0.5;
const maxZoom = 4;

export class Viewport {
  #dimensions = null;
  #pointers = new Map();
  #zoom = 1;
  #x = 0;
  #y = 0;

  constructor(frame, paper, { minus, fit, plus }) {
    this.frame = frame;
    this.paper = paper;
    this.buttons = { minus, fit, plus };

    new ResizeObserver(() => this.#draw()).observe(frame);
    frame.addEventListener('wheel', (event) => {
      if (!this.#dimensions) return;
      event.preventDefault();
      const delta = event.deltaY * (event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? frame.clientHeight : 1);
      this.#zoomAt(Math.exp(-Math.max(-500, Math.min(500, delta)) * 0.002), this.#position(event));
    }, { passive: false });
    frame.addEventListener('dblclick', () => { if (this.#dimensions) this.reset(); });
    frame.addEventListener('pointerdown', (event) => {
      if (!this.#dimensions || event.button !== 0 || this.#pointers.size === 2) return;
      this.#pointers.set(event.pointerId, this.#position(event));
      frame.setPointerCapture(event.pointerId);
      frame.toggleAttribute('data-dragging', true);
    });
    frame.addEventListener('pointermove', (event) => this.#move(event));
    for (const type of ['pointerup', 'pointercancel', 'lostpointercapture']) {
      frame.addEventListener(type, (event) => {
        this.#pointers.delete(event.pointerId);
        frame.toggleAttribute('data-dragging', this.#pointers.size > 0);
      });
    }
    minus.addEventListener('click', () => this.#zoomAt(1 / 1.25, { x: 0, y: 0 }));
    plus.addEventListener('click', () => this.#zoomAt(1.25, { x: 0, y: 0 }));
    fit.addEventListener('click', () => this.reset());
  }

  page(width = 0, height = 0) {
    this.#dimensions = width && height ? { width, height } : null;
    this.paper.hidden = !this.#dimensions;
    this.frame.toggleAttribute('data-ready', Boolean(this.#dimensions));
    this.reset();
  }

  reset() {
    this.#zoom = 1;
    this.#x = 0;
    this.#y = 0;
    for (const id of this.#pointers.keys()) {
      if (this.frame.hasPointerCapture(id)) this.frame.releasePointerCapture(id);
    }
    this.#pointers.clear();
    this.frame.removeAttribute('data-dragging');
    this.#draw();
  }

  #position(event) {
    const rect = this.frame.getBoundingClientRect();
    return { x: event.clientX - rect.left - rect.width / 2, y: event.clientY - rect.top - rect.height / 2 };
  }

  #move(event) {
    const previous = this.#pointers.get(event.pointerId);
    if (!previous) return;
    const next = this.#position(event);
    this.#pointers.set(event.pointerId, next);
    if (this.#pointers.size === 1) {
      this.#x += next.x - previous.x;
      this.#y += next.y - previous.y;
      this.#draw();
      return;
    }

    const [, other] = [...this.#pointers].find(([id]) => id !== event.pointerId);
    const center = (point) => ({ x: (point.x + other.x) / 2, y: (point.y + other.y) / 2 });
    const distance = (point) => Math.hypot(point.x - other.x, point.y - other.y);
    this.#zoomAt(Math.max(1, distance(next)) / Math.max(1, distance(previous)), center(previous), center(next));
  }

  #zoomAt(factor, oldAnchor, newAnchor = oldAnchor) {
    if (!this.#dimensions) return;
    const zoom = Math.max(minZoom, Math.min(maxZoom, this.#zoom * factor));
    const scale = zoom / this.#zoom;
    this.#x = newAnchor.x - (oldAnchor.x - this.#x) * scale;
    this.#y = newAnchor.y - (oldAnchor.y - this.#y) * scale;
    this.#zoom = zoom;
    this.#draw();
  }

  #draw() {
    const { minus, fit, plus } = this.buttons;
    minus.disabled = !this.#dimensions || this.#zoom <= minZoom;
    plus.disabled = !this.#dimensions || this.#zoom >= maxZoom;
    fit.disabled = !this.#dimensions;
    if (!this.#dimensions) return;
    const { width, height } = this.#dimensions;
    const factor = Math.max(0, Math.min((this.frame.clientWidth - 24) / width, (this.frame.clientHeight - 24) / height));
    this.paper.style.width = `${width * factor}px`;
    this.paper.style.height = `${height * factor}px`;
    this.paper.style.transform = `translate(-50%, -50%) translate(${this.#x}px, ${this.#y}px) scale(${this.#zoom})`;
  }
}
