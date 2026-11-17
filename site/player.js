/* global document, window, navigator, IntersectionObserver */

// One real film. A visitor's playback choice survives scrolling and visibility changes.
(() => {
  const video = document.querySelector("#product-film");
  const controls = document.querySelector("#film-actions");
  const toggle = document.querySelector("#film-toggle");
  const sound = document.querySelector("#film-sound");
  const fullscreen = document.querySelector("#film-fullscreen");
  const status = document.querySelector("#film-status");
  const chapters = [...document.querySelectorAll("[data-time]")];
  if (!video || !controls || !toggle || !sound || !fullscreen || !status) return;
  const reduced = window.matchMedia("(prefers-reduced-motion: reduce)");
  const connection = navigator.connection;
  const framing = window.matchMedia("(max-width: 700px)");
  const screen = document.querySelector("#film-screen");
  const poster = document.querySelector("#film-poster");
  const hero = document.querySelector("#hero-stage");
  let loaded = false;
  let visible = false;
  let pageActive = true;
  let userChoice = false;
  let wantsPlayback = !reduced.matches && !connection?.saveData;
  let suspending = false;
  let pendingSeek = null;
  let playGeneration = 0;
  controls.hidden = false;
  video.muted = true;
  // Until a source exists Chromium's disabled native controls swallow pointer
  // input. Show the poster with working page controls, then enable native UI.
  video.controls = false;
  video.tabIndex = 0;

  function chooseFraming() {
    if (loaded) return;
    const portrait = framing.matches && Boolean(video.dataset.srcPortrait);
    video.dataset.framing = portrait ? "portrait" : "wide";
    video.width = portrait ? Number(video.dataset.portraitWidth || 720) : Number(video.dataset.wideWidth || 1280);
    video.height = portrait ? Number(video.dataset.portraitHeight || 900) : Number(video.dataset.wideHeight || 720);
    const selectedPoster = portrait ? video.dataset.posterPortrait : video.dataset.posterWide;
    if (selectedPoster) video.poster = selectedPoster;
    if (screen) screen.dataset.framing = video.dataset.framing;
    if (hero) hero.dataset.framing = video.dataset.framing;
  }
  function load() {
    if (loaded) return;
    chooseFraming();
    loaded = true;
    video.controls = true;
    video.src = video.dataset.framing === "portrait" ? video.dataset.srcPortrait : video.dataset.src;
    const track = video.querySelector("track");
    if (track) track.src = track.dataset.src;
  }
  chooseFraming();
  video.hidden = false;
  if (poster) poster.hidden = true;
  framing.addEventListener("change", chooseFraming);
  function syncHero() {
    if (hero) hero.dataset.detail = String(!video.paused && !video.ended && video.currentTime < 3.2);
  }
  function syncControls() {
    syncHero();
    const playing = !video.paused && !video.ended;
    toggle.querySelector("[data-play-icon]").textContent = playing ? "Ⅱ" : "▷";
    toggle.querySelector("[data-play-label]").textContent = playing ? "暂停" : "播放";
    toggle.setAttribute("aria-label", playing ? "暂停影片" : "播放影片");
    sound.setAttribute("aria-pressed", String(!video.muted));
    sound.querySelector("[data-sound-label]").textContent = video.muted ? "开启声音" : "关闭声音";
  }
  function suspend() {
    playGeneration += 1;
    suspending = true;
    video.pause();
  }
  async function play(explicit = false) {
    if (!explicit && (!wantsPlayback || !visible || document.hidden || !pageActive)) return;
    load();
    suspending = false;
    const generation = ++playGeneration;
    try {
      await video.play();
      if (generation !== playGeneration) return;
      if (!visible || document.hidden || !pageActive) suspend();
      else status.textContent = "";
    } catch (error) {
      if (generation !== playGeneration || error.name === "AbortError") return;
      status.textContent = "点击播放，观看完整影片。";
    }
  }
  function reconcile() {
    if (!visible || document.hidden || !pageActive || !wantsPlayback) suspend();
    else play();
  }
  function userPlay() {
    userChoice = true;
    wantsPlayback = true;
    play(true);
  }
  // Empty native media controls cannot emit `play`; retain that direct entry point
  // when reduced motion or data saving deliberately deferred source attachment.
  video.addEventListener("click", () => {
    if (!loaded) userPlay();
  });
  video.addEventListener("keydown", (event) => {
    if (!loaded && (event.key === " " || event.key === "Enter")) {
      event.preventDefault();
      userPlay();
    }
  });
  toggle.addEventListener("click", () => {
    userChoice = true;
    if (!video.paused) {
      wantsPlayback = false;
      suspend();
      syncControls();
    } else userPlay();
  });
  sound.addEventListener("click", () => {
    video.muted = !video.muted;
    syncControls();
    if (!video.muted) userPlay();
  });
  fullscreen.addEventListener("click", async () => {
    load(); // Fullscreen must expose native play controls even before first playback.
    try {
      if (video.requestFullscreen) await video.requestFullscreen();
      else if (video.webkitEnterFullscreen) video.webkitEnterFullscreen();
      else status.textContent = "请使用播放器中的全屏按钮。";
    } catch {
      status.textContent = "请使用播放器中的全屏按钮。";
    }
  });
  video.addEventListener("play", () => {
    syncHero();
    suspending = false;
    if (!wantsPlayback) {
      userChoice = true;
      wantsPlayback = true;
    }
    if (!visible || document.hidden || !pageActive) suspend();
    syncControls();
  });
  video.addEventListener("pause", () => {
    if (!suspending && visible && !document.hidden && pageActive) {
      userChoice = true;
      wantsPlayback = false;
    }
    syncControls();
  });
  video.addEventListener("volumechange", syncControls);
  video.addEventListener("loadedmetadata", () => {
    if (pendingSeek !== null) {
      video.currentTime = Math.min(pendingSeek, video.duration);
      pendingSeek = null;
    }
  });
  video.addEventListener("ended", syncControls);
  video.addEventListener("error", () => {
    if (hero) hero.dataset.detail = "false";
    status.textContent = "影片暂时无法载入，可以使用页尾的下载链接。";
  });
  chapters.forEach((button) => {
    button.disabled = false;
    button.addEventListener("click", () => {
      const seconds = Number(button.dataset.time);
      load();
      if (video.readyState >= 1) video.currentTime = seconds;
      else pendingSeek = seconds;
      video.scrollIntoView({ block: "center", behavior: reduced.matches ? "instant" : "smooth" });
      visible = true;
      userPlay();
    });
  });
  video.addEventListener("timeupdate", () => {
    syncHero();
    for (const button of chapters) {
      const active =
        video.currentTime >= Number(button.dataset.time) &&
        video.currentTime < Number(button.dataset.end);
      if (active) button.setAttribute("aria-current", "true");
      else button.removeAttribute("aria-current");
    }
  });
  function preferencesChanged() {
    if (!userChoice) {
      wantsPlayback = !reduced.matches && !connection?.saveData;
      reconcile();
    }
  }
  reduced.addEventListener("change", preferencesChanged);
  connection?.addEventListener("change", preferencesChanged);
  document.addEventListener("visibilitychange", reconcile);
  window.addEventListener("pagehide", () => {
    pageActive = false;
    suspend();
  });
  window.addEventListener("pageshow", () => {
    pageActive = true;
    reconcile();
  });
  if ("IntersectionObserver" in window) {
    new IntersectionObserver(
      ([entry]) => {
        visible = entry.isIntersecting && entry.intersectionRatio >= 0.12;
        reconcile();
      },
      { threshold: 0.12 },
    ).observe(video);
  } else {
    visible = true;
    reconcile();
  }
  syncControls();
})();
