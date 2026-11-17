/* Public link transfer only; no tracking, requests, auto-download or clipboard reads. */
(() => {
  const primary = document.querySelector("#download-primary");
  const input = document.querySelector("#download-link");
  const copy = document.querySelector("#copy-download");
  const status = document.querySelector("#download-status");
  const platform = document.querySelector("#platform-note");
  if (!primary || !input || !copy || !status || !platform) return;
  const agent = navigator.userAgent || "";
  if (/Android|iPhone|iPad|Macintosh|Linux/i.test(agent)) {
    platform.dataset.platform = "other";
    platform.textContent = "此包供 Windows 10 / 11 x64 电脑使用。可先复制链接，在 Windows 电脑上下载。";
  }
  primary.addEventListener("click", () => {
    status.textContent = "正在打开下载链接。若未开始，可用官网备用下载，或到 GitHub 发布页重试。";
  });
  copy.hidden = false;
  copy.addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText(input.value);
      status.textContent = "下载链接已复制，可发送给自己的 Windows 电脑。";
    } catch {
      input.focus();
      input.select();
      status.textContent = "请手动复制已选中的链接，也可直接打开 GitHub 发布页。";
    }
  });
})();
