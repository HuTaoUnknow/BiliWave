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


def caption_text(blocks):
    """Group by script, retaining spatial order within each language."""
    original, chinese, other = [], [], []
    for box, text, confidence in blocks:
        text = text.strip()
        if not text:
            continue
        if HAN.search(text):
            chinese.append(text)
        elif FOREIGN.search(text):
            original.append(text)
        else:
            other.append(text)
    if original and chinese:
        return " ".join(original) + "\n" + " ".join(chinese)
    return " ".join(row[1].strip() for row in blocks if row[1].strip())


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
