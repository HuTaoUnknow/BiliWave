"""Run with tools/lyrics-runtime/python.exe; no model download is needed."""
from pathlib import Path
import sys
import unittest

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "workers"))
from ocr_geometry import (foreground, gap_profile, thin_strokes, threshold_pixels,
                          GeometryRecognizer, ThinStrokeEngine, sensitivity)
from ocr_spacing import geometry_mode
from bilingual import caption_text


def glyphs(word_gap=12):
    image = np.zeros((40, 90, 3), np.uint8)
    x = 5
    for gap in (3, word_gap, 3, 0):
        image[8:32, x:x+10] = 255
        x += 10 + gap
    return image


class GeometryTests(unittest.TestCase):
    def test_visible_word_gap_and_sensitivity_monotonicity(self):
        image = glyphs(10)
        self.assertEqual(len(gap_profile(image, 0.)["cuts"]), 0)
        self.assertEqual(len(gap_profile(image, .5)["cuts"]), 1)
        self.assertEqual(len(gap_profile(image, 1.)["cuts"]), 1)
        self.assertEqual(len(gap_profile(glyphs(3), 1.)["cuts"]), 0)
        thresholds = [threshold_pixels(3, 24, q) for q in [0, .25, .5, .75, 1]]
        self.assertEqual(thresholds, sorted(thresholds, reverse=True))

    def test_scale_and_polarity_do_not_change_word_boundaries(self):
        image = glyphs()
        expected = len(gap_profile(image, .5)["cuts"])
        self.assertEqual(expected, 1)
        self.assertEqual(len(gap_profile(255-image, .5)["cuts"]), expected)
        enlarged = cv2.resize(image, None, fx=3, fy=3, interpolation=cv2.INTER_NEAREST)
        self.assertEqual(len(gap_profile(enlarged, .5)["cuts"]), expected)
        self.assertAlmostEqual(gap_profile(enlarged, .5)["threshold"], gap_profile(image, .5)["threshold"]*3)

    def test_internal_model_spaces_are_replaced_by_measured_boundaries(self):
        class Recognizer:
            def __call__(self, crops, boxes=False):
                return [("A B" if i == 0 else "C D", .91) for i in range(len(crops))], .01
        recognizer = GeometryRecognizer(Recognizer(), {"ocr_gap_sensitivity": .5})
        rows, _ = recognizer([glyphs()])
        self.assertEqual(rows, [("AB CD", .91)])
        self.assertEqual(recognizer.last_diagnostics[0]["spaces"], 1)
        self.assertEqual(recognizer.last_diagnostics[0]["normal_gap_px"], 3.)

    def test_geometry_word_positions_keep_source_columns(self):
        class Recognizer:
            def __call__(self, crops, boxes=False):
                return [("AB", .9, [10, [["A", "B"]], [[2, 6]], ["en&num"], [.9, .9]])
                        for _ in crops], .01
        recognizer = GeometryRecognizer(Recognizer(), {"ocr_gap_sensitivity": .5})
        rows, _ = recognizer([glyphs()], True)
        self.assertEqual(rows[0][0], "AB AB")
        columns = rows[0][2][2][0]
        self.assertEqual(len(columns), len(rows[0][0]))
        self.assertEqual(columns, sorted(columns))
        self.assertTrue(all(0 <= x < glyphs().shape[1] for x in columns))

    def test_thin_yi_is_preserved_but_er_san_are_not_single_strokes(self):
        image = np.zeros((80, 160, 3), np.uint8)
        image[39:40, 30:55] = 255
        mask = foreground(image)
        self.assertEqual(len(thin_strokes(mask)), 1)
        image[30:31, 30:55] = 255
        self.assertEqual(thin_strokes(foreground(image)), [])
        self.assertIsNone(foreground(np.zeros_like(image)))

    def test_uncovered_stroke_requires_chinese_model_evidence(self):
        class Recognizer:
            def __init__(self, answer): self.answer = answer
            def __call__(self, crops, boxes=False): return [(self.answer, .9) for _ in crops], .01
        class Engine:
            def __init__(self, answer): self.text_rec = Recognizer(answer)
            def __call__(self, image, **kwargs): return None, None
        image = np.zeros((80, 160, 3), np.uint8)
        image[39:41, 30:55] = 255
        engine = ThinStrokeEngine(Engine("一"), {"ocr_language": "zh"})
        rows, _ = engine(image)
        self.assertEqual(rows[0][1], "一")
        self.assertEqual(engine.last_recovered, 1)
        self.assertIsNone(ThinStrokeEngine(Engine("-"), {"ocr_language": "zh"})(image)[0])
        self.assertIsNone(ThinStrokeEngine(Engine("一"), {"ocr_language": "en"})(image)[0])
        image[39:41, 30:145] = 255
        self.assertIsNone(engine(image)[0])  # A long graphic bar is not 一.

    def test_box_join_uses_same_geometric_sensitivity(self):
        box = lambda x, y, w, h: [[x,y],[x+w,y],[x+w,y+h],[x,y+h]]
        rows = [[box(0, 0, 20, 30), "我", .9], [box(23, 14, 20, 2), "一", .9],
                [box(46, 0, 20, 30), "生", .9]]
        self.assertEqual(caption_text(rows, .5), "我一生")
        words = [[box(0, 0, 20, 30), "Hello", .9], [box(29, 0, 20, 30), "world", .9]]
        self.assertEqual(caption_text(words, 0.), "Helloworld")
        self.assertEqual(caption_text(words, 1.), "Hello world")

    def test_legacy_jobs_keep_old_decoder_while_new_jobs_use_geometry(self):
        self.assertTrue(geometry_mode({}))
        self.assertFalse(geometry_mode({"ocr_space_probability": 1.}))
        self.assertFalse(geometry_mode({"ocr_space_weight": 5.}))
        self.assertTrue(geometry_mode({"ocr_gap_sensitivity": .5, "ocr_space_probability": 1.}))
        self.assertEqual(sensitivity({}), .5)
        for value in [-.1, 1.1, float("nan")]:
            with self.assertRaises(ValueError): sensitivity({"ocr_gap_sensitivity": value})


if __name__ == "__main__":
    unittest.main()
