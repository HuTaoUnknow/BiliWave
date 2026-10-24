# Lite 1.8.0 安装包验收（2026-10-22）

安装包：`BiliWave-Lite-1.8.0-Windows-x64-Setup.exe`，105,042,057 字节（100.18 MiB），比原完整版 1,156,067,497 字节缩小约 90.9%。安装内容约 308.9 MiB，实际使用时另计媒体和用户数据。

SHA-256：`fe228491274e3276c276d67f17582331c99538d7ad28deff8049fb944a7837e3`。

- Lite 的 core、lite 两组测试通过，覆盖禁用旧配置中的自动歌词、隐藏歌词入口、歌曲选择不暂存、URL 直接入库、歌单收录、本地导入、实际音频解码与跳转。
- 共享代码的完整版四组回归测试全部通过；实时在线歌词检索在该测试中按原设置跳过。
- 在含空格的独立路径执行中文静默安装，验证主程序哈希及模型、worker、Python 和 Whisper 文件不存在。
- 使用仅含 Windows 系统目录的 PATH 启动已安装程序，实际导入并播放 FLAC、跳转后退出。卸载移除程序并保留用户数据。
- 检查四个页面截图，搜索使用单列结果区域，歌词工作台、识别队列、桌面歌词与歌词设置入口均不存在。

机器可读结果见 `lite-installer-validation.json` 和 `lite-test-results.xml`，截图在 `screenshots/lite/`。本轮未重复实测在线下载；搜索、解析和原轨下载沿用原实现，网络账号与音轨权限仍取决于哔哩哔哩。

构建、打包、安装验证分别使用 `scripts/build-lite.ps1`、`scripts/package-installer.ps1 -Lite`、`scripts/verify-lite-installer.ps1`。
