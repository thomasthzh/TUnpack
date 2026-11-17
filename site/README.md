# T解压官网

正式官网：**https://tunpack.thomasthzh.top/**。GitHub Pages是镜像，不能代替正式域名发布。页面无需框架、外部字体、统计脚本或账号。应用版本仍为v0.13.0，官网修订不等于软件版本升级。

## 原创产品故事

主片为20秒原创3D功能演绎，非软件录屏，节奏不代表实际耗时。用同一个文件串起因果：旅途.mp4显露ZIP内容→ZIP内的三卷（末卷改名附件.docx）归组→从已存候选验证密码再开锁→这组7Z继续解出内层ZIP和文件→全部校验成功后按设置回收原MP4。自动配卷仅展示可唯一匹配的例子；回收还需设置已开启且没有后续任务依赖。

`tests/hero_story_check.py`用生成的MP4前缀ZIP、加密7z三卷/末卷改名及内层ZIP实际经过CLI/SDK解压，核对输出字节。使用隔离DPAPI书和公开测试密码，回收关闭，全部输入保留。它不证明任意改名裸切片或破损文件都能恢复，也不是新的回收测试。

`scripts/render_site_film.py`是唯一主片渲染器：Blender5.3/Cycles/OptiX、原创物理文件/钥匙/锁/回收站模型、独立灯光与摄影机。每版480帧序列、24fps；完全相同的静止姿态复用已渲染像素，渲染统计如实记录。横片源1152×720，合成1536×720；竖片独立取景720×900，合成720×1100，保留上方文字区，不裁横片。旧二维与抽象3D媒体只保留历史URL。

渲染需Python3.13+bpy5.3，或对应Blender的background环境。编码另用装有numpy/Pillow/imageio-ffmpeg的Python环境，不要把cp313依赖通过PYTHONPATH注入其他版本Python。

```text
python scripts/render_site_film.py --format wide
python scripts/render_site_film.py --format portrait
python scripts/encode_site_film.py
```

编码器把动作说明直接合成进两版影片，再导出H264/AAC、WebP和分享图。配乐为原创合成，没有第三方歌曲、视频或录音。Windows字体仅用于模型文字和像素合成，不分发字体。浏览器只播放影片，不下载模型或渲染环境。

## 阅读与控制

网页标题在影片外，片中保留具体文件名和处理状态。首次挂载前按700px断点选择静帧、媒体与尺寸；减少动态或省流量只出静帧。加载后冻结构图与比例，旋转不重置源、进度或手动暂停。横竖布局随各自媒体自然流动，没有标题遮罩或滚动劫持。

原生控件、三章跳转、播放/暂停、声音、全屏、VTT、双版MP4下载与无JS静帧均保留。离屏/后台暂停，手动选择保留。资源有版本路径，旧媒体保留以兼容缓存页面。

下载链接指向GitHub七文件便携包与校验文件，并提供官网备用包、发布页和手动复制URL退路。不把下载点击说成下载完成；明确默认回收及可关闭、失败保留和用户目录状态。网站不读取剪贴板、用户压缩包或密码本。

## 发布与回滚

Pages工作流只上传site目录。正式域名由已有Caddy独立版本目录承载。

`python scripts/deploy_site.py --ssh-host <existing-alias> --phase inspect|stage|apply|rollback --release <name>`使用操作者已有SSH配置。stage需已验证的 `dist/TUnpack-0.13.0-windows-x64.zip`，只写新目录，保留历史资源与兼容下载URL。本轮apply只切指定host的文件root；检查现有CSP已允许self媒体，不修改策略。更新携带ETag/If-Match，保存精确旧route用于rollback；不改DNS、不重载全局配置、不删除旧目录。核对其他配置不变及autosave持久，最后验证正式域名资源字节、MP4范围206和下载包。

验证用 `node --test tests/site_player.test.cjs tests/site_download.test.cjs`、`python tests/check_site.py`，以及成片完整解码和抽帧检查。用户禁止CU/桌面/浏览器自动操作；静态检查、状态测试不等于浏览器实屏或用户审美验收。
