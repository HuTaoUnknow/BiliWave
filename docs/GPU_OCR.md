# 字幕分段识别与 GPU 加速（2026-10-22）

当前机器为 NVIDIA GeForce RTX 3060 Laptop GPU，6 GB。项目内新增 `tools/ocr-gpu/`，使用 ONNX Runtime DirectML 1.24.4；已有 CPU Python、PyTorch 和音频对齐模型保持独立。GPU 加速用于字幕 OCR，已有歌词的音频对齐仍使用原 CPU 环境。

## 流程

1. FFmpeg 流式读取视频，只把选定字幕区域的采样画面送入处理器，默认间隔 0.5 秒。
2. 缩小画面并提取轮廓，比较字形变化，容忍轻微压缩噪声和颜色变化。与当前段的初始画面变化达到阈值才划分新段，避免只比较相邻帧导致缓慢漂移。
3. 每段只选择时间中间的一帧做 OCR；纯空白区域跳过。图像采用有内存上限的临时缓冲，长时间静态字幕不会无限占用内存。
4. 相邻识别文字相似时继续合并，采用合并段中间位置所属的字幕识别结果；保留整个段的起止时间。空白间隔会断开，重复副歌不会跨间隔合并。相似度与组内原始文字比较，避免连续近似变化最终把不同内容全串在一起。
5. 生成带空白清除点的 LRC，回到原有查看、编辑与应用流程。进度显示实际后端、已比较帧数和 OCR 次数。

检测器改为限制最大边长，避免把狭窄字幕条放大为巨大检测图。横向字幕跳过方向分类推理。图片预览、原视频和歌曲文件不改写。

## 设置

- 设置 → 字幕识别计算：自动（优先 GPU）/ GPU / CPU。自动模式在 GPU 不可用时提示并切回 CPU；强制 GPU 失败会报告错误，不伪称已经加速。
- 字幕预览 → 画面变化阈值：默认 22%，越低越容易划分新段。
- 字幕预览 → 文字合并相似度：默认 94%，可设为 100% 只合并规范化后相同的文字。
- 截取范围尽量紧贴字幕。动态背景也可能触发画面变化，放大范围会增加 OCR 次数；低对比文字和复杂特效仍需试听检查。

初始化会枚举 DXGI 显卡，选择有独立显存的适配器，并记录名称。检测、分类和识别会话均验证 GPU provider 实际成功创建；不会只根据安装包名字判断。DirectML 会话按[官方要求](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html#configuration-options)禁用 memory pattern 和并行执行。RapidOCR 1.4.4 所需的适配仅作用于当前 worker 进程，不修改第三方源码。

## 验证结果

30 秒受控字幕样片（60 个采样帧，两段各持续 10 秒的中文字幕，中间保留空白）：

| 流程 | OCR 次数 | 进程总耗时 |
|---|---:|---:|
| 原逐帧 CPU 流程 | 60 | 56.194 秒 |
| 新分段流程、CPU | 2 | 1.164 秒 |
| 新分段流程、GPU | 2 | 3.154 秒 |

三者的文字及起止时间一致。相对旧流程，当前 GPU 方案约快 17.82 倍；这是算法与检测尺寸调整、GPU 的综合效果，不能当作 GPU 单独加速倍数。短样片中 GPU 初始化、首次图编译开销较明显，新 CPU 流程反而更快；可按实际任务在设置中切换。

另对已有缓存视频的 25–55 秒动态背景片段实测，紧贴字幕截取：60 帧分成 26 段，合并为 10 句，GPU 进程总耗时 8.605 秒。该样本验证处理链路与时间顺序，未量化逐字正确率。

ONNX profiling 记录了 DirectML 节点执行，证明 GPU provider 实际参与计算；部分形状等操作仍在 CPU。原生 Qt 四组测试通过，worker 十二项测试覆盖中间帧选择、文字合并、空白、副歌、颜色变化、不同句子与强制 GPU 拒绝 CPU-only 环境。Release 中独立 Python 在只有 Windows 系统 PATH 的环境下完成实际 GPU OCR；Debug 新程序启动成功。正在运行的旧 Debug 会话保留，重新打开桌面快捷方式使用新界面。

证据：`ocr-gpu-benchmark.json`、`ocr-real-gpu-validation.json`、`ocr-gpu-portable-validation.json`、`ocr-gpu-dependencies.json`、`test-results.xml`。

重新准备 GPU 组件：`scripts/setup-ocr-gpu.ps1`。下载固定 wheel 并验证 SHA-256，安装仅写项目目录。`setup-lyric-tools.ps1 -CpuOnly` 可跳过 GPU 组件下载；已安装机器可直接用设置选择 CPU。当前 wheel 为官方 PyPI 的 Windows CPython 3.11 x64 包，25.1 MB，解压后约 71.2 MiB，保留许可证。没有安装整套 CUDA 或修改显卡驱动。
