# T解压 (TUnpack)

> Windows x64 原生轻量解压工具（**v1.0.0**）—— 不看后缀、只看内容，极速并行解压。

[官网与产品短片](https://tunpack.thomasthzh.top/) · [下载 v1.0.0 便携版](https://github.com/thomasthzh/TUnpack/releases/tag/v1.0.0) · [全部发布](https://github.com/thomasthzh/TUnpack/releases)

运行解压目录中的 `T解压.exe` 即可使用（单文件 **2.13 MB**），无需安装 `.NET`、`Python` 或 `WebView`。

---

## 核心特性

- **按内容识别，不信任扩展名**：原生支持 ZIP、7z、RAR、GZIP、BZIP2、XZ、TAR、Zstandard、LZ4、LZ5、Lizard；支持改后缀、视频/图片尾部追加、apate 伪装与多卷（`.z01…zip` / `.part1` / 编号改名分卷）自动归组，直接从只读视图解压。
- **双层并行加速**：单包多核负载均衡并行解压（ZIP 最高提速 **2.69×**）+ 多任务并行队列（2–4 并发），支持 CLI `--jobs N`。
- **智能解压与嵌套处理**：默认开启单根智能解压（Smart Extract，避免套娃文件夹），自动展开嵌套包并保护 DOCX/APK/EPUB 等正常容器；全部成功后可按需将原包移入回收站。
- **极速密码探测与本机密码本**：预计算 PBKDF2 中间态并行探测（较常规命令行提速最高 **160×**），自动读取文件名与同目录 `password.txt` / `密码.txt` 提示；密码本经 Windows DPAPI 本机加密保存，支持批量导入 UTF-8 文本。
- **现代双列工作台**：瑞士工业风暖陶界面 + 60fps 微动效；左侧自绘宽敞任务流（状态胶囊 + 行内进度条），右侧实时任务检查器与可折叠本批选项抽屉；工作台自动保存与恢复。
- **多格式压缩与过滤**：支持创建 ZIP / 7z / Zstandard（`.zst`）压缩包，支持 `--include` / `--exclude` 过滤与仅列清单（`list`）。

## 快捷操作

- **拖入即用**：将文件或文件夹拖入窗口任意位置，点击「开始解压」即可；运行中可继续添加任务。
- **常用快捷键**：`Ctrl+O` 添加文件 · `Ctrl+V` 粘贴文件 · `Ctrl+F` 查找筛选 · `Ctrl+,` 展开/收起本批选项 · `Ctrl+Enter` 开始队列 · 双击已完成项直达结果目录。

## 命令行（CLI）

```powershell
# 并行智能解压（默认不删源包）
.\unpack_cli.exe extract "D:\path\archive.zip" --smart --jobs 4 --keep

# 仅查看包内文件清单（不落盘）
.\unpack_cli.exe list "D:\path\archive.7z"

# 仅识别格式并全量校验正文（支持伪装包）
.\unpack_cli.exe inspect "D:\path\disguised.mp4" --verify --keep
```

## 从源码构建

需要 CMake 3.24+、Git、Python 3 与 C++20 工具链（推荐 WinLibs GCC / Ninja）：

```powershell
python scripts/fetch_deps.py --seven-zip "C:/Program Files/7-Zip/7z.exe"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
python scripts/package.py
```

打包产物输出至 `dist/TUnpack-1.0.0-windows-x64.zip` 与 `dist/SHA256SUMS.txt`。完整开发记录与测试证据见 `docs/ai/` 与 `docs/evidence/`。

## 许可与致谢

- 本项目以 **MIT** 许可开源，详见 [LICENSE](LICENSE)。
- 第三方组件（[bit7z](https://github.com/rikyoz/bit7z) MPL-2.0，含本地卷解析补丁 `patches/bit7z-volumes.patch`；[7-Zip-zstd](https://github.com/mcmilk/7-Zip-zstd)）保留各自许可，详见 [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt)。
