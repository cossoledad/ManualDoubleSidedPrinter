import assert from 'node:assert/strict';
import { test } from 'node:test';
import { Viewport } from '../public/viewport.mjs';

class Element extends EventTarget {
  style = {};
  captured = new Set();
  clientWidth = 524;
  clientHeight = 624;

  getBoundingClientRect() { return { left: 0, top: 0, width: this.clientWidth, height: this.clientHeight }; }
  setPointerCapture(id) { this.captured.add(id); }
  hasPointerCapture(id) { return this.captured.has(id); }
  releasePointerCapture(id) { this.captured.delete(id); }
  removeAttribute() {}
  toggleAttribute() {}
  send(type, values) { this.dispatchEvent(Object.assign(new Event(type, { cancelable: true }), values)); }
}

test('preview allows offscreen panning at fit scale, wheel and pinch zoom, and reset', () => {
  let resize;
  globalThis.ResizeObserver = class {
    constructor(update) { resize = update; }
    observe() {}
  };
  const frame = new Element();
  const paper = new Element();
  const buttons = { minus: new Element(), fit: new Element(), plus: new Element() };
  const viewport = new Viewport(frame, paper, buttons);
  viewport.page(200, 300);
  assert.equal(paper.style.width, '400px');
  assert.match(paper.style.transform, /translate\(0px, 0px\) scale\(1\)$/);

  frame.send('pointerdown', { pointerId: 1, clientX: 210, clientY: 210, button: 0 });
  frame.send('pointermove', { pointerId: 1, clientX: 1210, clientY: -390 });
  assert.match(paper.style.transform, /translate\(1000px, -600px\) scale\(1\)$/);
  frame.send('lostpointercapture', { pointerId: 1 });
  frame.send('pointermove', { pointerId: 1, clientX: 0, clientY: 0 });
  assert.match(paper.style.transform, /translate\(1000px, -600px\)/);

  buttons.fit.send('click');
  frame.send('wheel', { clientX: 362, clientY: 312, deltaY: -100, deltaMode: 0 });
  const zoom = Math.exp(0.2);
  assert.ok(paper.style.transform.endsWith(`translate(${100 - 100 * zoom}px, 0px) scale(${zoom})`));
  frame.send('dblclick');

  frame.send('pointerdown', { pointerId: 1, clientX: 150, clientY: 210, button: 0 });
  frame.send('pointerdown', { pointerId: 2, clientX: 250, clientY: 210, button: 0 });
  frame.send('pointermove', { pointerId: 2, clientX: 350, clientY: 210 });
  assert.match(paper.style.transform, /scale\(2\)$/);
  frame.clientHeight = 324;
  resize();
  assert.equal(paper.style.width, '200px');
  assert.match(paper.style.transform, /scale\(2\)$/);
  viewport.page();
  assert.equal(paper.hidden, true);
  assert.equal(buttons.fit.disabled, true);
  assert.equal(frame.captured.size, 0);
});
