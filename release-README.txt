T解压 0.13.0 — Windows x64 便携版

开始使用
1. 将 ZIP 完整展开到普通文件夹，保留全部文件。
2. 运行 T解压.exe。无需安装 .NET、Python 或 WebView。
3. 拖入文件/文件夹，检查输出目录、密码和处理选项，点击“开始全部”。

默认行为
- 原包旁创建同名结果目录；也可指定输出。重名使用新名称，不覆盖已有内容。
- 自动处理嵌套包，按内容识别格式及受支持的伪装方式。
- 成功后按设置将原包移入回收站；开始前可取消“成功后回收原包”。
- 失败保留原包与已解结果。缺卷可补齐顺序，内层失败可单独准备重试。
- 内层单独成功不会将整个父任务标为全部完成，也不自动接续其他内层包。

密码与工作台
本批输入和密码本可供自动尝试；不进行无边界暴力破解。
密码本用 Windows DPAPI 加密，通常与当前 Windows 用户/机器绑定。
队列、非敏感设置与结果记录在 %LOCALAPPDATA%\T解压 中。
因此“便携”表示无需安装，不表示所有用户状态都写在程序旁。
工作台恢复后不会自动开始，旧的运行项需要显式重试。

版本与下载
仅支持 Windows 10/11 x64；本版本尚未数字签名。
请从官方 GitHub Release 下载，可用同页 SHA256SUMS.txt 核对文件。
官网：https://thomasthzh.github.io/TUnpack/
发布：https://github.com/thomasthzh/TUnpack/releases/tag/v0.13.0
说明：https://github.com/thomasthzh/TUnpack#readme
反馈：https://github.com/thomasthzh/TUnpack/issues

范围
格式与伪装恢复取决于内容和完整性，并非所有 MP4 或任意损坏文件都能解开。
本版真实SDK、生成样本与原生布局回归已验；实屏手感、跨屏DPI及Explorer实际选择仍待人工验收。

许可
T解压主体 MIT，见 LICENSE.txt。
第三方组件与修改保留各自许可；完整通知及对应源码见 THIRD-PARTY-NOTICES.txt、
7-Zip-License.txt、bit7z-LICENSE.txt。
