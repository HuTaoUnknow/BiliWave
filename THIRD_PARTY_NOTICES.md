# Third-party components

BiliWave uses the following independently licensed components. Source code and dependency replacement remain available through this CMake project and its setup script. Third-party executable tools run as separate child processes.

| Component | Version / source | License and local notices |
|---|---|---|
| Qt Core, Gui, Widgets, Multimedia, Network, Sql | 6.11.1, existing local Qt installation; https://download.qt.io/official_releases/qt/6.11/ | Dynamic Qt libraries; LGPLv3 / applicable Qt license. https://www.qt.io/licensing/open-source-lgpl-obligations ; `docs/licenses/Qt-LGPL-3.0.txt` |
| Qt Multimedia FFmpeg backend | 7.1.3 shipped with this Qt installation | LGPL 2.1 or later as reported by Qt; Qt deployment copies its matched DLLs. https://ffmpeg.org/legal.html ; `docs/licenses/FFmpeg-LGPL-2.1.txt` |
| yt-dlp executable | 2026.08.19; https://github.com/yt-dlp/yt-dlp/releases/tag/2026.08.19 | Upstream source is Unlicense; the bundled Windows executable includes GPLv3+ dependencies. See `docs/licenses/yt-dlp-LICENSE.txt`, `yt-dlp-THIRD_PARTY_LICENSES.txt`, and GPL text below. |
| FFmpeg / ffprobe tools | 9.0.2 essentials, Gyan distribution; https://github.com/GyanD/codexffmpeg/releases/tag/9.0.2 | GPLv3 distribution; `docs/licenses/LICENSE`. Matching FFmpeg source: https://github.com/FFmpeg/FFmpeg/commit/946fcce07b ; build details https://www.gyan.dev/ffmpeg/builds/ |
| MinGW GCC runtime | GCC 13.1.0 from the existing Qt Tools installation | GCC runtime exceptions and applicable runtime notices: https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html ; source https://gcc.gnu.org/releases.html |
| LRCLIB | Runtime network service, not bundled | https://lrclib.net/docs ; only read-only lyric lookup is used. |
| CPython embeddable | 3.11.9 AMD64; https://www.python.org/downloads/release/python-3119/ | Python Software Foundation license, bundled `tools/lyrics-runtime/LICENSE.txt`. |
| stable-ts | 2.19.1; https://github.com/jianfch/stable-ts | MIT, included in its `*.dist-info` directory. Upstream development is paused; this project pins and tests the version used. |
| OpenAI Whisper (Python and small.pt model) | 20250625; https://github.com/openai/whisper | MIT. Official small model SHA-256 `9ecf779972d90ba49c06d968637d720dd632c55bbf19d441fb42bf17a411e794`. |
| PyTorch / torchaudio CPU | 2.6.0+cpu; https://pytorch.org/ | BSD-style and bundled dependency notices in the runtime package license files. |
| RapidOCR ONNX Runtime | 1.4.4; https://github.com/RapidAI/RapidOCR | Apache-2.0. The package includes PaddleOCR v4 detection/recognition and v2 classification ONNX models. |
| PaddleOCR / RapidAI multilingual recognition models | Cyrillic and Latin PP-OCRv5 mobile, RapidAI model release v3.10.0 | Apache-2.0; `models/ocr/LICENSE-PaddleOCR.txt`. Official model URLs, SHA-256 and language coverage recorded in `docs/ocr-language-dependencies.json`. |
| ONNX Runtime / OpenCV | 1.31.0 / 4.11.0.86 | MIT / Apache-2.0 and bundled third-party notices. |
| ONNX Runtime DirectML (isolated OCR backend) | 1.24.4; https://pypi.org/project/onnxruntime-directml/1.24.4/ | MIT / packaged third-party notices and DirectML redistributable terms, retained in `tools/ocr-gpu/onnxruntime/`. Fixed wheel SHA-256 and source recorded in `docs/ocr-gpu-dependencies.json`. |

The private Python environment and transitive dependencies are copied intact, including their `*.dist-info`, license files and third-party notices. Exact installed versions are recorded in `scripts/lyric-requirements.txt` and `docs/lyric-dependencies.json`. The worker is in `workers/lyric_worker.py`; CPU execution does not require CUDA. Version 1.9 removes whisper.cpp transcription and its ggml model; the Python Whisper small.pt model remains solely for alignment of existing lyric text. Earlier dependency manifests and license files retain the historical transcription component records.

The downloaded yt-dlp executable was checked against the publisher's SHA-256 list. The FFmpeg archive was checked against the GitHub release asset SHA-256. The model was checked against its Hugging Face LFS SHA-256. Local executable hashes and download archive checks are recorded in `docs/dependencies.json`.

whisper.cpp is compiled with `CMAKE_SYSTEM_NAME=Windows`, `CMAKE_SYSTEM_PROCESSOR=AMD64`, `GGML_NATIVE=OFF`, `GGML_OPENMP=OFF`, and `_WIN32_WINNT=0x0601`. The final flag disables an optional Windows thread-throttling optimization whose newer declarations are absent in MinGW 13.1 headers. This affects inference scheduling, not transcription format or stored audio. CPU acceleration is portable; no GPU runtime is required.

For redistribution, retain all notices, the matching dependency source references, and the ability to replace dynamically linked Qt libraries. This notice describes dependencies and does not assign a license to the user's application source.
