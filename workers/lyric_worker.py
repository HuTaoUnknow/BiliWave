"""Local lyric alignment / cropped video OCR worker. JSON files are the IPC contract."""
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import unicodedata


class Cancelled(Exception):
    pass


def progress(message, percent=None):
    print(json.dumps({"message": message, "percent": percent}, ensure_ascii=False), flush=True)


def check_cancel(config):
    if Path(config["cancel"]).exists():
        raise Cancelled("已取消，原歌词保持不变")


def run(config, args, progress_label=None):
    """Keep every child cancellable, including downloader and FFmpeg."""
    log = Path(config["work"]) / "child.log"
    with log.open("wb") as out:
        child = subprocess.Popen(args, stdout=out, stderr=out,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        last_percent = -1

        def report_progress():
            nonlocal last_percent
            if not progress_label:
                return
            with log.open("rb") as reader:
                reader.seek(0, 2)
                reader.seek(max(0, reader.tell() - 8192))
                text = reader.read().decode("utf-8", errors="replace")
            values = re.findall(r"progress\s*=\s*(\d+)%", text)
            if values:
                percent = min(99, max(5, int(values[-1])))
                if percent > last_percent:
                    progress(progress_label, percent)
                    last_percent = percent

        try:
            while child.poll() is None:
                check_cancel(config)
                report_progress()
                time.sleep(.15)
            if child.returncode:
                raise RuntimeError(log.read_text("utf-8", errors="replace")[-1800:])
            report_progress()
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()


def plain_lines(text):
    text = text.lstrip("\ufeff").replace("\r\n", "\n").replace("\r", "\n")
    lines = []
    srt = "-->" in text
    for line in text.splitlines():
        line = line.strip()
        if not line or re.fullmatch(r"\[\w+:.*\]", line):
            continue
        if "-->" in line or (srt and line.isdigit()):
            continue
        line = re.sub(r"\[\d+:\d+(?:[.:]\d+)?\]", "", line).strip()
        if line:
            lines.append(line)
    return lines


def lrc_stamp(seconds):
    cs = max(0, round(seconds * 100))
    return f"[{cs // 6000:02d}:{cs // 100 % 60:02d}.{cs % 100:02d}]"


def align(config):
    worker_path = str(Path(__file__).resolve().parent)
    if worker_path not in sys.path:
        sys.path.insert(0, worker_path)
    import torch
    import stable_whisper
    import wave
    import numpy as np
    from alignment_quality import timing_hints, segment_data, review_reasons
    lines = plain_lines(config["text"])
    from bilingual import alignment_pairs
    lines, translations = alignment_pairs(lines)
    if not lines:
        raise ValueError("请先提供逐行歌词")
    if len("".join(lines)) > 100000:
        raise ValueError("歌词过长，请只保留当前歌曲演唱的歌词")
    model_path = Path(config["model"])
    if not model_path.is_file():
        raise ValueError("缺少强制对齐模型，请运行 scripts/setup-lyric-tools.ps1")
    torch.set_num_threads(max(1, min(8, (os.cpu_count() or 2) - 1)))
    wav = str(Path(config["work"]) / "alignment.wav")
    progress("准备音频副本，不改变原曲", 0)
    run(config, [config["ffmpeg"], "-hide_banner", "-nostdin", "-v", "error", "-y",
                 "-i", config["audio"], "-vn", "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le", wav])
    check_cancel(config)
    progress("加载本机已有歌词对齐模型", 1)
    model = stable_whisper.load_model(str(model_path), device="cpu")
    worker_path = str(Path(__file__).resolve().parent)
    if worker_path not in sys.path:
        sys.path.insert(0, worker_path)
    from languages import alignment_language
    language = alignment_language(config.get("language", "auto"), "".join(lines), model, wav)
    progress(f"按已有歌词对齐（{language}）", 2)

    def update(done, total):
        check_cancel(config)
        progress("正在匹配歌词时间", min(79 if hints else 99, round(done / max(total, 1) * (79 if hints else 100))))

    with wave.open(wav, 'rb') as reader:
        duration = reader.getnframes()/reader.getframerate()
        hints = timing_hints(config, lines, duration)
        audio = np.frombuffer(reader.readframes(reader.getnframes()), dtype=np.int16).astype(np.float32)/32768 if hints else None
    rows, fallbacks = [], []
    result = model.align(wav, "\n".join(lines), language=language, original_split=True,
                         regroup=False, verbose=None, progress_callback=update)
    if result is not None:
        rows = [segment_data(segment) for segment in result.segments]
    if hints:
        # The small model's global pass can attach a repeated verse to a long
        # instrumental. Same-version subtitle hints constrain each occurrence.
        # No additional model and no lyric transcription are performed.
        progress('核对同源字幕定位，局部重试异常句', 80)
        global_rows = rows if len(rows) == len(lines) else [None]*len(lines)
        rows, retries = [], {}
        def invalid(row, index):
            hint = hints[index]
            next_start = hints[index+1]['start'] if index+1 < len(hints) else duration
            if row is None:
                return True
            start, end, words = row['start'], row['end'], row['words']
            mean = sum(w['probability'] for w in words)/len(words) if words else 0
            return (not math.isfinite(start) or not math.isfinite(end)
                    or start < max(0, hint['start']-2) or start > min(hint['start']+2, next_start-.01)
                    or end-start < .08 or end-start > max(12, hint['end']-hint['start']+2)
                    or end > duration+.02 or mean < .35)
        for index, (text, hint) in enumerate(zip(lines, hints)):
            check_cancel(config)
            next_start = hints[index+1]['start'] if index+1 < len(hints) else duration
            row = global_rows[index]
            if invalid(row, index):
                first, last_index = index, index
                if index and hint['start']-hints[index-1]['end'] < 6:
                    first -= 1
                while (last_index+1 < len(hints) and last_index-first < 2
                       and hints[last_index+1]['start']-hints[last_index]['end'] < 6
                       and hints[last_index+1]['end']-hints[first]['start'] < 26):
                    last_index += 1
                window = first, last_index
                if window not in retries:
                    left = max(0., hints[first]['start']-2)
                    right = min(duration, hints[last_index]['end']+1, hints[last_index]['start']+18)
                    clip = audio[round(left*16000):round(right*16000)].copy()
                    retry = model.align(clip, '\n'.join(lines[first:last_index+1]), language=language,
                                        original_split=True, regroup=False, verbose=None)
                    retries[window] = ([segment_data(s, left) for s in retry.segments]
                                       if retry is not None and len(retry.segments) == last_index-first+1 else [])
                candidates = retries[window]
                row = candidates[index-first] if candidates else None
            if invalid(row, index):
                # An untrustworthy refinement must not masquerade as success.
                row = dict(start=hint['start'], end=min(next_start, hint['end']), words=[],
                           timing_origin='caption_fallback')
                fallbacks.append(index+1)
            else:
                row['timing_origin'] = 'audio_with_caption_hint'
            row['caption_start'] = hint['start']
            rows.append(row)
            progress('核对字幕定位与音频匹配', 80+round((index+1)/len(lines)*19))
    check_cancel(config)
    if len(rows) != len(lines):
        raise ValueError("未能完整对应每行歌词，请检查是否为同一演唱版本及副歌重复次数")
    output, warnings, details = [], [], []
    last = -1.
    for i, (line, row) in enumerate(zip(lines, rows)):
        start, end = row['start'], row['end']
        words = row.get('words', [])
        reasons = review_reasons(row, last)
        if row.get('timing_origin') == 'caption_fallback':
            reasons.append('音频校准不可靠，保留字幕出现时间')
        doubtful = bool(reasons)
        if not math.isfinite(start) or not math.isfinite(end) or start < 0 or start < last:
            raise ValueError("时间轴不连续，无法安全应用；请检查歌词版本")
        if doubtful:
            warnings.append(f"第 {i + 1} 行需试听检查（{'；'.join(reasons)}）：{line}")
        # Always preserve the user's text, punctuation and line breaks.
        output.append(lrc_stamp(start) + line)
        if i in translations:
            output.append(lrc_stamp(start) + translations[i])
        details.append({"text": line, "translation": translations.get(i, ""),
                        "start": start, "end": end, "review": doubtful,
                        "review_reasons": reasons,
                        "mean_probability": round(sum(w['probability'] for w in words)/len(words), 4) if words else None,
                        "timing_origin": row.get('timing_origin', 'audio'),
                        "caption_start": row.get('caption_start')})
        last = start
    progress(f"时间轴已生成，{len(warnings)} 行需复核" if warnings else "时间轴生成完成，请试听后应用", 100)
    return {"lrc": "\n".join(output), "lines": details, "warnings": warnings, "language": language,
            "timing_kind": "audio_with_caption_hints" if hints else "audio_alignment",
            "caption_fallback_lines": fallbacks,
            "source": config.get("source", "已有歌词") + (" · 字幕定位后音频校准" if hints else " · 音频强制对齐")}


def capture_json(config, args, target):
    target = Path(config["work"]) / target
    partial = target.with_name(target.name + ".pending")
    with partial.open("wb") as output, (Path(config["work"]) / "metadata-errors.log").open("wb") as errors:
        child = subprocess.Popen(args, stdout=output, stderr=errors,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            while child.poll() is None:
                check_cancel(config)
                time.sleep(.1)
            code = child.returncode
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
    if code:
        partial.unlink(missing_ok=True)
        raise ValueError("无法读取视频信息，请检查链接、网络或 Cookie 设置")
    try:
        info = json.loads(partial.read_text("utf-8-sig"))
        partial.replace(target)
        return info
    except (ValueError, OSError):
        partial.unlink(missing_ok=True)
        raise ValueError("视频信息不完整，请重试")


def video_input(config):
    """Resolve a stream only; never download a complete video into the task folder."""
    source = config["video"].strip()
    if not re.match(r"https?://", source, re.I):
        import cv2
        path = Path(source)
        if not path.is_file():
            raise ValueError("请选择有效的本地视频文件或哔哩哔哩链接")
        capture = cv2.VideoCapture(str(path))
        try:
            fps = capture.get(cv2.CAP_PROP_FPS)
            return {"url": str(path), "headers": {}, "width": int(capture.get(cv2.CAP_PROP_FRAME_WIDTH)),
                    "height": int(capture.get(cv2.CAP_PROP_FRAME_HEIGHT)),
                    "duration": capture.get(cv2.CAP_PROP_FRAME_COUNT) / fps if fps > 0 else 0}
        finally:
            capture.release()
    from urllib.parse import urlparse
    url = urlparse(source)
    if url.scheme != "https" or not (url.hostname in {"bilibili.com", "www.bilibili.com", "m.bilibili.com", "b23.tv"}):
        raise ValueError("在线视频仅支持哔哩哔哩 HTTPS 链接")
    key = hashlib.sha256(source.encode()).hexdigest()[:16]
    target = Path(config["work"]) / (key + ".stream.json")
    info = None
    if target.is_file() and time.time() - target.stat().st_mtime <= 300:
        try:
            cached = json.loads(target.read_text("utf-8-sig"))
            info = cached if isinstance(cached, dict) and cached.get("url") else None
        except (ValueError, OSError):
            target.unlink(missing_ok=True)
    if not info:
        progress("解析当前分 P 的视频流，仅按需读取画面", 0)
        args = [config["ytdlp"], "--ignore-config", "--no-playlist", "--no-progress", "--skip-download",
                "--dump-single-json", "-f",
                "bestvideo[vcodec^=avc1][height<=480]/bestvideo[height<=480]/best[height<=480]/bestvideo[height<=720]/best[height<=720]/bestvideo/best"]
        if config.get("cookies"):
            args += ["--cookies", config["cookies"]]
        info = capture_json(config, args + [source], key + ".stream.json")
    if urlparse(info.get("url", "")).scheme not in {"http", "https"}:
        raise ValueError("未找到当前分 P 可读取的视频流")
    return {"url": info["url"], "headers": info.get("http_headers", {}),
            "width": int(info.get("width") or 0), "height": int(info.get("height") or 0),
            "duration": float(info.get("duration") or 0)}


def input_args(video):
    args = []
    if re.match(r"https?://", video["url"], re.I):
        args += ["-rw_timeout", "20000000"]
        headers = dict(video.get("headers") or {})
        headers.setdefault("Referer", "https://www.bilibili.com/")
        lines = [f"{key}: {value}\r\n" for key, value in headers.items()
                 if not re.search(r"[\r\n]", str(key) + str(value))]
        if lines:
            args += ["-headers", "".join(lines)]
    return args + ["-i", video["url"]]


def crop_bounds(config, width, height):
    x, y, w, h = map(float, config.get("crop", [0, 2 / 3, 1, 1 / 3]))
    if not (0 <= x < 1 and 0 <= y < 1 and w > 0 and h > 0 and x + w <= 1.000001 and y + h <= 1.000001):
        raise ValueError("截取范围超出画面，请调整左、上、宽、高百分比")
    left, top = int(width * x), int(height * y)
    right, bottom = min(width, round(width * (x + w))), min(height, round(height * (y + h)))
    if right - left < 8 or bottom - top < 8:
        raise ValueError("截取区域过小")
    return left, top, right, bottom


def video_preview(config):
    import cv2
    video = video_input(config)
    duration = video["duration"]
    requested = max(0, float(config.get("preview_time", 0)))
    # Wrap short videos when advancing beyond the last frame.
    stamp = requested % duration if duration > 0 else requested
    frame = Path(config["work"]) / "preview.png"
    frame.unlink(missing_ok=True)
    run(config, [config["ffmpeg"], "-hide_banner", "-nostdin", "-v", "error", "-y", "-ss",
                 str(stamp), *input_args(video), "-an", "-frames:v", "1", "-vf", "scale='min(960,iw)':-1",
                 "-update", "1", str(frame)])
    image = cv2.imread(str(frame))
    if image is None:
        raise ValueError("无法读取视频画面，请调整预览时间")
    bounds = crop_bounds(config, image.shape[1], image.shape[0])
    l, t, r, b = bounds
    crop = Path(config["work"]) / "crop.png"
    run(config, [config["ffmpeg"], "-hide_banner", "-nostdin", "-v", "error", "-y",
                 "-i", str(frame), "-vf", f"format=bgr24,crop={r-l}:{b-t}:{l}:{t}:exact=1",
                 "-frames:v", "1", "-update", "1", str(crop)])
    cv2.rectangle(image, (l, t), (r - 1, b - 1), (0, 210, 140), max(2, image.shape[1] // 300))
    marked = Path(config["work"]) / "marked.png"
    cv2.imwrite(str(marked), image)
    return {"preview": str(marked), "frame_preview": str(frame), "crop_preview": str(crop), "preview_time": stamp,
            "duration": duration}


def normalized(text):
    return "".join(c.casefold() for c in unicodedata.normalize("NFKC", text) if c.isalnum())


def collect_ocr_events(samples, duration=None, interval=.5):
    """Merge adjacent identical captions only; retain repeated choruses after a gap."""
    events = []
    previous = ""
    for index, (stamp, text, score) in enumerate(samples):
        end = samples[index + 1][0] if index + 1 < len(samples) else stamp + interval
        if duration is not None:
            end = min(end, duration)
        key = normalized(text)
        if not key:
            previous = ""
            continue
        if key == previous and events:
            events[-1]["end"] = end
            if score > events[-1]["confidence"]:
                events[-1].update(text=text, confidence=score)
        else:
            events.append({"start": stamp, "end": end, "text": text, "confidence": score})
        previous = key
    return events


def events_to_lrc(events):
    output = []
    for event in events:
        for line in event["text"].splitlines():
            output.append(lrc_stamp(event["start"]) + line)
    return "\n".join(output)


def caption_result(result):
    from bilingual import caption_text
    blocks = sorted((row for row in (result or []) if float(row[2]) >= .55),
                    key=lambda row: (round(min(p[1] for p in row[0]) / 20), min(p[0] for p in row[0])))
    return caption_text(blocks), min((float(row[2]) for row in blocks), default=0.)


def frame_ocr(config):
    """Recognize only the captured preview image; never resolve or read a video."""
    import cv2
    import numpy as np
    worker_path = str(Path(__file__).resolve().parent)
    if worker_path not in sys.path:
        sys.path.insert(0, worker_path)
    from ocr_backend import create_engine
    check_cancel(config)
    frame = Path(config["frame_image"])
    if not frame.is_file():
        raise ValueError("当前预览帧不存在，请先获取预览")
    image = cv2.imdecode(np.frombuffer(frame.read_bytes(), dtype=np.uint8), cv2.IMREAD_COLOR)
    if image is None:
        raise ValueError("无法读取当前预览帧")
    l, t, r, b = crop_bounds(config, image.shape[1], image.shape[0])
    crop = image[t:b, l:r].copy()
    if crop.shape[1] > 1280:
        crop = cv2.resize(crop, (1280, max(8, round(crop.shape[0] * 1280 / crop.shape[1]))))
    engine, backend, _ = create_engine(config, progress)
    check_cancel(config)
    progress("仅识别当前帧框选区域", 50)
    try:
        result, _ = engine(crop, use_cls=False)
    except Exception as error:
        if config.get("ocr_device", "auto") != "auto" or backend["provider"] == "CPUExecutionProvider":
            raise
        engine, backend, _ = create_engine(dict(config, ocr_device="cpu"), progress)
        backend["gpu_error"] = str(error)
        result, _ = engine(crop, use_cls=False)
    check_cancel(config)
    text, confidence = caption_result(result)
    progress("当前帧识别完成", 100)
    return {"text": text, "confidence": confidence, "backend": backend,
            "crop_pixels": [l, t, r, b], "preview_time": config.get("preview_time", 0),
            "stats": {"sampled_frames": 1, "ocr_calls": 1}, "scope": "current_frame"}


def video_ocr(config):
    import numpy as np
    # Loading by file is also supported by the test/benchmark harness.
    worker_path = str(Path(__file__).resolve().parent)
    if worker_path not in sys.path:
        sys.path.insert(0, worker_path)
    from ocr_backend import create_engine
    from subtitle_segments import (FrameSpan, signature, visual_difference, merge_text_segments,
                                   fill_caption_gaps)

    started = time.perf_counter()
    video = video_input(config)
    width, height, duration = video["width"], video["height"], video["duration"]
    if width <= 0 or height <= 0 or duration <= 0:
        raise ValueError("无法读取视频尺寸或时长，请检查视频流")
    interval = float(config.get("interval", .5))
    change = float(config.get("change_threshold", .22))
    similarity = float(config.get("text_similarity", .6))
    if not .1 <= interval <= 5:
        raise ValueError("采样间隔应为 0.1 至 5 秒")
    if not .05 <= change <= .8 or not .6 <= similarity <= 1:
        raise ValueError("字幕变化或文字相似阈值超出范围")
    init_start = time.perf_counter()
    engine, backend, sessions = create_engine(config, progress)
    init_seconds = time.perf_counter() - init_start
    l, t, r, b = crop_bounds(config, width, height)
    out_width = min(1280, r - l)
    out_height = max(8, round((b - t) * out_width / (r - l)))
    filters = (f"fps=fps=1/{interval}:start_time=0:round=up,"
               f"format=bgr24,crop={r-l}:{b-t}:{l}:{t}:exact=1,scale={out_width}:{out_height}")
    args = [config["ffmpeg"], "-hide_banner", "-nostdin", "-v", "error", *input_args(video),
            "-an", "-vf", filters, "-f", "rawvideo", "-pix_fmt", "bgr24", "pipe:1"]
    segments, span = [], None
    frame_index, last_progress, ocr_calls, ocr_seconds = 0, -1, 0, 0.

    def finish_span(end):
        nonlocal span, engine, backend, sessions, ocr_calls, ocr_seconds
        if span is None:
            return
        try:
            check_cancel(config)
            stamp, image = span.middle()
            text, score = "", 0.
            if image is not None:
                call_start = time.perf_counter()
                try:
                    result, _ = engine(image, use_cls=False)
                except Exception as error:
                    if config.get("ocr_device", "auto") != "auto" or backend["provider"] == "CPUExecutionProvider":
                        raise
                    progress("GPU 推理不可用，改用 CPU 继续当前任务", min(99, round(end / duration * 100)))
                    engine, backend, sessions = create_engine(dict(config, ocr_device="cpu"), progress)
                    backend["gpu_error"] = str(error)
                    result, _ = engine(image, use_cls=False)
                ocr_seconds += time.perf_counter() - call_start
                ocr_calls += 1
                text, score = caption_result(result)
            segments.append({"start": round(span.start, 3), "end": round(end, 3),
                             "text": text, "confidence": score, "representative_time": round(stamp, 3)})
        finally:
            span.close()
            span = None

    error_log = (Path(config["work"]) / "frames.log").open("wb")
    child = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=error_log,
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    frame_bytes = out_width * out_height * 3
    try:
        while True:
            check_cancel(config)
            data = bytearray()
            while len(data) < frame_bytes:
                check_cancel(config)
                chunk = child.stdout.read(frame_bytes - len(data))
                if not chunk:
                    break
                data.extend(chunk)
            if not data:
                break
            if len(data) != frame_bytes:
                raise ValueError("字幕区域帧不完整，请重试")
            stamp = frame_index * interval
            frame_index += 1
            region = np.frombuffer(data, dtype=np.uint8).reshape(out_height, out_width, 3)
            feature = signature(region)
            if span is not None and visual_difference(span.feature, feature) >= change:
                finish_span(min(stamp, duration))
            if span is None:
                span = FrameSpan(stamp, feature)
            span.add(stamp, region)
            percent = min(99, round(stamp / duration * 100))
            if percent != last_progress:
                progress(f"{backend['label']} · 比较字幕画面 {frame_index} 帧，已识别 {ocr_calls} 段", percent)
                last_progress = percent
        if child.wait() != 0:
            raise ValueError("FFmpeg 字幕区域解码失败，请检查视频")
        finish_span(min(duration, frame_index * interval))
    finally:
        if span is not None:
            span.close()
        child.stdout.close()
        if child.poll() is None:
            child.kill()
            child.wait()
        error_log.close()
    check_cancel(config)
    events = merge_text_segments(segments, similarity)
    # Keep removed credits in diagnostics. Never treat ordinary short title-like
    # lyrics as metadata: the full artist/title pair must match at the beginning.
    title, artist = normalized(config.get("song_title", "")), normalized(config.get("song_artist", ""))
    metadata = []
    def credit(event):
        if event["start"] >= min(30, duration * .2):
            return False
        text = event["text"].strip()
        labeled = re.match(r"^(?:作词|填词|作曲|编曲|演唱|词曲|lyrics|composer|music)\s*[:：]", text, re.I)
        pair = title and artist and normalized(text) in {artist+title, title+artist}
        if labeled or pair:
            metadata.append(event)
            return True
        return False
    events = [event for event in events if not credit(event)]
    if not events:
        raise ValueError("选定区域未识别到文字，请调整截取范围或预览时间")
    events = fill_caption_gaps(events, duration)
    if config.get("ocr_profile"):
        backend["profiles"] = [session.end_profiling() for session in sessions]
    stats = {"sampled_frames": frame_index, "visual_segments": len(segments), "ocr_calls": ocr_calls,
             "output_lines": len(events), "filtered_credits": len(metadata), "init_seconds": round(init_seconds, 3),
             "ocr_seconds": round(ocr_seconds, 3), "elapsed_seconds": round(time.perf_counter() - started, 3)}
    progress(f"已完成：{frame_index} 帧仅识别 {ocr_calls} 次，合并为 {len(events)} 句", 100)
    return {"text": "\n".join(e["text"] for e in events), "events": events, "discarded_metadata": metadata,
            "lrc": events_to_lrc(events), "interval": interval, "stats": stats, "backend": backend,
            "change_threshold": change, "text_similarity": similarity,
            "timing_kind": "video_caption",
            "warnings": [f"字幕出现时间受 {interval:g} 秒采样影响；尚未按音频校准。相近时长不代表相同演唱版本。",
                         "双行卡拉 OK 可能同时显示当前句和预告句；须核对演唱顺序，不能直接把两行当作同时演唱。"],
            "source": "视频字幕 OCR · " + backend["language_name"] + " · 字幕出现时间（未按音频校准）"}


def main():
    config = json.loads(Path(sys.argv[1]).read_text("utf-8-sig"))
    os.environ["PATH"] = str(Path(config["ffmpeg"]).parent) + os.pathsep + os.environ.get("PATH", "")
    try:
        check_cancel(config)
        result = {"align": align, "preview": video_preview, "ocr": video_ocr, "ocr_frame": frame_ocr,
                  "lrc": lambda c: {"lrc": c["lrc"], "source": c.get("source", "导入 LRC")}}[config["operation"]](config)
        result["ok"] = True
    except Exception as exc:
        result = {"ok": False, "error": str(exc) or type(exc).__name__}
    Path(config["result"]).write_text(json.dumps(result, ensure_ascii=False, indent=2), "utf-8")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    sys.exit(main())
