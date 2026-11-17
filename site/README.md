# T解压官网

独立静态站，无框架、外部字体、统计脚本或运行时依赖。相对资源路径兼容 GitHub Pages 项目子路径。公开地址为 https://thomasthzh.github.io/TUnpack/ 。

参考本机 SyncAction 公开官网的黑底留白、单一影片及“广角 → 特写 → 拉回”节奏，原创 T解压文件、密码和分卷视觉；未复制其山景、音乐、品牌或私密资料。影片明确标注功能演绎，不是实际软件录屏。

`scripts/render_site_film.py` 用 Pillow / numpy 离线按时间渲染 1280×720、30fps、24秒影片，并合成原创器乐音轨；Windows字体只用于栅格化，不随包分发。需要 `pip install Pillow numpy imageio-ffmpeg`，然后 `python scripts/render_site_film.py`。源码与成片一起维护，不在网页运行渲染器。

首页采用原生视频控件。页面控制支持播放/暂停、静音/声音、全屏与章节定位。离屏/后台暂停，手动暂停会保留；减少动态或省流量时不挂载视频/字幕，先显示小体积 WebP 静帧，主动播放才加载。无JS有MP4、字幕、下载和使用说明。没有滚动劫持。章节按钮在无JS时禁用，避免无效操作。

验证：`node --test tests/site_player.test.cjs` 检查媒体状态流；`python tests/check_site.py` 检查公开链接/相对路径/资源/语义结构。离线生成的分镜与成片帧可直接审查，FFmpeg完整解码检查音视频。遵照本项目禁止CU/浏览器自动操作的约束，未运行浏览器，不将静态检查称为真实浏览器或手机验收。

`.github/workflows/site.yml` 只上传 `site/` 为Pages，不包含源码、测试输入、构建目录或密码本。原下载直接指向固定版本GitHub Release，不需要外部API。官网可通过恢复之前站点提交并重新运行同一Pages工作流回滚；本轮新增Release，不覆盖其他版本资产。
