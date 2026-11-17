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

function setup({ reduced = false, saveData = false, observer = true, narrow = false, legacy = false } = {}) {
  const document = new Element(), window = new Element(), motion = new Element(), connection = new Element(), framing = new Element();
  framing.matches = narrow; motion.matches = reduced; connection.saveData = saveData; document.hidden = false;
  const ids = ['product-film', 'film-actions', 'film-toggle', 'film-sound', 'film-fullscreen', 'film-status', 'film-screen', 'film-poster', 'hero-stage'];
  const elements = Object.fromEntries(ids.map(id => [id, new Element()]));
  const video = elements['product-film'];
  Object.assign(video, { dataset: { src: 'assets/tunpack-film.mp4', srcPortrait: 'assets/portrait.mp4', posterWide: 'wide.webp', posterPortrait: 'portrait.webp' }, paused: true, muted: true, ended: false, readyState: 0, currentTime: 0, duration: 20, playCalls: 0 });
  if (legacy) { delete video.dataset.srcPortrait; delete video.dataset.posterPortrait; delete video.dataset.posterWide; video.poster = 'legacy.webp'; }
  video.querySelector('track').dataset.src = 'assets/film.zh-CN.vtt';
  video.play = () => { video.playCalls++; if (video.reject) return Promise.reject(Object.assign(new Error('blocked'), {name: 'NotAllowedError'})); video.paused = false; video.emit('play'); return Promise.resolve(); };
  video.pause = () => { if (!video.paused) { video.paused = true; video.emit('pause'); } };
  const chapters = [0, 4.8, 11].map((time, i) => Object.assign(new Element(), { dataset: {time: String(time), end: String([4.8, 11, 20][i])} }));
  document.querySelector = name => elements[name.slice(1)]; document.querySelectorAll = () => chapters;
  window.matchMedia = query => query.includes("max-width") ? framing : motion;
  let onIntersection;
  const context = { document, window, navigator: { connection } };
  if (observer) { context.IntersectionObserver = class { constructor(fn) {onIntersection = fn;} observe() {} }; window.IntersectionObserver = context.IntersectionObserver; }
  vm.runInNewContext(code, context);
  return { ...elements, video, chapters, document, window, motion, connection, framing, intersect(visible) { onIntersection([{isIntersecting: visible, intersectionRatio: visible ? 1 : 0}]); } };
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
    e.video.readyState = 1; e.video.emit('loadedmetadata'); assert.equal(e.video.currentTime, 11); e.video.emit('timeupdate'); assert.equal(e.chapters[2].attrs['aria-current'], 'true'); assert.equal(e.chapters[0].attrs['aria-current'], undefined);
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

test('Phone selects reflowed portrait poster and media without loading a second source', async () => {
  const e = setup({narrow:true}); assert.equal(e.video.poster, 'portrait.webp'); assert.equal(e.video.width,720); assert.equal(e.video.height,900); assert.equal(e.video.src,undefined); assert.equal(e['film-poster'].hidden,true); assert.equal(e.video.hidden,false); assert.equal(e['film-screen'].dataset.framing,'portrait');
  e.intersect(true); await tick(); assert.equal(e.video.src,'assets/portrait.mp4');
  e.video.currentTime=12; e['film-toggle'].emit('click'); e.framing.matches=false; e.framing.emit('change'); await tick(); assert.equal(e.video.src,'assets/portrait.mp4'); assert.equal(e.video.currentTime,12); assert.equal(e.video.paused,true); assert.equal(e['film-screen'].dataset.framing,'portrait');
});

test('Framing may change before first load under reduced motion and save-data', async () => {
  for (const option of [{reduced:true},{saveData:true}]) {
    const e=setup(option); e.intersect(true); e.framing.matches=true; e.framing.emit('change'); assert.equal(e.video.poster,'portrait.webp'); assert.equal(e.video.src,undefined); assert.equal(e.video.querySelector('track').src,undefined);
    e['film-toggle'].emit('click'); await tick(); assert.equal(e.video.src,'assets/portrait.mp4'); assert.equal(e.video.width,720);
  }
});

test('Wide film also keeps position and framing across rotation after loading', async () => {
  const e=setup();e.intersect(true);await tick(); e.video.currentTime=9;e.framing.matches=true;e.framing.emit('change');assert.equal(e.video.src,'assets/tunpack-film.mp4');assert.equal(e.video.width,1280);assert.equal(e.video.currentTime,9);
});

test('Cached prior homepage keeps its original poster when the new controller arrives', async () => {
  const e=setup({legacy:true,reduced:true,narrow:true});e.intersect(true);assert.equal(e.video.poster,'legacy.webp');assert.equal(e.video.src,undefined);e['film-toggle'].emit('click');await tick();assert.equal(e.video.src,'assets/tunpack-film.mp4');assert.equal(e.video.width,1280);
});


test('Product story uses declared delivery sizes and freezes after start', async () => {
  const e=setup({reduced:true,narrow:true});Object.assign(e.video.dataset,{wideWidth:'1536',wideHeight:'720',portraitWidth:'720',portraitHeight:'1100'});e.framing.emit('change');assert.equal(e.video.width,720);assert.equal(e.video.height,1100);e.intersect(true);e['film-toggle'].emit('click');await tick();e.framing.matches=false;e.framing.emit('change');assert.equal(e.video.width,720);
});


test('Frozen portrait media also freezes the stacked hero layout marker across rotation', async () => {
  const e=setup({narrow:true});e.intersect(true);await tick();e.framing.matches=false;e.framing.emit('change');assert.equal(e['hero-stage'].dataset.framing,'portrait');assert.equal(e['film-screen'].dataset.framing,'portrait');
});

test('Explicit fullscreen from a reduced-motion poster prepares native controls without forcing play', async () => {
  const e=setup({reduced:true,narrow:true});e.intersect(true);assert.equal(e.video.src,undefined);e['film-fullscreen'].emit('click');await tick();assert.equal(e.video.src,'assets/portrait.mp4');assert.equal(e.video.controls,true);assert.equal(e.video.paused,true);assert.equal(e.video.playCalls,0);
});
