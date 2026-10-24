# 自动消声

更新时间：2026-10-24。

设置 → 通用设置 → 自动消声。默认关闭；勾选“自动降低音乐音量”后生效。

- 触发条件可分别勾选“麦克风检测到说话”“其他应用正在使用麦克风”“其他应用正在发声”，任一满足就压低 BiliWave 音乐。默认三个条件都选中，总开关默认关闭。
- 降低幅度为 0–100%，默认 70%。例如手动音量 80%，降低 70% 后实际输出为 24%；100% 为静音。
- 触发后约 180 ms 平滑降低，解除后约 650 ms 恢复。说话条件保留 1.2 s 的尾音等待，避免句子中间反复恢复。
- 其他应用发声条件保留 800 ms 的安静等待，再恢复音量，避免短停顿时反复起落。
- 音量滑块始终表示用户设定音量。压低期间调整滑块，恢复时使用调整后的音量；保存和下次启动也使用用户音量。连续触发不会层层相乘。
- 最小化到托盘后继续工作。关闭功能停止麦克风读取和状态检测，并恢复用户音量；明确退出程序立即释放检测资源。

## 检测方式和边界

说话条件使用系统默认输入设备。Qt `QAudioSource` 只在功能和说话条件都开启时读取 PCM，按 20 ms 窗口计算 RMS，约 −38 dBFS（0.0126）以上持续至少 120 ms 后触发。音频只用于内存中的音量计算，不写入文件、不发送网络、不调用语音识别模型。此方式为音量检测，较响的环境声音或扬声器漏入麦克风的音乐也可能触发，并非语义上的人声识别。更换默认麦克风或输入设备列表变化后重新连接。

开麦条件在独立 Windows COM 线程每 500 ms 枚举全部活动录音端点的 `IAudioSessionManager2` 会话，判断 `AudioSessionStateActive`，并通过 `IAudioSessionControl2::GetProcessId` 排除当前 BiliWave 进程。只查询使用状态，不录制其他应用。应用内部静音但仍持有活动录音流时仍触发；无法得知所有应用内部的按键说话状态。无权限、设备缺失或被独占时在本页显示提示，可用的另一条件仍继续工作。

其他应用发声条件在独立 COM 线程每 100 ms 枚举全部活动播放端点上的会话，排除当前 BiliWave 进程后读取每个会话 `IAudioMeterInformation::GetPeakValue`，峰值大于 0.001（约 −60 dBFS）才视为发声。同时检查播放端点和会话的静音、零音量；只占有播放流但输出全零的应用不触发。浏览器、视频、游戏等使用同一规则。只读取音量数值，不采集、保存或上传其他应用音频；BiliWave 自己的音乐和试听均被排除。极短于采样周期的提示音可能漏检，音频接口不支持时本页显示错误。

本机 MinGW 13.1 的头文件仅前向声明 `IAudioMeterInformation`，工程按 Microsoft WinSDK 的标准 COM ABI 补充受头文件保护的接口声明，不增加动态库依赖。

设置存储在当前资料库 settings.ini：

| 键 | 默认 |
|---|---|
| `audioDucking/enabled` | false |
| `audioDucking/onSpeech` | true |
| `audioDucking/onMicrophone` | true |
| `audioDucking/onOtherAudio` | true |
| `audioDucking/reduction` | 70 |

实现：`src/autoducking.cpp`、`src/player.cpp`、`src/mainwindow.cpp`。无需新增模型或第三方运行时。

## 验证

`ducking_tests` 检查短脉冲过滤、持续声音触发、尾音保持、三个条件的 128 种组合、重复衰减、期间调整音量、平滑恢复、发声条件的安静保持及关闭后的回调忽略。Windows 实机测试短暂打开并丢弃本进程和自建辅助进程的麦克风输入，检查自身排除、其他进程开麦触发和辅助进程正常退出后的解除；另用低音量合成音轨检查自身播放不触发、辅助进程发声触发、全零音轨及静音不触发。有其他应用已经占用待测设备、设备缺失或权限受限时明确跳过相应硬件测试。麦克风辅助进程最长运行十秒，播放辅助进程最长运行二十秒，不保存录音。

`redesign_tests` 检查通用设置保存恢复、音量滑块不随临时衰减变化、托盘生命周期以及两套主题。结果和构建哈希见 `auto-ducking-validation.json`。

接口参考：[Qt QAudioSource](https://doc.qt.io/qt-6/qaudiosource.html)、[Qt QAudioFormat](https://doc.qt.io/qt-6/qaudioformat.html)、[Microsoft 音频会话进程 ID](https://learn.microsoft.com/en-us/windows/win32/api/audiopolicy/nf-audiopolicy-iaudiosessioncontrol2-getprocessid)、[Microsoft 音量峰值接口](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nn-endpointvolume-iaudiometerinformation)、[Microsoft WinSDK 接口声明](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/endpointvolume.h)。
