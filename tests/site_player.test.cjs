// Node-only media lifecycle tests. No browser, GUI, network or real media player.
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const code = fs.readFileSync(path.join(__dirname, '../site/player.js'), 'utf8');

class Element {
  constructor() { this.handlers = new Map(); this.attrs = {}; this.nodes = {}; this.dataset = {}; this.textContent = ''; this.disabled = true; }
  addEventListener(name, fn) { if (!this.handlers.has(name)) this.handlers.set(name, []); this.handlers.get(name).push(fn); }
  emit(name, event = {}) { for (const fn of this.handlers.get(name) || []) fn(event); }
  querySelector(name) { return this.nodes[name] ||= new Element(); }
  setAttribute(name, value) { this.attrs[name] = value; }
  removeAttribute(name) { delete this.attrs[name]; }
  scrollIntoView() { this.scrolled = true; }
}

function setup({ reduced = false, saveData = false, observer = true } = {}) {
  const document = new Element(), window = new Element(), motion = new Element(), connection = new Element();
  motion.matches = reduced; connection.saveData = saveData; document.hidden = false;
  const ids = ['product-film', 'film-actions', 'film-toggle', 'film-sound', 'film-fullscreen', 'film-status'];
  const elements = Object.fromEntries(ids.map(id => [id, new Element()]));
  const video = elements['product-film'];
  Object.assign(video, { dataset: { src: 'assets/tunpack-film.mp4' }, paused: true, muted: true, ended: false, readyState: 0, currentTime: 0, duration: 24, playCalls: 0 });
  video.querySelector('track').dataset.src = 'assets/film.zh-CN.vtt';
  video.play = () => { video.playCalls++; if (video.reject) return Promise.reject(Object.assign(new Error('blocked'), {name: 'NotAllowedError'})); video.paused = false; video.emit('play'); return Promise.resolve(); };
  video.pause = () => { if (!video.paused) { video.paused = true; video.emit('pause'); } };
  const chapters = [0, 7.5, 15].map((time, i) => Object.assign(new Element(), { dataset: {time: String(time), end: String([7.5, 15, 24][i])} }));
  document.querySelector = name => elements[name.slice(1)]; document.querySelectorAll = () => chapters;
  window.matchMedia = () => motion;
  let onIntersection;
  const context = { document, window, navigator: { connection } };
  if (observer) { context.IntersectionObserver = class { constructor(fn) {onIntersection = fn;} observe() {} }; window.IntersectionObserver = context.IntersectionObserver; }
  vm.runInNewContext(code, context);
  return { ...elements, video, chapters, document, window, motion, connection, intersect(visible) { onIntersection([{isIntersecting: visible, intersectionRatio: visible ? 1 : 0}]); } };
}

const tick = () => new Promise(resolve => setImmediate(resolve));

test('Visible film lazily attaches source and captions, offscreen auto-play suspends and resumes', async () => {
  const e = setup(); assert.equal(e.video.src, undefined); assert.equal(e.video.controls, false); assert.equal(e['film-actions'].hidden, false);
  e.intersect(true); await tick(); assert.equal(e.video.src, 'assets/tunpack-film.mp4'); assert.equal(e.video.querySelector('track').src, 'assets/film.zh-CN.vtt'); assert.equal(e.video.paused, false); assert.equal(e.video.muted, true); assert.equal(e.video.controls, true);
  e.intersect(false); assert.equal(e.video.paused, true); e.intersect(true); await tick(); assert.equal(e.video.paused, false);
});

test('Manual pause survives scroll, hidden tab and BFCache return', async () => {
  const e = setup(); e.intersect(true); await tick(); e['film-toggle'].emit('click'); assert.equal(e.video.paused, true);
  e.intersect(false); e.intersect(true); e.document.hidden = true; e.document.emit('visibilitychange'); e.window.emit('pagehide'); e.document.hidden = false; e.window.emit('pageshow'); await tick(); assert.equal(e.video.paused, true); assert.equal(e.video.playCalls, 1);
  e['film-toggle'].emit('click'); await tick(); assert.equal(e.video.paused, false);
});

test('Reduced motion and save-data keep poster until explicit chapter playback', async () => {
  for (const option of [{ reduced: true }, { saveData: true }]) {
    const e = setup(option); e.intersect(true); await tick(); assert.equal(e.video.src, undefined); assert.equal(e.video.playCalls, 0);
    assert.equal(e.chapters[2].disabled, false); e.chapters[2].emit('click'); await tick(); assert.equal(e.video.paused, false); assert.equal(e.video.scrolled, true);
    e.video.readyState = 1; e.video.emit('loadedmetadata'); assert.equal(e.video.currentTime, 15); e.video.emit('timeupdate'); assert.equal(e.chapters[2].attrs['aria-current'], 'true'); assert.equal(e.chapters[0].attrs['aria-current'], undefined);
  }
});

test('Playback rejection remains retryable and network failure has useful feedback', async () => {
  const e = setup(); e.video.reject = true; e.intersect(true); await tick(); assert.match(e['film-status'].textContent, /点击播放/);
  e.video.reject = false; e['film-toggle'].emit('click'); await tick(); assert.equal(e.video.paused, false); assert.equal(e['film-status'].textContent, '');
  e.video.emit('error'); assert.match(e['film-status'].textContent, /下载链接/);
});

test('Background and pagehide prevent playback until the page is active again', async () => {
  const e = setup(); e.intersect(true); await tick(); e.window.emit('pagehide'); assert.equal(e.video.paused, true); const calls = e.video.playCalls;
  e.document.emit('visibilitychange'); await tick(); assert.equal(e.video.playCalls, calls); e.window.emit('pageshow'); await tick(); assert.equal(e.video.paused, false);
});

test('Native pause is a user choice; preference changes cannot override it', async () => {
  const e = setup(); e.intersect(true); await tick(); e.video.pause(); e.motion.matches = true; e.motion.emit('change'); e.motion.matches = false; e.motion.emit('change'); e.intersect(false); e.intersect(true); await tick(); assert.equal(e.video.paused, true);
});

test('No IntersectionObserver still provides explicit keyboard start and sound control', async () => {
  const e = setup({reduced: true, observer: false}); assert.equal(e.video.src, undefined); let prevented = false;
  e.video.emit('keydown', {key: 'Enter', preventDefault() {prevented = true;}}); await tick(); assert.equal(prevented, true); assert.equal(e.video.paused, false);
  e['film-sound'].emit('click'); await tick(); assert.equal(e.video.muted, false); assert.equal(e['film-sound'].attrs['aria-pressed'], 'true'); e['film-sound'].emit('click'); assert.equal(e.video.muted, true);
});

test('Fullscreen unavailable or rejected reports native control fallback', async () => {
  const e = setup(); e['film-fullscreen'].emit('click'); await tick(); assert.match(e['film-status'].textContent, /全屏按钮/);
  e.video.requestFullscreen = () => Promise.reject(new Error('not allowed')); e['film-fullscreen'].emit('click'); await tick(); assert.match(e['film-status'].textContent, /全屏按钮/);
});

test('Late play resolution cannot resume an offscreen or hidden film', async () => {
  const e = setup(); let resolve;
  e.video.play = () => new Promise(done => {resolve = done;}); e.intersect(true); e.intersect(false); e.video.paused = false; resolve(); await tick();
  // A late media play event is also checked against the current visibility.
  e.video.emit('play'); assert.equal(e.video.paused, true);
});
