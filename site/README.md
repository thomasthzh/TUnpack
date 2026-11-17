# T解压官网

正式官网：**https://tunpack.thomasthzh.top/** 。GitHub Pages只是同源镜像，不代替正式域名发布。页面与下载不需要框架、外部字体、统计脚本、账号或运行时库。应用版本仍为v0.13.0，官网修订不等于软件版本升级。

## 原创英雄视频

当前主片是12秒原创离线3D品牌概念影片，非实际软件录屏。根据用户指定的藏锋设计方法复核主体、布光、景别、节奏与阅读交接；私有原始资料、会话和凭据不随仓库分发。此前二维文件牌/密码表影片只保留旧URL供缓存兼容，不再作为英雄片，也没有得到用户审美认可。

`render_site_film.py`是唯一主片渲染实现：Blender5.3 / Cycles / OptiX，实体T与三层壳体网格、六层内容薄片、PBR金属材质、方向面光和真实透视相机。每版288帧、24fps。横版渲染1440×900，交付时左留480px暗部成为1920×900供网页标题；竖版独立摄影机取景864×1080，不裁切横片。

镜头：0–3.2秒拉链/金属局部掠光，3.2秒切到完整主体，3.75秒起结构分离，8.2秒起绕行看另一侧，最后落到完整形体。没有主片表格、卖点卡或大量常驻字幕。声音为原创合成，不使用第三方歌曲、素材或录音。

本机开发可用Python3.13安装bpy5.3，或在对应Blender的background模式执行：

```text
python scripts/render_site_film.py --format wide
python scripts/render_site_film.py --format portrait
python scripts/encode_site_film.py
```

编码器使用numpy/Pillow/imageio-ffmpeg合成原创音轨、导出H264/AAC与WebP静帧，完整解码核对。字体只用于分享图的栅格化，不分发字体。浏览器仅播放视频，不下载Blender、模型或渲染器。

## 阅读与控制

宏观细节镜头时画面主导，主体揭示后标题进入；暂停、出错和结束恢复标题。首次挂载前按700px断点选择媒体/静帧/尺寸；减少动态或省流量只出静帧。加载后构图、比例与标题布局一并冻结，旋转不重置源、进度或暂停选择。窄屏和中宽屏重新排成上下关系，低高度宽屏不锁滚动。

原生控件、章节跳转、播放/暂停、声音、全屏、VTT、双版MP4下载与无JS静帧都保留。离屏/后台暂停，手动暂停保留；没有滚动劫持。源文件/资源有版本路径，旧媒体保留，不影响缓存页面。

下载固定链接到GitHub已发布的七文件便携包与校验文件，另有备用发布页和手工复制URL退路；不伪报点击就是下载成功。明确每个任务及嵌套成功后的默认回收、可关闭和用户目录状态。此网站不读取剪贴板、用户压缩包、密码本或操作系统凭据。

## 发布与回滚

GitHub Pages工作流只上传site目录。正式域名由已有Caddy独立版本目录承载，不能仅发布Pages后声称正式官网更新。

`deploy_site.py --ssh-host <existing-alias> --phase inspect|stage|apply|rollback --release <name>`使用操作者已有SSH配置。stage需要已验证的 `dist/TUnpack-0.13.0-windows-x64.zip`，只写新目录并保留历史资源/兼容下载URL；apply只PATCH指定host的route，更新root和media-src，携带ETag/If-Match避免并发覆盖。保存精确旧route用于rollback，不改DNS、不重载全局配置、不删除旧目录。核对其他配置不变及autosave持久，最后必须核验正式域名的视频206、CSP及下载。

验证用 `node --test tests/site_player.test.cjs tests/site_download.test.cjs` 和 `python tests/check_site.py`，再检查真实成片。用户禁止CU/桌面/浏览器自动操作，因此本轮没有浏览器实屏验收；静态检查、状态测试和模型自评都不冒充用户审美认可。
