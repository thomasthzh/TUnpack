# T解压官网

正式官网：**https://tunpack.thomasthzh.top/**。GitHub Pages是镜像，不能代替正式域名发布。页面无需框架、外部字体、统计脚本或账号。应用版本仍为v0.13.0，官网修订不等于软件版本升级。

## 原创矢量英雄动画

主片22秒、60fps，单一矢量时间轴，非软件录屏，节奏不代表实际耗时。用同一个旅途.mp4串起内容识别、ZIP里的改名分卷、已存密码验证、内层ZIP、输出与按设置回收。自动配卷仅展示可唯一匹配的例子；回收需已开启且没有后续任务依赖。

英雄镜头落在动作上：扩展名外表分开，ZIP内容以亮色满幅揭示；3.00–3.36秒快速推进，再缓退接入卷轨。后续镜头沿同一内容流移动；19.00–20.05秒拉回整条路径，品牌定格。字体、对象、内容流与镜头分别计时，说明字不会跟着特写放大出界。横版1600×900，竖版900×1200独立重排，不裁横片。

`scripts/render_site_film.py`是唯一动画实现，使用Skia路径、字体、裁切与变换，不使用Blender、bpy、浏览器录制或桌面操作。`scripts/encode_site_film.py`直接逐帧调用同一个render_frame函数导出H264/AAC，另存WebP和分享图；不依赖旧PNG帧序列。旧影片只保留历史URL供缓存兼容，不是并行制作实现。

制作用Python3.12、skia-python144.1、numpy、Pillow、imageio-ffmpeg。依赖只供制作，不随网页或软件分发。Windows字体只栅格化为画面，不分发字体；声音为原创合成，没有第三方歌曲或录音。

```text
python -m pip install skia-python==144.1 numpy Pillow imageio-ffmpeg
python scripts/render_site_film.py
python scripts/encode_site_film.py
```

`tests/hero_story_check.py`此前实际通过CLI/SDK解开同结构生成样本，输出字节正确；使用隔离DPAPI和公开测试密码、关闭回收、保留输入。本轮只替换动画与页面，没有重跑或扩大该引擎证据，不证明任意改名裸切片或破损文件均能恢复，也没有新增回收测试。

## 阅读与控制

网页标题在影片外，片中保留具体文件名和处理状态。首次挂载前按700px断点选择静帧、媒体与尺寸；减少动态或省流量只出静帧。加载后冻结构图与比例，旋转不重置源、进度或手动暂停。横竖布局随各自媒体自然流动，没有标题遮罩或滚动劫持。

原生控件、三章跳转、播放/暂停、声音、全屏、VTT、双版MP4下载与无JS静帧均保留。离屏/后台暂停，手动选择保留。资源有版本路径，旧媒体保留以兼容缓存页面。

下载链接指向GitHub七文件便携包与校验文件，并提供官网备用包、发布页和手动复制URL退路。不把下载点击说成下载完成；明确默认回收及可关闭、失败保留和用户目录状态。网站不读取剪贴板、用户压缩包或密码本。

## 发布与回滚

Pages工作流只上传site目录。正式域名由已有Caddy独立版本目录承载。

`python scripts/deploy_site.py --ssh-host <existing-alias> --phase inspect|stage|apply|rollback --release <name>`使用操作者已有SSH配置。stage需已验证的 `dist/TUnpack-0.13.0-windows-x64.zip`，只写新目录，保留历史资源与兼容下载URL。本轮apply只切指定host的文件root；检查现有CSP已允许self媒体，不修改策略。更新携带ETag/If-Match，保存精确旧route用于rollback；不改DNS、不重载全局配置、不删除旧目录。核对其他配置不变及autosave持久，最后验证正式域名资源字节、MP4范围206和下载包。

验证用 `node --test tests/site_player.test.cjs tests/site_download.test.cjs`、`python tests/check_site.py`，以及成片完整解码和抽帧检查。用户禁止CU/桌面/浏览器自动操作；静态检查、状态测试不等于浏览器实屏或用户审美验收。
