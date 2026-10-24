# 俄语、意大利语支持（2026-10-22）

字幕 OCR：在字幕范围预览的“字幕语言”选择中英、俄语或意大利语。设置中的“默认字幕语言”决定新任务的初始选择，开始识别后也会记住该任务所选语言。每个任务记录提交时的语言，后续修改设置不影响已排队任务。

俄语使用 `cyrillic_PP-OCRv5_rec_mobile.onnx`，意大利语使用 `latin_PP-OCRv5_rec_mobile.onnx`。模型来自 [RapidAI 官方清单](https://github.com/RapidAI/RapidOCR/blob/main/python/rapidocr/default_models.yaml)，固定于其 ModelScope v3.10.0 发布地址，逐文件验证官方 SHA-256。复用已有文本检测器、DirectML GPU、画面分段、中间帧识别及相似句合并。只加载本次所选模型，默认中英行为不变。两个新增识别模型合计约 15.2 MiB，不包含语音听写模型。模型文件与许可证在 `models/ocr/`。

歌词配时：设置和原文审阅步骤增加俄语 `ru`、意大利语 `it`。手动选择直接传递给现有多语言对齐模型。自动模式根据文字脚本判断中、日、韩、俄；拉丁字母文字则复用现有 Whisper 模型检测音频语言，避免把意大利语固定当英语。自动判断可能受前奏或短音频影响，可手动指定语言。没有恢复音频听写功能。

验证：

- 检查 OCR 模型字库，包含完整俄语大小写字母（含 Ё/ё）与意大利语重音字母。
- 两段自制原文字幕视频，每段六秒；俄语包含“Привет, мир!”和“Всё начинается с мечты.”，意大利语包含“È già una bella città.”和“L'amore è qui con noi.”。
- 四组实际 worker 测试：两种语言分别使用 CPU/GPU。识别字母与重音正确，标点大小写归一化后的文字与原文一致；每个视频 12 帧仅执行两次 OCR，起止时间均为 0–2 秒、3–5 秒，空白间隔保留。GPU 实际 provider 为 DirectML。
- 原生 Qt 测试验证设置保存、语言配置进入队列、结果归属、生成后暂不覆盖原歌词。完整版四组测试及 worker 十四项测试通过。
- 俄语真实语音使用 [Google FLEURS](https://huggingface.co/datasets/google/fleurs) `ru_ru / validation / row 0`，CC-BY-4.0。两句原文自动选为俄语并生成递增时间轴，原文保持一致。缓存中的来源记录保留完整转录与文件 SHA-256。
- 意大利语使用 Google Translate TTS 合成的两句原创测试文本，分别验证手动 `it` 和自动选择，均生成两行有效时间轴并保留 `città` 等原文。此处是链路验证，不代表任意演唱、方言或伴奏下的配时精度。

数据结果：`ocr-language-dependencies.json`、`ocr-russian-italian-validation.json`、`alignment-russian-italian-validation.json`、`test-results.xml`。测试字幕生成脚本：`tests/create_language_fixtures.py`；音频仅保留在开发缓存，不随应用分发。

准备模型：运行 `scripts/setup-ocr-languages.ps1`；完整歌词依赖准备脚本也会执行这一步。Release 随包提供模型；桌面 Debug 仍使用现有快捷方式，重新打开后选择字幕语言。
