"""Keep simultaneous translations together, while detecting each frame only once."""
import re

HAN = re.compile(r"[\u3400-\u9fff]")
FOREIGN = re.compile(r"[A-Za-z\u00c0-\u024f\u0400-\u052f]")


class DualRecognizer:
    def __init__(self, original, chinese):
        self.original, self.chinese = original, chinese
        self.session = original.session

    def __call__(self, crops, return_word_box=False):
        foreign, first_time = self.original(crops, return_word_box)
        chinese, second_time = self.chinese(crops, return_word_box)
        result = []
        for a, b in zip(foreign, chinese):
            # Both models see exactly the same detected text box. Require Han
            # evidence and a winning score before choosing the Chinese model.
            result.append(b if HAN.search(b[0]) and b[1] >= a[1] else a)
        return result, first_time + second_time


def caption_text(blocks, gap_sensitivity=None):
    """Group by script, retaining spatial order within each language."""
    original, chinese, other = [], [], []
    for box, text, confidence in blocks:
        text = text.strip()
        if not text:
            continue
        if HAN.search(text):
            chinese.append((box, text, confidence))
        elif FOREIGN.search(text):
            original.append((box, text, confidence))
        else:
            other.append((box, text, confidence))
    if original and chinese:
        return join_blocks(original, gap_sensitivity) + "\n" + join_blocks(chinese, gap_sensitivity)
    return join_blocks(blocks, gap_sensitivity)


def join_blocks(blocks, value):
    if value is None:
        return " ".join(row[1].strip() for row in blocks if row[1].strip())
    from ocr_geometry import threshold_pixels
    # Thin 一 boxes have a different top edge but share the same row center.
    lines = []
    for row in sorted(blocks, key=lambda row: sum(p[1] for p in row[0]) / 4):
        box, text, _ = row
        if not text.strip():
            continue
        top, bottom = min(p[1] for p in box), max(p[1] for p in box)
        center, height = (top + bottom) / 2, max(1, bottom - top)
        line = next((entry for entry in lines if abs(entry[0]-center) <= .55 * max(entry[1], height)), None)
        if line is None:
            lines.append([center, height, [row]])
        else:
            line[1] = max(line[1], height)
            line[2].append(row)
    output = []
    for _, height, rows in lines:
        text, previous = "", None
        for box, part, _ in sorted(rows, key=lambda row: min(p[0] for p in row[0])):
            separator = ""
            if previous is not None:
                gap = min(p[0] for p in box) - max(p[0] for p in previous)
                if gap > threshold_pixels(height * .08, height, value):
                    separator = " "
            text += separator + part.strip()
            previous = box
        output.append(text)
    return " ".join(output)


def alignment_pairs(lines):
    """Only pair uniformly alternating foreign/Chinese text; never guess verses."""
    if len(lines) < 2 or len(lines) % 2:
        return lines, {}
    originals, translations = [], {}
    for i in range(0, len(lines), 2):
        a, b = lines[i:i + 2]
        if HAN.search(a) and FOREIGN.search(b) and not HAN.search(b):
            a, b = b, a
        if HAN.search(a) or not FOREIGN.search(a) or not HAN.search(b):
            return lines, {}
        translations[len(originals)] = b
        originals.append(a)
    return originals, translations
