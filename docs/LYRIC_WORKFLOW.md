# 1.2 歌词流程交付记录

入口：搜索 → 歌词工作台 → 选歌曲 → “导入 / 视频 / 在线歌词 → 匹配时间”。

- 文件导入与粘贴：UTF-8 TXT、LRC、SRT；检查文字后匹配音频。LRC 可选保留时间标签直接使用。
- 视频字幕：本地视频或 Bilibili HTTPS 链接，保留 `?p=` 当前分 P。第一步设置范围并预览，默认只识别下三分之一。RapidOCR 使用选中区域像素；提取结果先进入文字编辑，不自动覆盖歌曲歌词。
- 在线歌词：LRCLIB 同时检索纯文本和同步歌词。候选文字可编辑，再用当前音频重新对齐。
- 时间匹配：stable-ts `model.align`，CPU、Whisper small、`original_split=True`，输出时使用原始文字与标点，按行保存 LRC。可疑行提示检查。
- 每个流程均在应用前预览；取消或失败保持原歌词。应用新的时间轴时清零旧时间偏移。

## 验证

2026-10-21，本机 Qt 6.11.1 / MinGW 13.1：

1. 四组 CTest 全部通过，新增测试涵盖导入审阅、延后写入、取消、默认范围、实际 Python 子进程对齐。
2. 真实 10.64 秒 JFK 音频，输入三行指定英文原文；成功生成三行时间戳，原文与标点逐行相同。`lyric-alignment-result.json`。
3. 6 秒中文字幕测试视频，画面上方另有 `TOP OUTSIDE TEXT`。仅下三分之一执行 OCR，输出“听见微光 / 落在窗前”，未包含上方文字。`lyric-ocr-result.json`、`screenshots/lyric-crop-verified.png`。
4. 四项 Python 数据行为测试：数字歌词不丢失、重复副歌不去重、越界范围拒绝、取消检查。
5. 新在线检索界面实测 Yellow / Coldplay，成功选取候选并进入原文编辑步骤，尚未应用时数据库保持不变；`lyric-online-live-results.txt`。
6. Qt 对话框中实际预览范围并提取受控中文视频，成功进入文字检查步骤；`lyric-video-ui-results.txt`。默认测试套件的联网检查按需启用，已单独联网执行通过。
7. 真实在线视频 `BV1bK411W797?p=2` 下载与 10 秒处范围预览成功；`lyric-bilibili-preview-result.json`。
8. 发布包独立 Python / 模型 / FFmpeg 在 PATH 仅含 Windows 系统目录时完成对齐；`lyric-portable-result.json`。新版 Debug 同环境启动及截图退出码为 0，桌面快捷方式路径不变。

截图：`screenshots/lyric-video-workflow.png`、`screenshots/lyric-text-review.png`。

说明：语音对齐样本与控制变量的中文视频用于验证工程链路及区域隔离，不代表任意音乐、方言、渐变字幕都能达到同样精度。在线资源覆盖率依赖 LRCLIB，视频下载仍受平台及账号可访问范围限制。
