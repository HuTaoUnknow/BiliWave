# 音频输出与耳机切换

通用设置增加“音频输出”和“重新检测”。两种版本共用同一套输出逻辑。

- 默认选择“跟随系统默认”，Windows 默认输出改变或插拔耳机时，使用更新后的系统输出设备。
- 也可从当前检测到的耳机、音箱、HDMI 等设备中手动选择；选择立即保存，以设备 ID 区分同名设备。
- 手动设备断开时暂时退回系统默认输出，保留原选择；重新连接同一设备后自动恢复。没有输出设备时显示提示，接入后自动恢复。
- “重新检测”刷新设备列表，并重新连接当前音频输出，方便处理驱动或蓝牙状态恢复后仍无声的情况。
- 切换输出不重置歌曲、播放队列、进度、基础音量、自动消声音量或静音状态；暂停时切换仍保持暂停。
- 下拉框关闭时滚轮不会改动所选设备。

原实现只在启动时创建 `QAudioOutput`，没有监听设备和系统默认输出的变化。现在监听 `QMediaDevices::audioOutputsChanged`，重新解析默认或手动输出并调用 `QAudioOutput::setDevice`。手动重新检测只重新绑定音频输出，不重新设置媒体来源。

接口依据：[Qt QMediaDevices](https://doc.qt.io/qt-6/qmediadevices.html)、[Qt QAudioOutput](https://doc.qt.io/qt-6/qaudiooutput.html)。

设置键：`audio/outputDevice`（Base64 编码设备 ID，空值表示系统默认）、`audio/outputDeviceName`（仅用于断开提示）。
