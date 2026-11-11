# BiliWave 优化审查报告

审查对象：`main` @ `fb01521`（Refactor UI and add themes, keyboard controls and install choices）。本次只做分析，没有修改产品源码。

审查范围：README、PROJECT_SPEC、CHANGELOG、CMake / Presets、`src/`、`workers/`、`tests/`、`scripts/`、`packaging/`、`.gitignore`、`.gitattributes`，以及第三方声明。云端环境是 Ubuntu 24.04，没有 Windows、MinGW 和 Qt 6.11。能编译和能跑的测试已经跑过，跑不了的原因写在第 8 节。

## 结论摘要

工程分层（`bili_core` → `bili_services` → `bili_ui`）是真实的，歌词限流、字幕代表帧 OCR、音轨 stream copy、SQLite 参数绑定和本机流代理的主机限制都做得比较仔细。当前最值得先做的是：

1. 哔哩哔哩搜索没有 WBI 签名，封面和在线音流对返回 URL 的主机约束偏松。
2. 安装包里的 yt-dlp 没有应用内更新通道，B 站解析器过期后下载和 URL 播放会一起失效。
3. `src/features.h` 会遮蔽 C 库的 `<features.h>`，非 MinGW 工具链无法编译；仓库也没有 CI。
4. 启动和整单入队仍会在界面线程上读完整歌词、做简繁转换，并在打开库时逐文件取大小。
5. `.gitignore` 盖住了用户提到的大部分本机构建目录，但没有盖住任意 `release*`，也没有盖住整个 `models/`。换行规则方向正确，本地 `tests/*.cpp` 的 CRLF 差异不是索引里存了 CRLF。

下文每条建议都包含问题、证据、影响、方案、优先级和工作量。工作量按改动面估计：**小**是局部修改，**中**是跨几个模块，**大**是发布流程或结构拆分。

## 1. 项目概览

BiliWave 是面向 Windows 的 Qt Widgets 音乐库。播放用 Qt Multimedia，资料库是 SQLite（WAL），设置是 `QSettings`。哔哩哔哩解析和下载交给独立进程里的 yt-dlp，封装检查交给 FFmpeg / ffprobe。字幕识别在加装版里由独立 Python 进程完成，界面只收 JSON 进度。

### 模块

| 目标 | 内容 | 依赖 |
| --- | --- | --- |
| `bili_core` | 资料库、LRC、播放器、自动消声、OpenCC 简繁词典 | Qt Widgets / Multimedia / Sql |
| `bili_services` | 导入下载、在线歌词、搜索、URL 流转发 | `bili_core` + Qt Network |
| `bili_ui`（OBJECT） | 主窗口各页、主题、托盘、字幕向导 | `bili_services` |
| `BiliWave` | `src/main.cpp`，单实例、托盘启动、冒烟参数 | `bili_ui` |

编译开关在 `src/features.h`：

- `BILI_ENABLE_LYRICS`：歌词工作台和在线匹配。当前加装版、简装版都打开。
- `BILI_ENABLE_RECOGNITION`：字幕 OCR、歌词导入、Python worker。加装版开，简装版关。
- `BILI_ENABLE_ALIGNMENT`：旧的音频强制对齐。CMake 默认关闭，两个发行版都不打包 Whisper / PyTorch。

`MainWindow` 仍是一个类。页面文件（`librarypage.cpp`、`searchpage.cpp`、`workbenchpage.cpp`、`settingspage.cpp`、`playbackpage.cpp`）实现的是 `MainWindow::` 成员函数，控件指针集中在 `src/window.h`。这比 2912 行的单文件好维护，但页面之间不能单独构造，测试必须拉起整个窗口。

### 版本差异

公开安装包以 GitHub Release 为准。源码里的当前形态是：

| | 加装版（screen） | 简装版（Lite） |
| --- | --- | --- |
| 构建脚本 | `scripts/build.ps1` → `build/`，可选 `deploy-screen.ps1` → `release-screen/` | `scripts/build-lite.ps1` → `build-lite/`、`release-lite/` |
| 识别 | OCR、区域框选、GPU、双语、当前帧校验 | 关闭 `BILI_ENABLE_RECOGNITION` |
| 在线歌词 | 塞壬、网易云、QQ、LRCLIB，酷狗和 Lyrics.ovh 默认备用 | 相同 |
| 安装身份 | Inno AppId 与「BiliWave」 | 独立 GUID 与「BiliWave Lite」 |
| 体积（1.12.0 已发布记录） | 安装包 211.63 MiB，装后约 667 MiB | 安装包 100.66 MiB，装后约 310 MiB |

CMake 工程版本仍是 `1.12.0`，CHANGELOG 在该版本之后还有一节「未发布」。若现在打包，未发布的主题、安装选择和快捷键会带着旧版本号出去。`packaging/README.md` 仍写打包来源是 `release/` 且包含对齐模型；`scripts/package-installer.ps1` 实际使用 `release-screen` / `release-lite`，并拒绝 `models/small.pt`。`PROJECT_SPEC.md` 仍描述列表循环和 whisper.cpp，和现行三种播放模式、OCR-only 加装版不一致。

## 2. 忽略规则与换行

用 `git check-ignore` 对用户列出的路径做了核对。索引里的文本文件没有 CR（`git grep` 检索回车为 0）。`tests/*.cpp` 在本克隆中是 LF。

### 已经盖住的产物

| 本地路径 | 规则 |
| --- | --- |
| `build/`、`build-debug/`、`build-lite/` | `.gitignore` 第 1、2、15 行 |
| `.cache/`、`dist/` | 第 4、18 行 |
| `release/`、`release-lite/`、`release-screen/` | 第 3、16、17 行 |
| `library-workbench-*`、`queue-controls-*` | 第 22、21 行 |
| `debug.log` 以及其他 `*.log` | 第 14 行 |
| `models/*.bin`、`models/*.pt`、`models/ocr/` | 第 8–9、20 行 |
| `data/`、Cookie、`tools/*.exe`、`tools/*.dll`、歌词运行时、OCR GPU 运行时 | 第 5–7、10、19、29–30 行 |

### 建议 A1：补上其余构建目录和整个模型目录

- **问题**：只点名了三个 `release*` 目录。`release-online/`、`release-full/` 或其他 `release*` 不会被忽略。`models/` 只忽略 `*.bin`、`*.pt` 和 `models/ocr/`，`models/*.onnx`、`*.gguf` 或其他子目录仍可能被 `git add` 收进去。仓库根上的源码内构建（`CMakeCache.txt`、`CMakeFiles/`、`compile_commands.json`）、`.vs/`、`cmake-build-*` 也没有规则。`docs/screenshots/` 已忽略，但已跟踪的 4 张截图不会因此退出索引。
- **证据**：`.gitignore` 第 1–34 行。`git check-ignore` 忽略了 `release-screen/BiliWave.exe`、`models/small.pt`、`models/ocr/a.onnx`、`debug.log`，没有忽略 `release-online/BiliWave.exe`、`models/other/model.onnx`、`CMakeUserPresets.json`、`.vs/`、`cmake-build-release/`。`git ls-files models` 为空，说明模型本来就不应入库。
- **影响**：一次宽泛的 `git add` 可能把几百 MiB 的模型、旧发行目录或本机 Qt 用户文件送进历史。
- **建议**：用 `/release*/` 替换三个点名；将 `/models/` 整目录忽略（仓库里没有已跟踪的模型文件）。补上 `/CMakeCache.txt`、`/CMakeFiles/`、`/compile_commands.json`、`/CMakeUserPresets.json`、`.vs/`、`cmake-build-*/`。已跟踪截图如无发布需要，用 `git rm --cached` 移出索引。
- **优先级**：高
- **工作量**：小

### 建议 A2：换行规则保持 LF，并加编辑器约定

- **问题**：本地只有 `tests/*.cpp` 显示 CRLF/LF 差异。这不是索引里存了回车。`.gitattributes` 已经要求文本和 `*.cpp` 以 LF 入库、以 LF 检出。Windows 编辑器若把测试文件存成 CRLF，配合 `core.autocrlf=true`，状态或外部比较工具会把整文件显示成换行差异。仓库没有 `.editorconfig`，Qt Creator / Visual Studio 默认仍可能写 CRLF。
- **证据**：`.gitattributes` 第 1–2 行；`git check-attr eol -- tests/core_tests.cpp` 为 `lf`；本克隆 `tests/core_tests.cpp` 的 CR 计数为 0。
- **影响**：提交时如果绕过属性，会制造无意义的整文件 diff，掩盖真正的测试改动。
- **建议**：不要把 `*.cpp` 改成 CRLF。增加 `.editorconfig`（`end_of_line = lf`，`insert_final_newline = true`）。本地确认没有内容改动后执行 `git add --renormalize -- tests`，这次操作不应产生新的内容提交。编辑器里对这个仓库关闭「按系统换行保存」。
- **优先级**：中
- **工作量**：小

## 3. 性能

播放位置更新已经不再查库，双语时间轴有缓存，歌单统计改为聚合查询。这些优化有 `docs/PERFORMANCE.md` 的局部基准。下面是仍然会碰到用户的路径。

### 建议 B1：打开资料库时不要在界面线程逐个 stat 文件

- **问题**：`Library` 构造函数在打开 SQLite 后，对每首 `filesize=0` 的本地歌曲同步 `QFileInfo::size()`，并且每首一次 `UPDATE`，没有事务。`main()` 在显示窗口之前就构造 `Library`。
- **证据**：`src/library.cpp` 第 68–76 行；`src/main.cpp` 第 67–72 行。
- **影响**：乐库在机械盘、网络盘或歌曲很多时，首窗出现前会卡住。失败的 `UPDATE` 被忽略，下次启动会再扫一遍。
- **建议**：把补齐文件大小挪到启动后的空闲任务，或只在导入和手动校验时写入；多条更新放进一个事务。已有大小的行不要再 stat。
- **优先级**：高
- **工作量**：小

### 建议 B2：入队和切歌不要读取并转换整首歌词

- **问题**：`Player::enqueue` 用 `Library::track()` 判断歌曲是否存在。`track()` 执行 `SELECT *`，再经 `lyricView()` 做简繁转换。`playPlaylist()` 先用了轻量的 `trackSummaries()`，随后 `setQueue()` 又把每首重新读成完整行。简繁词典在第一次调用时于界面线程建 trie，缺失则 `qFatal`。
- **证据**：`src/player.cpp` 第 144–151、275–279 行；`src/library.cpp` 第 153–157、339–344 行；`src/lrc.cpp` 第 23–53、80–85 行。播放页刷新队列也会对每个条目调用 `track()`（`src/playbackpage.cpp` 第 517 行附近）。
- **影响**：把一张长歌单加入队列、或恢复一个很长的会话队列时，界面线程要解析并转换每首 LRC。词典首次加载还会叠加一次可见停顿。
- **建议**：存在性检查只用 `trackIds()` 或一条 `SELECT id`。歌词正文只在当前曲目变化时读取。简繁转换的结果按歌曲和开关缓存，词典初始化失败改为可恢复错误，不要 `qFatal`。
- **优先级**：高
- **工作量**：中

### 建议 B3：启动时建完全部页面，并可能立刻解析在线音轨

- **问题**：主窗口构造函数同步创建搜索、乐库、播放、工作台和设置五页。工作台约 1035 行，设置约 649 行。随后恢复队列；若当前项是 URL 歌曲，会马上启动 yt-dlp。自动消声在事件循环开头按设置打开麦克风和 WASAPI 监听。`StreamService` 构造时就在 `127.0.0.1` 上监听。
- **证据**：`src/mainwindow.cpp` 第 112–118、361–389 行；`src/player.cpp` 第 298–316 行；`src/autoducking.cpp` 第 254、263–290 行；`src/streamservice.cpp` 第 14–16 行。
- **影响**：冷启动成本里有大量当前看不见的控件，以及一次最长 60 秒的外部进程。用户只想打开乐库时也会付这笔费用。
- **建议**：工作台和设置在第一次进入时创建。恢复队列时若上次是暂停或退出，不要自动 `resolve`。监听端口延到第一次 URL 播放。自动消声保持现有「设置开启才采样」的行为即可。
- **优先级**：中
- **工作量**：中

### 建议 B4：封面在界面线程解码，也没有缓存

- **问题**：搜索结果对每个封面立刻 `GET`。完成回调在界面线程 `loadFromData`，再用 `SmoothTransformation` 缩到 96×54。没有磁盘缓存，没有并发上限，翻页会使上一代请求作废但已经发出的连接仍会跑完。
- **证据**：`src/searchpage.cpp` 第 179–199 行。
- **影响**：一页 20 张原图时，列表滚动和键盘选歌会抖。重复搜索同一页会重复下载。
- **建议**：主机限定在哔哩哔哩图床（见 D1）。内存或磁盘缓存按 URL。最多 4 个并发。缩放放到工作线程，或请求 CDN 上的小图。生成号仍然用来丢弃过期结果。
- **优先级**：中
- **工作量**：中

### 建议 B5：URL 歌曲每次播放都重新跑 yt-dlp

- **问题**：在线音轨不落盘，这是对的。但单曲循环在每次结束时调用 `activate()`，从而再次 `streamRequested`。解析超时 60 秒，进程参数里带 Cookie 路径。字幕 worker 对同一视频流有 300 秒 JSON 缓存，播放路径没有等价缓存。
- **证据**：`src/player.cpp` 第 350–356、313–314 行；`src/streamservice.cpp` 第 111–121 行；`workers/lyric_worker.py` 第 264–272 行。
- **影响**：循环一首 URL 歌会反复打 B 站和 CDN 签名接口，间隔可能超过 1 秒的歌词限流策略，也让循环出现数秒空白。
- **建议**：在内存中缓存「源 URL → 已签名音流 + 到期时间」。循环和暂停恢复在到期前复用。到期或 403 再解析。缓存不要写入磁盘。
- **优先级**：高
- **工作量**：中

### 建议 B6：字幕段为了取中间帧保留了每一帧

- **问题**：OCR 主路径已经只对代表帧做识别，这是正确的。但 `FrameSpan` 把该段里每一帧都按 JPEG 质量 97 写入缓冲，最后只解码正中间那一张。默认采样 0.5 秒，一段长时间静止字幕会积累大量 JPEG。
- **证据**：`workers/subtitle_segments.py` 第 34–62 行；`workers/lyric_worker.py` 第 451–479 行。
- **影响**：长视频、静止字幕或片头会把内存顶到 8 MiB 溢出阈值后再写临时文件，CPU 花在永远不会被识别的编码上。
- **建议**：只保留最接近当前段中点的一帧，或固定容量的蓄水池。质量可以降到 85 左右，识别前仍用原始解码帧则不必存 JPEG。
- **优先级**：中
- **工作量**：小

在线歌词侧已经是单请求、至少间隔 1 秒、每次点击最多 8 次、30 秒截止、2 MiB 响应上限、429/503 停止且至少冷却 60 秒。这次在本地夹具上测得该套测试约 23.6 秒，时间主要花在故意的间隔上，不是实现失误。不建议为了变快而改成并发。

## 4. 代码质量与可维护性

### 建议 C1：`MainWindow` 仍然过大，页面无法独立测试

- **问题**：类声明集中了导航、搜索、乐库、播放、工作台、设置、会话和歌词预览。实现拆进多个 cpp 后，最大的实现文件仍是 `src/lyricworkflow.cpp`（1713 行）和 `src/workbenchpage.cpp`（1035 行）。界面测试 `tests/redesign_tests.cpp` 有 1949 行，靠对象名查找控件。
- **证据**：`src/window.h` 第 142–305 行；上述文件行数。`docs/PROJECT_STRUCTURE.md` 已说明页面继续共享 `MainWindow`。
- **影响**：改主题或播放队列容易碰到不相关的槽函数。界面测试必须使用 Windows 平台插件和真实焦点，无法在无头环境稳定运行。
- **建议**：不必引入一套新框架。先把工作台草稿和字幕向导收成拥有自己控件的 `QWidget` 子类，让 `MainWindow` 只保留导航和会话。新测试优先覆盖这些子类的纯逻辑，而不是再扩大 `redesign_tests.cpp`。
- **优先级**：中
- **工作量**：大

### 建议 C2：两套在线歌词客户端，限流策略不一致

- **问题**：实际检索用 `LyricsSourcesSearch`（不自动重试）。`OnlineLyricsSearch` 仍会在 503 且等待不超过 5 秒时最多重试 2 次，User-Agent 还写着 `BiliWave/1.10.0`。排序和标题清洗被新客户端复用，所以不能直接删掉整个类。
- **证据**：`src/onlinelyrics.cpp` 第 183、203–211 行，以及第 373–384、401–414 行；`src/services.cpp` 第 450–473 行只连接 `LyricsSourcesSearch`。
- **影响**：以后若有人重新接上旧客户端，会违反「遇限流不重试」的产品规则，并用过期版本号联系 LRCLIB。
- **建议**：把 `cleanTitle`、`normalized`、`ranked` 留在无网络的函数里。删除或停用 `OnlineLyricsSearch` 的请求状态机，测试改为直接调用纯函数和 `LyricsSourcesSearch`。
- **优先级**：中
- **工作量**：中

### 建议 C3：音轨打分写了两份，且不完全相同

- **问题**：下载导入和 URL 播放各自遍历 `formats`，都偏好 FLAC/ALAC 再看码率。导入额外把采样率加进分数，播放路径不加。
- **证据**：`src/services.cpp` 第 241–255 行；`src/streamservice.cpp` 第 30–46 行。
- **影响**：同一视频的「下载文件」和「URL 播放」可能选中不同音轨，用户会认为音质不稳定。
- **建议**：抽一个纯函数，输入 formats JSON，输出选中项。两处调用它，并为 FLAC、ALAC、仅音频、空 URL 加测试。
- **优先级**：中
- **工作量**：小

### 建议 C4：资料库错误经常被吞掉，SQLite 只在界面线程使用

- **问题**：多数写操作失败时只返回 `false`，不更新 `Library::error()`。`trackIds()` 连 `exec()` 的返回值都不看。没有任何互斥锁；这在「全部调用来自 GUI 线程」的前提下是安全的，代码没有把这个前提写下来。自动消声线程通过信号回到界面，`stop()` 会在界面线程 `wait()` 工作线程。
- **证据**：`src/library.cpp` 第 187–192、306–313 行；`src/autoducking.cpp` 第 172–180、182–206 行。
- **影响**：磁盘满或数据库锁住时，界面只表现为「没变化」。若以后把查询丢进线程池，会直接出现未定义行为。退出时 `wait()` 可能让窗口关闭多等一个采样周期（麦克风 500 ms）。
- **建议**：写失败时保留 `lastError()` 并发一条状态消息。在 `library.h` 注明「仅 GUI 线程」。消声线程的停止改为 `requestInterruption` 后由 `finished` 信号收尾，避免在关闭事件里无界等待。
- **优先级**：中
- **工作量**：中

### 建议 C5：析构函数里同步等待外部进程

- **问题**：导入服务和字幕任务在析构时 `kill` 后 `waitForFinished(3000)`。流服务对每个子进程等待 2000 ms。
- **证据**：`src/services.cpp` 第 98–104 行；`src/lyricworkflow.cpp` 第 261–264 行；`src/streamservice.cpp` 第 18–24 行。
- **影响**：退出或销毁窗口时，界面最多卡住数秒。Windows 上已有 Job Object，子进程会随作业关闭，这次等待多数是多余的。
- **建议**：关闭作业对象后异步回收。测试里若必须等待，只在测试进程中等待。
- **优先级**：低
- **工作量**：小

查询本身使用占位符，`instr(lower(?))` 没有拼接用户字符串，这一点应保持。搜索不能用普通 B-tree 索引；歌曲过万以后可以再考虑 FTS5，目前不是瓶颈。

## 5. 构建与打包

### 建议 E1：最低 Qt 版本写得比代码实际使用的低，预设只含一台机器

- **问题**：`find_package` 要求 Qt 6.5。源码使用 `QPalette::Accent`、`QEvent::DevicePixelRatioChange` 和 `QtAudio::State`，这些接口晚于 6.5（Accent 与像素比事件在 6.6，`QtAudio` 命名空间在 6.7）。预设和全部脚本把 Qt、编译器、Ninja 写死在 `D:/PC/EXE/QtCreator/...`。
- **证据**：`CMakeLists.txt` 第 10 行；`src/window.cpp` 第 74 行；`src/progressbar.cpp` 第 67 行；`src/autoducking.cpp` 第 308 行；`CMakePresets.json` 第 5–16 行；`scripts/build.ps1` 第 1–6 行。
- **影响**：按文档「6.5+」在一台新机器上配置，会在编译期失败。换一台电脑必须改脚本参数，预设不能直接用。
- **建议**：把 `find_package` 提高到实际用到的小版本（与本机 6.11 对齐，或至少 6.7），并在 README 写明。预设改为读取 `Qt6_ROOT` 环境变量；本机绝对路径留在不提交的 `CMakeUserPresets.json`。
- **优先级**：中
- **工作量**：小

### 建议 E2：测试构建把不会运行的 live 程序也链进来

- **问题**：`BUILD_TESTING` 打开时总会编译 `live_lyrics_sources`。识别开关打开时再编译 `live_lyrics`、`live_search`、`live_stream`。它们都没有 `add_test`。每个界面测试都把 `-platform windows` 写进 CTest。
- **证据**：`CMakeLists.txt` 第 47–82 行。
- **影响**：日常 `build.ps1` 多编 3–4 个可执行文件。把 CTest 原样拿到无交互桌面或 Linux 上，除 `core_tests` 外都会因缺少 `windows` 平台插件失败。
- **建议**：live 目标放到 `BILI_BUILD_LIVE_TESTS` 选项后面，默认关闭。逻辑测试使用 `offscreen`；只有真正需要焦点的测试才要求 Windows 插件，并在 CTest 里标成需要图形会话。
- **优先级**：高
- **工作量**：小

### 建议 E3：发行目录里复制了整棵 `docs/`，且有两套 FFmpeg

- **问题**：`windeployqt` 会带上 Qt Multimedia 的 LGPL FFmpeg 插件。部署脚本另外复制一份 Gyan GPLv3 的 `ffmpeg.exe` / `ffprobe.exe`。脚本还把仓库 `docs/` 整棵复制进 `release-screen`。安装脚本的排除列表没有去掉文档、主题预览和验证 JSON。1.12.0 加装版安装包 211.63 MiB、装后约 667 MiB，主要来自 Qt、嵌入式 Python、OpenCV、ONNX Runtime 和 OCR 模型，文档本身只有约 1 MiB，但会把开发记录装进用户目录。
- **证据**：`scripts/deploy-screen.ps1` 第 6、29–37 行；`packaging/BiliWave.iss` 第 74–75 行；`docs/RELEASE_1_12_0_VALIDATION.json` 第 6–16、23–24、38–39 行；`THIRD_PARTY_NOTICES.md` 第 8–10 行。
- **影响**：用户安装目录里出现审查记录和截图。两套 FFmpeg 增加体积，也使许可证组合更难解释。
- **建议**：部署时只复制用户需要的 README 和 `THIRD_PARTY_NOTICES.md`。确认播放插件和命令行 FFmpeg 缺一不可后，在第三方声明里写清「播放后端是 LGPL，命令行工具是 GPL」。若要明显缩小加装版，把 OCR 运行时改成首次使用时下载，并校验现有 SHA-256。
- **优先级**：中
- **工作量**：中

### 建议 E4：仓库没有项目自身的许可证

- **问题**：`THIRD_PARTY_NOTICES.md` 写明该文件不给应用源码授权。根目录没有 `LICENSE`。随安装包分发的命令行 FFmpeg 是 GPLv3，yt-dlp 的 Windows 可执行文件包含 GPL 依赖。Qt 是动态 LGPL，文档已要求保留替换能力。
- **证据**：`THIRD_PARTY_NOTICES.md` 第 9–10、29 行；仓库根目录无 `LICENSE`。
- **影响**：继续发布安装包时，GPL 工具和未授权的应用源码放在同一目录，接收方无法判断源码是否必须按 GPL 提供。这是分发合规问题，不是播放功能缺陷。
- **建议**：选定一种与所捆绑 GPL 工具兼容的许可证，或把 GPL 命令行工具改为安装后单独下载。保留现有第三方文本、OpenCC Apache-2.0 声明和 Qt 动态链接说明。
- **优先级**：高
- **工作量**：中（决定许可证本身超出代码改动；落地文件是小改动）

编译告警只加在 MinGW 上（`CMakeLists.txt` 第 37–42 行）。当前工具链匹配。若以后用 MSVC，需要同样打开 `/W4`。没有 ccache、预编译头或统一构建。对象库已经避免界面文件被每个测试编译多次，这是该保留的。

## 6. 测试与 CI

### 这次实际跑了什么

云端没有 Qt 6.11 / MinGW / Windows。系统 Qt 是 6.4.2，低于 `find_package(Qt6 6.5)`，因此**未改仓库的正式配置失败**，错误是找不到兼容的 Qt6。

为了判断源码能否编译，只在 `/tmp` 副本上做了实验（降低版本号、把 `features.h` 改名、把 `QtAudio::` 换成 6.4 的 `QAudio::`）。这些改动没有进入分支。结果：

| 目标 | 结果 |
| --- | --- |
| 正式 CMake 配置 | 失败。系统 Qt 6.4.2，工程要求 ≥ 6.5 |
| 实验副本中的 `bili_core`、`bili_services` | 在改名 `features.h` 并替换 `QtAudio` 后链接成功 |
| `core_tests -platform offscreen` | **16 passed，0 failed，135 ms**。有一条 PulseAudio 连接失败警告，对应用例仍通过 |
| `online_lyrics_tests -platform offscreen` | **12 passed，0 failed，约 23.6 s**。夹具是本机 TCP，没有访问外网 |
| `bili_ui` | 未编过。`QPalette::Accent`、`QEvent::DevicePixelRatioChange` 在 Qt 6.4 不存在 |
| `workbench_tests`、`ducking_tests`、`integration_tests`、`redesign_tests`、`lyric_workflow_tests`、`lite_tests` | 依赖界面库或 Windows 插件，未运行 |
| `live_search`、`live_lyrics`、`live_stream`、`live_lyrics_sources` | 未编译、未运行。它们需要外网、yt-dlp 和 Windows |
| `tests/*.py` | 未运行。系统 Python 没有该工程的 RapidOCR / OpenCV 运行时 |

`src/features.h` 被 `-I src` 放在尖括号搜索路径上。实验编译里，`#include <features.h>` 打开的是项目头而不是 glibc 头，于是 libstdc++ 在 `__GLIBC_PREREQ` 处失败。MinGW 的 libstdc++ 不走这条包含，所以现有 Windows 构建可以成功。这是可移植性缺陷，不是这次实验引入的。

### 建议 F1：加上最小 CI，并把平台插件从逻辑测试里拿掉

- **问题**：没有 `.github/workflows`。合并请求不会编译，也不会跑 CTest。界面测试命令写死 `-platform windows`。
- **证据**：仓库无 `.github/`；`CMakeLists.txt` 第 47、53、57、61、68、71、75、86 行。`core_tests` 是唯一使用 `offscreen` 的注册测试。
- **影响**：`fb01521` 这类结构改动只能靠作者本机的 7 组或 5 组 CTest。换行、CMake 或歌词状态机的回归要等下一次手工发布才被发现。
- **建议**：先加一个 Windows 工作流，安装与预设一致的 Qt 6.11 MinGW，运行不访问网络的 CTest。逻辑测试改 `offscreen`。需要焦点的测试单独一个 job，失败日志保留现有的 `*-results.txt`。Linux job 可以在改掉 `features.h` 名字之后只跑 `core_tests` 和 `online_lyrics_tests`，作为接口意外破坏的警报。
- **优先级**：高
- **工作量**：中

### 建议 F2：live 测试会改写已提交的证据，也不适合进默认 CI

- **问题**：`live_lyrics` 把结果写到 `docs/live-lyrics-result.json`，`live_stream` 写到 `docs/url-stream-live-result.json`。这两个文件已在 Git 里。测试依赖真实的 LRCLIB 或哔哩哔哩，超时 30–90 秒，没有重试，也没有录制回放。`live_search` 使用调用者给出的查询词。Python 的 `--live` 同样在脚本之外。
- **证据**：`tests/live_lyrics.cpp` 第 27–30 行；`tests/live_stream.cpp` 第 26–27、44–45 行；`CMakeLists.txt` 第 77–82 行没有 `add_test`。
- **影响**：手工跑一次 live 测试就会把时间戳和网络结果 diff 进文档。放进 CI 会因 B 站风控、歌词站 429 或样本下架而随机失败。
- **建议**：输出改到 `build/` 或 `.cache/`。保留 `docs/` 里的历史 JSON 作为某次验收快照，不要让测试覆盖它们。CI 不跑 live。需要周期性外网检查时，用手动 workflow，并允许失败。
- **优先级**：中
- **工作量**：小

### 建议 F3：Python 测试没有进入 CTest，覆盖率没有度量

- **问题**：OCR 几何、分段和 worker 的回归在 `tests/*_tests.py`，依赖 `tools/lyrics-runtime`。CMake 不调用它们。没有 gcov / llvm-cov 或覆盖率门槛。1.12.0 验证记录里的通过数与当前 `core_tests` 用例数已经不一致（记录为 13，本次 offscreen 跑出 16 个 Qt Test 项，含 init/cleanup）。
- **证据**：`CMakeLists.txt` 的测试段没有 Python；`tests/ocr_geometry_tests.py` 第 1–9 行要求私有运行时。
- **影响**：改 `workers/subtitle_segments.py` 或空格逻辑时，C++ 测试可以全绿。发布记录也不能再当作「当前用例数」使用。
- **建议**：加装版构建在 OCR 运行时存在时用 `add_test` 调用其 `python.exe`。覆盖率先只对 `bili_core` 和 `onlinelyrics.cpp` 收集，不把界面测试算进门槛。
- **优先级**：中
- **工作量**：中

`online_lyrics_tests` 用本地 TCP 模拟限流、分页和塞壬目录，这次 12 项全部通过。这条套件应留在每次提交里，不要为了速度删掉间隔断言。

## 7. 安全与稳健性

### 建议 D1：搜索没有 WBI 签名，封面和音流主机过宽

- **问题**：搜索请求只带浏览器 User-Agent、`Referer` 和 Cookie。没有 `w_rid` / `wts` 一类签名。失败时提示用户配置 Cookie，这能覆盖一部分风控，但不能代替网页端现在要求的签名。封面 URL 来自接口的 `pic` 字段，客户端不检查主机就发出 `GET`。播放代理只检查用户输入的页面是哔哩哔哩 HTTPS，随后 yt-dlp 返回的 `url` 只要是 http 或 https 就会由本机代理去取，包括内网地址。
- **证据**：`src/searchservice.cpp` 第 140–173、194–199 行；`src/searchpage.cpp` 第 179–183 行；`src/streamservice.cpp` 第 55–62、84–88、153–163 行。塞壬歌词 CDN 反而有主机白名单（`src/onlinelyrics.cpp` 第 578–587 行），搜索和音流没有同等限制。
- **影响**：搜索接口策略收紧时，功能会突然变成「服务暂不可用」加一个数字错误码。异常或被篡改的元数据可以让播放器代为请求非 CDN 地址。封面也可以指向任意主机。
- **建议**：实现哔哩哔哩当前文档中的 WBI mixin，签名材料放在代码里并随接口变更更新；导航接口结果可以短时缓存。封面只允许已知图床。代理上游只允许已知音视频 CDN，拒绝回环以外的私网、链路本地和元数据地址。保持「不关闭 TLS 校验」。
- **优先级**：高
- **工作量**：中

### 建议 D2：Cookie 整文件读入，并以路径形式出现在子进程命令行

- **问题**：搜索在界面线程 `readAll()` Cookie 文件，只过滤域名和过期时间，没有大小上限。yt-dlp 通过 `--cookies <路径>` 接收它。错误文本会抹掉 URL（`cleanError`），这是好的，但进程列表仍能看到 Cookie 文件路径。文件内容没有写入日志。
- **证据**：`src/searchservice.cpp` 第 147–173 行；`src/services.cpp` 第 43–46、201–203 行；`src/streamservice.cpp` 第 115–117 行。
- **影响**：一个异常巨大的「Cookie 文件」会在点搜索时卡住界面。本机其他用户若能看进程命令行，能知道文件位置；文件本身仍需磁盘权限才能读取。
- **建议**：限制读取字节（例如 1 MiB）和行数。路径继续传给 yt-dlp，不要把 Cookie 值放进参数。设置页说明该文件等价于登录凭据，不要放进同步盘。
- **优先级**：中
- **工作量**：小

### 建议 D3：没有应用或 yt-dlp 的更新机制

- **问题**：版本来自 CMake，安装包不会再向 GitHub 查询新版本。yt-dlp 被固定为 2026.08.19，并用 SHA-256 校验下载。这保证了可重复构建，也意味着 B 站页面一变，已安装的用户必须等新的安装包。
- **证据**：`scripts/setup-dependencies.ps1` 第 16 行；`THIRD_PARTY_NOTICES.md` 第 9 行；源码中没有版本检查或更新下载。`CMakeLists.txt` 第 2 行版本仍为 1.12.0，而 `CHANGELOG.md` 第 3–13 行已有未发布变更。
- **影响**：解析失败会被显示成网络或登录问题。用户无法区分「该更新 yt-dlp」和「视频确实不可用」。
- **建议**：设置里显示当前 yt-dlp 日期，并提供「下载已记录的下一个校验版本」而不是关闭校验的自动覆盖。应用更新可以只做「发现新 Release 时打开发布页」，不要静默替换可执行文件。发版时先把 CMake 版本和 CHANGELOG 对齐。
- **优先级**：高
- **工作量**：中

本机代理的其他行为是稳妥的：只监听 `127.0.0.1`，路径含 UUID，请求体限制 16 KiB，`Range` 有正则，未知路径返回 404。状态行把所有 HTTP 码都写成 `OK`（`src/streamservice.cpp` 第 171–172 行），播放器通常只看数字，但 206 会被写成不标准的原因短语。改成使用上游原因短语即可，工作量小，优先级低。

单实例套接字使用 `QLocalServer::UserAccessOption`，并限制 64 KiB，这是合适的。自动消声只在 Windows 上枚举音频会话，失败会回到状态文字，不会假装消声成功。

## 8. 用户体验与功能建议

现有交互里已经有比较完整的部分：三种播放模式并兼容旧编号、队列可重复、工作台按歌曲隔离草稿、在线源串行分批、无歌词时留白、安装器可覆盖或并列安装、五种主题不覆盖用户自定义的桌面歌词颜色。

### 建议 G1：把「解析中」和「需要更新解析器」说清楚

- **问题**：URL 播放在 yt-dlp 结束前没有独立于普通缓冲的状态。失败文案是「检查网络、登录权限或重试」。搜索失败把 Qt 错误和服务端 `code` 放在一起。
- **证据**：`src/streamservice.cpp` 第 80–82、119 行；`src/searchservice.cpp` 第 190–199 行。
- **影响**：风控码、签名缺失、yt-dlp 过期和真的断网看起来一样，用户会反复重试并加重风控。
- **建议**：解析期间在播放按钮旁显示「正在解析在线音轨」。对已知的风控码给出「稍后再试或更新解析组件」，对工具缺失给出设置页路径。不要把原始 HTML 或 Cookie 显示出来。
- **优先级**：中
- **工作量**：小

### 建议 G2：工作台和设置的信息密度

- **问题**：设置页同时放消声、主题、设备、歌词源、OCR 和工具路径。工作台同时放候选、时间轴、偏移、裁剪和试听。功能是齐的，首次使用要靠说明文字自己分类。
- **证据**：`src/settingspage.cpp` 约 649 行；`src/workbenchpage.cpp` 约 1035 行；CHANGELOG 已把歌词源收进「工作台设置」。
- **影响**：简装版用户仍会在长页里经过与自己无关的识别说明（识别控件有开关，但页结构仍是一整页）。
- **建议**：保持一个窗口。用现有分组把「播放与设备」「歌词源」「字幕识别」「工具」做成页内锚点或子页，而不是新的顶级导航。简装版不创建识别分组。
- **优先级**：低
- **工作量**：中

### 建议 G3：本地导入和快捷键保持现状，补一条失败汇总

- **问题**：拖入文件夹只收第一层，子目录被跳过并提示，这是明确的产品行为。提示在导入服务里一次性发出。若用户一次拖入很多非法文件，对话框会很长。
- **证据**：`src/services.cpp` 第 143–173 行；README 第 33 行。
- **影响**：大批量导入时模态提示淹没真正的失败原因。
- **建议**：超过数条时显示计数，并把完整列表放到可展开的详情或日志，不改变「不递归、不自动配歌词」的规则。
- **优先级**：低
- **工作量**：小

不建议在这一轮把播放模式改回四种，也不建议把在线歌词改成并发请求。这两处是已经收敛过的行为。

## 9. 路线图

按优先级排列。同一级里，前面的项更影响现有用户或仓库安全。

### 高

1. **D1** 搜索 WBI 签名，封面和音流 CDN / 图床白名单。工作量中。
2. **D3** yt-dlp 校验更新入口，并把 CMake 版本与未发布 CHANGELOG 对齐。工作量中。
3. **F1 + E2** 最小 CI；live 程序默认不编译；逻辑测试改为 offscreen。顺手把 `src/features.h` 改成不会遮蔽系统头的名字（例如 `editionfeatures.h`），否则 Linux 或某些 GCC 无法作为 CI 第二平台。工作量中。
4. **B1 + B2** 去掉启动时的逐文件 stat，入队不要读取整首歌词。工作量中。
5. **B5** URL 音流短时缓存，循环不再每次解析。工作量中。
6. **A1** `.gitignore` 覆盖全部 `release*` 和整个 `models/`，以及源码内 CMake 产物。工作量小。
7. **E4** 补上与 GPL 工具相符的项目许可证，或改为外置这些工具。工作量中（决策大于代码）。

### 中

8. **E1** 提高 Qt 最低版本，预设改为环境变量。工作量小。
9. **C3** 合并两处音轨打分。工作量小。
10. **C2** 收掉旧歌词客户端的自动重试。工作量中。
11. **B4 + B6** 封面缓存与离线程解码；OCR 段只保留代表帧。工作量中。
12. **F2** live 测试不再覆盖 `docs/` 下已提交的 JSON。工作量小。
13. **C1** 把工作台和字幕向导从 `MainWindow` 里拆成独立控件。工作量大。
14. **E3** 安装包不要复制整棵 `docs/`，并写清两套 FFmpeg 的许可证。工作量中。
15. **A2** 增加 `.editorconfig`，本地 renormalize 测试文件换行。工作量小。
16. **C4 + G1** 资料库错误可见；解析失败区分风控、过期工具和断网。工作量中。
17. **F3** 把 Python OCR 单测接进加装版 CTest。工作量中。
18. **B3** 延迟创建工作台/设置，恢复会话时不要自动解析。工作量中。

### 低

19. **C5** 退出时不要同步 `waitForFinished`。工作量小。
20. **G2 + G3** 设置页分组和批量导入提示的折叠。工作量中。
21. 本机代理的 HTTP 原因短语、共享一个 `QNetworkAccessManager`、塞壬目录落盘、ccache / 预编译头。工作量小到中，对当前用户体感有限。

建议的第一段落地顺序是 1 → 3 → 4 → 6：先让搜索和构建在别人的机器上可信，再减启动和入队的界面线程工作，并防止模型和发行目录进 Git。URL 缓存和 yt-dlp 更新可以跟在下一次安装包之前。
