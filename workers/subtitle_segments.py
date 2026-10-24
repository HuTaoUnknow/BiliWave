"""Cheap visual grouping before OCR, followed by text-aware caption merging."""
from difflib import SequenceMatcher
import re
import tempfile
import unicodedata

import cv2
import numpy as np


def signature(image):
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    scale = min(1., 640 / gray.shape[1], 160 / gray.shape[0])
    if scale < 1:
        gray = cv2.resize(gray, None, fx=scale, fy=scale, interpolation=cv2.INTER_AREA)
    # Compare glyph geometry rather than RGB values: karaoke color changes alone
    # should not create new lines. One-pixel tolerance absorbs compression noise.
    edges = cv2.Canny(cv2.GaussianBlur(gray, (3, 3), 0), 60, 140) != 0
    tolerant = cv2.dilate(edges.astype(np.uint8), np.ones((3, 3), np.uint8)) != 0
    return edges, tolerant


def visual_difference(a, b):
    if a[0].shape != b[0].shape:
        return 1.
    counts = int(a[0].sum()), int(b[0].sum())
    if min(counts) < 12:
        return 0. if max(counts) < 12 else 1.
    missing_a = np.count_nonzero(a[0] & ~b[1]) / counts[0]
    missing_b = np.count_nonzero(b[0] & ~a[1]) / counts[1]
    return max(missing_a, missing_b)


class FrameSpan:
    """Retain frames for an exact middle sample with bounded RAM, even for a
    whole-song static caption. Compressed overflow is spooled to a temp file."""
    def __init__(self, stamp, feature):
        self.start = stamp
        self.feature = feature
        self.blank = np.count_nonzero(feature[0]) < 12
        self.frames = []
        self.store = tempfile.SpooledTemporaryFile(max_size=8 * 1024 * 1024)

    def add(self, stamp, image):
        if self.blank:
            return
        ok, encoded = cv2.imencode(".jpg", image, [cv2.IMWRITE_JPEG_QUALITY, 97])
        if not ok:
            raise ValueError("无法保存字幕段中间画面")
        offset = self.store.tell()
        self.store.write(encoded.tobytes())
        self.frames.append((stamp, offset, encoded.size))

    def middle(self):
        if self.blank or not self.frames:
            return self.start, None
        stamp, offset, length = self.frames[len(self.frames) // 2]
        self.store.seek(offset)
        image = cv2.imdecode(np.frombuffer(self.store.read(length), dtype=np.uint8), cv2.IMREAD_COLOR)
        if image is None:
            raise ValueError("无法读取字幕段中间画面")
        return stamp, image

    def close(self):
        self.store.close()


def text_key(text):
    return "".join(c.casefold() for c in unicodedata.normalize("NFKC", text) if c.isalnum())


def caption_parts(text):
    foreign, chinese = [], []
    for line in text.splitlines():
        (chinese if re.search(r"[\u3400-\u9fff]", line) else foreign).append(line)
    return text_key(" ".join(foreign)), text_key(" ".join(chinese))


def caption_similarity(first, second):
    a, b = caption_parts(first), caption_parts(second)
    if bool(a[0]) != bool(b[0]) or bool(a[1]) != bool(b[1]):
        return 0.
    return min((SequenceMatcher(None, x, y, autojunk=False).ratio()
                for x, y in zip(a, b) if x or y), default=1.)


def fill_caption_gaps(events, duration):
    """Keep actual detection ends for diagnostics, extend display to the next cue."""
    result = []
    for index, source in enumerate(events):
        event = dict(source)
        event["observed_end"] = source["end"]
        event["end"] = events[index + 1]["start"] if index + 1 < len(events) else duration
        result.append(event)
    return result


def merge_text_segments(segments, similarity=.6):
    """Merge only contiguous similar captions; keep blank gaps and repeated
    choruses. Choose the middle caption, rather than the first/highest score."""
    events, group, anchor = [], [], None

    def finish():
        nonlocal anchor
        if not group:
            return
        center = (group[0]["start"] + group[-1]["end"]) / 2
        # Prefer the span containing the merged midpoint. This also handles
        # unequal span lengths without selecting a short transition image.
        middle = min(group, key=lambda s: (0 if s["start"] <= center < s["end"] else 1,
                                          abs(s["representative_time"] - center)))
        event = dict(middle)
        event["start"], event["end"] = group[0]["start"], group[-1]["end"]
        event["merged_segments"] = len(group)
        events.append(event)
        group.clear()
        anchor = None

    for segment in segments:
        if not text_key(segment["text"]):
            finish()
            continue
        if group:
            close = abs(segment["start"] - group[-1]["end"]) < .01
            # Use the group's original text to prevent gradual fuzzy drift.
            ratio = caption_similarity(anchor["text"], segment["text"])
            # The configured percentage is the only text-merge criterion.
            # Confidence, edit type and span duration must not override it.
            if not close or ratio < similarity:
                finish()
        if not group:
            anchor = segment
        group.append(segment)
    finish()
    return events
