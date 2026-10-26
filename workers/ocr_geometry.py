"""Measure visible subtitle gaps; preserve thin horizontal Chinese strokes."""
import math
import re

import cv2
import numpy as np

HAN = re.compile(r"[\u3400-\u9fff]")


def sensitivity(config):
    value = float(config.get("ocr_gap_sensitivity", .5))
    if not math.isfinite(value) or not 0 <= value <= 1:
        raise ValueError("空格间距敏感度应为 0 至 100%")
    return value


def threshold_pixels(normal_gap, glyph_height, value):
    # Higher sensitivity lowers BOTH relative and font-normalized requirements.
    return max(2., normal_gap * (3.5 - 2 * value), glyph_height * (.42 - .26 * value))


def foreground(image):
    """Choose foreground polarity without opening/erosion that can erase 一."""
    if image is None or image.size == 0:
        return None
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY) if image.ndim == 3 else image
    if float(gray.max()) - float(gray.min()) < 18:
        return None
    _, binary = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    candidates = []
    for mask in (binary, 255 - binary):
        fraction = np.count_nonzero(mask) / mask.size
        if np.count_nonzero(mask) < 4 or not .00005 <= fraction <= .65:
            continue
        border = np.concatenate((mask[0], mask[-1]))
        candidates.append((np.count_nonzero(border) / border.size + fraction * .2, mask))
    if not candidates:
        return None
    mask = min(candidates, key=lambda entry: entry[0])[1]
    count, labels, stats, _ = cv2.connectedComponentsWithStats(mask, 8)
    keep = np.zeros(count, np.uint8)
    for label in range(1, count):
        x, y, w, h, area = stats[label]
        # Area only: a long one-pixel horizontal stroke is valid foreground.
        if area >= max(2, round(image.shape[0] * .025)) and not (
                w > image.shape[1] * .95 and h > image.shape[0] * .95):
            keep[label] = 255
    clean = keep[labels]
    return clean if np.any(clean) else None


def gap_profile(image, value):
    mask = foreground(image)
    if mask is None:
        return {"valid": False, "cuts": [], "mask": mask}
    occupied = np.any(mask != 0, axis=0)
    changes = np.diff(np.r_[False, occupied, False].astype(np.int8))
    starts, ends = np.flatnonzero(changes == 1), np.flatnonzero(changes == -1)
    ys = np.flatnonzero(np.any(mask, axis=1))
    glyph_height = max(4., float(ys[-1] - ys[0] + 1))
    gaps = starts[1:] - ends[:-1]
    if len(gaps) >= 3:
        normal = float(np.median(gaps[gaps <= np.percentile(gaps, 60)]))
    else:
        normal = min(float(np.median(gaps)) if len(gaps) else 0., glyph_height * .10)
    threshold = threshold_pixels(normal, glyph_height, value)
    cuts = [(int(left), int(right)) for left, right in zip(ends[:-1], starts[1:])
            if right - left > threshold]
    return {"valid": True, "cuts": cuts, "mask": mask, "normal_gap": normal,
            "glyph_height": glyph_height, "threshold": threshold,
            "gaps": [int(gap) for gap in gaps]}


def thin_strokes(mask, glyph_height=None):
    if mask is None:
        return []
    count, _, stats, _ = cv2.connectedComponentsWithStats(mask, 8)
    components = [tuple(map(int, row)) for row in stats[1:]]
    tall = [h for _, _, w, h, _ in components if h >= 6 and h > w / 4]
    height = float(glyph_height or (np.median(tall) if tall else 0))
    result = []
    for x, y, w, h, area in components:
        em = height or w * 1.12
        if not (w >= 8 and w >= 4 * h and .50 * em <= w <= 1.35 * em
                and h <= .23 * em and area / (w * h) >= .55):
            continue
        # 二/三 and crossbars inside other glyphs are not independent 一.
        surrounding = mask[max(0, round(y - em * .55)):min(mask.shape[0], round(y + h + em * .55)), x:x+w]
        if np.count_nonzero(surrounding) > area * 1.25:
            continue
        result.append((x, y, w, h))
    return result


def stroke_patch(image, bounds, ratio):
    x, y, w, h = bounds
    patch = image[y:y+h, x:x+w]
    height = max(h + 8, round(w * ratio))
    top = (height - h) // 2
    background = tuple(int(v) for v in np.median(image.reshape(-1, 3), axis=0))
    patch = cv2.copyMakeBorder(patch, top, height-h-top, 4, 4, cv2.BORDER_CONSTANT, value=background)
    if w < 32:
        patch = cv2.resize(patch, None, fx=2, fy=2, interpolation=cv2.INTER_CUBIC)
    return patch


def verify_strokes(recognizer, image, bounds):
    patches = [stroke_patch(image, box, ratio) for box in bounds for ratio in (.5, .7, 1.)]
    if not patches:
        return [], 0.
    checks, elapsed = recognizer(patches, False)
    verified = []
    for index in range(len(bounds)):
        votes = [row for row in checks[3*index:3*index+3] if row[0].strip() == "一"]
        verified.append(max((float(row[1]) for row in votes), default=0.) if len(votes) >= 2 else 0.)
    return verified, elapsed


class GeometryRecognizer:
    def __init__(self, recognizer, config):
        self.recognizer = recognizer
        self.value = sensitivity(config)
        self.language = config.get("ocr_language", "zh")
        self.last_diagnostics = []

    def __getattr__(self, name):
        return getattr(self.recognizer, name)

    def __call__(self, crops, return_word_box=False):
        if isinstance(crops, np.ndarray):
            crops = [crops]
        pieces, mappings, profiles = [], [], []
        for image in crops:
            profile = gap_profile(image, self.value)
            profiles.append(profile)
            # Keep every nonblank pixel, including thin horizontal strokes.
            borders = [0] + [(left + right) // 2 for left, right in profile["cuts"]] + [image.shape[1]]
            mapping = []
            for left, right in zip(borders, borders[1:]):
                if right > left:
                    mapping.append(len(pieces))
                    pieces.append(image[:, left:right].copy())
            mappings.append(mapping)
        if not pieces:
            return [], 0.
        recognized, elapsed = self.recognizer(pieces, return_word_box)
        output, diagnostics = [], []
        for image, profile, mapping in zip(crops, profiles, mappings):
            rows = [recognized[index] for index in mapping]
            if profile["valid"]:
                words = [re.sub(r"\s+", "", row[0]) for row in rows]
                text = " ".join(word for word in words if word)
            else:
                text = " ".join(row[0] for row in rows if row[0])
            # Geometric spaces do not pretend to be model confidence samples.
            confidence = float(np.average([row[1] for row in rows],
                               weights=[max(1, len(row[0].strip())) for row in rows])) if rows else 0.
            repaired = False
            if HAN.search(text) and self.language in {"zh", "en", "en_zh"}:
                candidates = thin_strokes(profile["mask"])
                marks = [index for index, char in enumerate(text) if char in "一—-"]
                if candidates and len(candidates) == len(marks) and any(text[index] != "一" for index in marks):
                    verified, check_time = verify_strokes(self.recognizer, image, sorted(candidates))
                    elapsed += check_time
                    corrected = list(text)
                    for mark, score in zip(marks, verified):
                        if corrected[mark] != "一" and score >= .70:
                            corrected[mark], repaired = "一", True
                            confidence = min(confidence, score)
                    text = "".join(corrected)
                if len(candidates) > text.count("一"):
                    mask = profile["mask"]
                    enhanced = cv2.cvtColor(mask, cv2.COLOR_GRAY2BGR)
                    retry, retry_time = self.recognizer([enhanced], return_word_box)
                    elapsed += retry_time
                    alternative = re.sub(r"\s+", "", retry[0][0])
                    original = re.sub(r"\s+", "", text)
                    if (alternative.replace("一", "") == original.replace("一", "")
                            and original.count("一") < alternative.count("一") <= len(candidates)
                            and retry[0][1] >= max(.55, confidence - .10)):
                        # Keep the measured gaps when the only recognition change
                        # is restoration of image-supported 一 characters.
                        if len(mapping) == 1:
                            text, confidence, repaired = alternative, float(retry[0][1]), True
                            recognized[mapping[0]] = retry[0]
            if return_word_box:
                # Preserve native CTC positions and map each subcrop back into
                # source-image pixel columns instead of inventing uniform boxes.
                chars, columns, probabilities = [], [], []
                left = 0
                for piece_index in mapping:
                    row = recognized[piece_index]
                    count, words, positions, _, scores = row[2]
                    width = pieces[piece_index].shape[1]
                    flat = [(char, column) for word, word_columns in zip(words, positions)
                            for char, column in zip(word, word_columns)]
                    if chars and row[0].strip():
                        chars.append(" "); columns.append(max(0, left-1)); probabilities.append(confidence)
                    for (char, column), score in zip(flat, scores):
                        if profile["valid"] and char.isspace():
                            continue
                        chars.append(char); columns.append(left + round(column * width / max(1, count)))
                        probabilities.append(score)
                    left += width
                # The only character replacements above preserve column order.
                if len(chars) == len(text):
                    chars = list(text)
                output.append((text, confidence, [image.shape[1], [chars], [columns],
                                                  ["en&num"], probabilities]))
            else:
                output.append((text, confidence))
            diagnostics.append({"valid": profile["valid"], "sensitivity": self.value,
                                "threshold_px": profile.get("threshold"),
                                "normal_gap_px": profile.get("normal_gap"),
                                "measured_gaps_px": profile.get("gaps", []),
                                "spaces": text.count(" "), "candidate_gaps": max(0, len(mapping)-1),
                                "yi_restored": repaired})
        self.last_diagnostics = diagnostics
        return output, elapsed


class ThinStrokeEngine:
    """Rescue uncovered 一-shaped boxes with independent Chinese-model evidence."""
    def __init__(self, engine, config):
        self.engine = engine
        self.language = config.get("ocr_language", "zh")
        self.last_recovered = 0

    def __getattr__(self, name):
        return getattr(self.engine, name)

    def __call__(self, image, **kwargs):
        if not isinstance(image, np.ndarray):
            image = self.engine.load_img(image)
        result, times = self.engine(image, **kwargs)
        self.last_recovered = 0
        if not kwargs.get("use_det", getattr(self.engine, "use_det", True)) or not kwargs.get(
                "use_rec", getattr(self.engine, "use_rec", True)):
            return result, times
        if self.language not in {"zh", "en_zh", "ru_zh", "it_zh"}:
            return result, times
        rows = list(result or [])
        mask = foreground(image)
        candidates = thin_strokes(mask)
        uncovered = []
        for bounds in candidates:
            x, y, w, h = bounds
            if w > image.shape[1] * .6 or w > image.shape[0] * 1.3:
                continue
            center = (x+w/2, y+h/2)
            if any(cv2.pointPolygonTest(np.asarray(row[0], np.float32), center, False) >= 0 for row in rows):
                continue
            uncovered.append(bounds)
        if not uncovered:
            return result, times
        recognizer = getattr(self.engine, "chinese_rec", self.engine.text_rec)
        recognizer = getattr(recognizer, "recognizer", recognizer)
        if hasattr(recognizer, "original"):
            return result, times
        verified, elapsed = verify_strokes(recognizer, image, uncovered)
        for index, (x, y, w, h) in enumerate(uncovered):
            if verified[index] >= .70:
                box = [[x, y], [x+w, y], [x+w, y+h], [x, y+h]]
                rows.append([box, "一", verified[index]])
                self.last_recovered += 1
        if times is not None:
            times = list(times)
            times[-1] += elapsed
        return rows or None, times


def diagnostics(engine):
    recognizer = getattr(engine, "text_rec", None)
    if hasattr(recognizer, "original"):
        rows = {"original": getattr(recognizer.original, "last_diagnostics", []),
                "chinese": getattr(recognizer.chinese, "last_diagnostics", [])}
    else:
        rows = {"rows": getattr(recognizer, "last_diagnostics", [])}
    rows["thin_strokes_recovered"] = getattr(engine, "last_recovered", 0)
    return rows
