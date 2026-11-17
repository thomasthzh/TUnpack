# T解压官网

正式官网：**https://tunpack.thomasthzh.top/**。GitHub Pages是镜像，不能代替正式域名发布。页面无需框架、外部字体、统计脚本或账号。应用版本仍为v0.13.1，官网修订不等于软件版本升级。

## 图标英雄动画

当前主片28秒、60fps，以文件、压缩包、密码本、钥匙和结果图标作为主体。参考本地SyncAction的实际v6影片：动作发生在同一工作面，指针点击后有对应反馈，快速推进后缓退，最终收束品牌。图形为功能演绎，不是软件录屏，片中节奏不代表实际耗时。

同一旅途.mp4被拖入队列，停在待开始，再点击开始全部；原文件名始终保留，内容识别为ZIP。源预览退回队列，ZIP留在工作面放出三卷；改名DOCX在可唯一匹配后归为卷003。卷组收成同一个带后层的7Z主体，移入密码区，先验证后开锁，再连续移到左侧让出内层ZIP。已展开的三个文件保持身份，直接移到结果区和片尾右侧，避免消失后重生。

内层ZIP在焦点放大后从前景掠过，后方文件沿同一来源出现，镜头缓退看到完整结果。全部成功且回收已开启、无后续依赖时，原包才回收；指针点击输出目录后展开结果视图。点击、落入、验证和前景掠过的原创合成音效与同一时钟对齐，片尾才收束品牌与格式关系。

`scripts/render_site_film.py`仍是唯一制作源。同一package_pose和output_pose跨章节持续求值，不用整场交叉淡化重置主角；文字与容器分别交接。SVG图标增加短下缘、方向性渐变与边缘亮度，T标保持原生资产。队列行与源身份固定，内容镜头单独推进。横1600×900、竖900×1200独立构图，不裁横片。旧媒体仅保留历史URL，不是并行制作实现。

`brand/film-icons`保留Microsoft Fluent UI System Icons的17个24 Filled原SVG、MIT许可与固定上游commit；统一用白色字形和渐变底座。T标仍为本项目原生图标。影片直接绘制SVG矢量路径，不使用插画位图、Blender、GUI或浏览器录制；网站附图标许可。编码器用有界四进程批次逐帧调用同一个render_frame函数，直接输出H264/AAC与静帧。声音为原创合成，无第三方歌曲或录音。

制作环境：Python3.12、skia-python144.1、numpy、Pillow、imageio-ffmpeg；这些都不随软件或网页分发。Windows字体只栅格化为画面。

```text
python -m pip install skia-python==144.1 numpy Pillow imageio-ffmpeg
python scripts/render_site_film.py
python scripts/encode_site_film.py
```

此前 `tests/hero_story_check.py` 已实际验证ZIP→加密7z三卷/末卷改名→内层ZIP的结构，保留输入、关闭回收，使用隔离DPAPI和公开测试密码。本轮未改引擎、未重跑或扩大该证据；影片图标不是样本文件截图，也不证明任意破损/任意改名裸切片均能恢复。

## 阅读与控制

网页标题在影片外，片中保留具体文件名和处理状态。首次挂载前按700px断点选择静帧、媒体与尺寸；减少动态或省流量只出静帧。加载后冻结构图与比例，旋转不重置源、进度或手动暂停。横竖布局随各自媒体自然流动，没有标题遮罩或滚动劫持。

原生控件、三章跳转、播放/暂停、声音、全屏、VTT、双版MP4下载与无JS静帧均保留。离屏/后台暂停，手动选择保留。资源有版本路径，旧媒体保留以兼容缓存页面。

下载链接指向GitHub七文件便携包与校验文件，并提供官网备用包、发布页和手动复制URL退路。不把下载点击说成下载完成；明确默认回收及可关闭、失败保留和用户目录状态。网站不读取剪贴板、用户压缩包或密码本。

## 发布与回滚

Pages工作流只上传site目录。正式域名由已有Caddy独立版本目录承载。

`python scripts/deploy_site.py --ssh-host <existing-alias> --phase inspect|stage|apply|rollback --release <name>`使用操作者已有SSH配置。stage需已验证的 `dist/TUnpack-0.13.1-windows-x64.zip`，只写新目录，保留历史资源与兼容下载URL。本轮apply只切指定host的文件root；检查现有CSP已允许self媒体，不修改策略。更新携带ETag/If-Match，保存精确旧route用于rollback；不改DNS、不重载全局配置、不删除旧目录。核对其他配置不变及autosave持久，最后验证正式域名资源字节、MP4范围206和下载包。

验证用 `node --test tests/site_player.test.cjs tests/site_download.test.cjs`、`python tests/check_site.py`，以及成片完整解码和抽帧检查。用户禁止CU/桌面/浏览器自动操作；静态检查、状态测试不等于浏览器实屏或用户审美验收。
