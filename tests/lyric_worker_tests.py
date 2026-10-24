"""Run with the app's private Python. Heavy model checks use --live."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'workers'))
spec = importlib.util.spec_from_file_location("lyric_worker", ROOT / "workers/lyric_worker.py")
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)


class LyricWorkerTests(unittest.TestCase):
    def test_unified_space_probability_is_neutral_at_half_and_monotonic(self):
        import numpy as np
        from rapidocr_onnxruntime.ch_ppocr_rec.utils import CTCLabelDecode
        from ocr_spacing import SpaceAwareDecode, spacing_options, appearance_probability
        decoder = CTCLabelDecode(character=['A', 'B'])
        predictions = np.array([[[.01, .97, .01, .01], [.8, 0, 0, .2],
                                 [.01, .01, .97, .01]]], dtype=np.float32)
        neutral = SpaceAwareDecode(decoder, {'ocr_space_probability': .5})
        self.assertEqual(neutral(predictions), decoder(predictions))
        boosted = SpaceAwareDecode(decoder, {'ocr_space_probability': .8})
        self.assertEqual(boosted(predictions)[0][0], 'A B')
        suppressed = SpaceAwareDecode(decoder, {'ocr_space_probability': .2})
        self.assertEqual(suppressed(predictions)[0][0], 'AB')
        predictions[0, 1] = [.01, .01, .01, .97]
        disabled = SpaceAwareDecode(decoder, {'ocr_space_probability': 0})
        self.assertEqual(disabled(predictions)[0][0], 'AB')
        weights = [spacing_options({'ocr_space_probability': value})[0]
                   for value in [0, .1, .3, .5, .7, .9, 1]]
        self.assertEqual(weights, sorted(weights))
        self.assertEqual(weights[-1], 20.)
        self.assertEqual(appearance_probability({}), .5)
        self.assertAlmostEqual(appearance_probability({'ocr_space_weight': 5}),
                               .5 + .5 * __import__('math').log(5) / __import__('math').log(20))
        for value in [-.1, 1.01, float('nan'), float('inf')]:
            with self.assertRaises(ValueError):
                spacing_options({'ocr_space_probability': value})

    def test_space_decoding_uses_model_probability_and_retains_raw_confidence(self):
        import numpy as np
        from rapidocr_onnxruntime.ch_ppocr_rec.utils import CTCLabelDecode
        from ocr_spacing import SpaceAwareDecode, spacing_options
        decoder = CTCLabelDecode(character=['A', 'B'])
        predictions = np.array([[[.01, .97, .01, .01], [.8, 0, 0, .2],
                                 [.01, .01, .97, .01], [.999, 0, 0, .001],
                                 [.01, .97, .01, .01]]], dtype=np.float32)
        original = predictions.copy()
        default = SpaceAwareDecode(decoder, {})
        self.assertEqual(default(predictions), decoder(predictions))
        self.assertEqual(default(predictions)[0][0], 'ABA')
        boosted = SpaceAwareDecode(decoder, {'ocr_space_weight': 5})
        text, confidence = boosted(predictions)[0]
        self.assertEqual(text, 'A BA')
        self.assertAlmostEqual(confidence, (.97 + .2 + .97 + .97) / 4, places=5)
        np.testing.assert_array_equal(predictions, original)
        threshold = SpaceAwareDecode(decoder, {'ocr_space_weight': 5, 'ocr_space_min_probability': .3})
        self.assertEqual(threshold(predictions)[0][0], 'ABA')
        box = boosted(predictions, True, wh_ratio_list=[2.], max_wh_ratio=2.)
        self.assertEqual(box[0][0], 'A BA')
        for config in ({'ocr_space_weight': 0}, {'ocr_space_weight': float('nan')},
                       {'ocr_space_min_probability': 1.1}):
            with self.assertRaises(ValueError):
                spacing_options(config)

    def test_single_frame_uses_only_snapshot_crop_and_does_not_resolve_video(self):
        import cv2
        import numpy as np
        with tempfile.TemporaryDirectory() as directory:
            image = np.zeros((100, 200, 3), dtype=np.uint8)
            image[50:90, 20:180] = (50, 100, 150)
            path = Path(directory) / 'snapshot.png'
            path.write_bytes(cv2.imencode('.png', image)[1].tobytes())
            config = {'frame_image': str(path), 'crop': [.1, .5, .8, .4],
                      'cancel': str(Path(directory) / 'cancel'), 'video': 'must-not-fetch'}
            calls = []
            def engine(crop, **options):
                calls.append(crop.copy())
                self.assertEqual(options, {'use_cls': False})
                return [[[[0, 0], [160, 0], [160, 40], [0, 40]], 'Привет мир', .95]], []
            with patch('ocr_backend.create_engine', return_value=(engine, {'provider': 'CPUExecutionProvider'}, [])), \
                 patch.object(worker, 'video_input', side_effect=AssertionError('Video access forbidden')), \
                 patch.object(worker, 'run', side_effect=AssertionError('FFmpeg forbidden')):
                result = worker.frame_ocr(config)
            self.assertEqual(len(calls), 1)
            np.testing.assert_array_equal(calls[0], image[50:90, 20:180])
            self.assertEqual(result['text'], 'Привет мир')
            self.assertEqual(result['crop_pixels'], [20, 50, 180, 90])
            self.assertEqual(result['stats'], {'sampled_frames': 1, 'ocr_calls': 1})
            self.assertNotIn('lrc', result)

    def test_bilingual_caption_pairing_and_lrc_roundtrip(self):
        from bilingual import caption_text, alignment_pairs, DualRecognizer
        box = [[0, 0], [200, 0], [200, 20], [0, 20]]
        text = caption_text([[box, 'Привет, мир!', .99], [box, '你好，世界！', .98]])
        self.assertEqual(text, 'Привет, мир!\n你好，世界！')
        self.assertEqual(caption_text([[box, '只有中文', .99]]), '只有中文')
        self.assertEqual(worker.events_to_lrc([dict(start=1, end=3, text=text)]),
                         '[00:01.00]Привет, мир!\n[00:01.00]你好，世界！')
        originals, translations = alignment_pairs(['Hello!', '你好！', 'A new day.', '新的一天。'])
        self.assertEqual(originals, ['Hello!', 'A new day.'])
        self.assertEqual(translations, {0: '你好！', 1: '新的一天。'})
        self.assertEqual(alignment_pairs(['Hello!', '你好！', 'Unpaired'])[1], {})
        class Recognizer:
            session = None
            def __init__(self, values): self.values = values
            def __call__(self, *_): return self.values, .1
        recognizer = DualRecognizer(Recognizer([('Привет', .99), ('XXX', .4)]),
                                   Recognizer([('幻觉', .6), ('你好', .98)]))
        self.assertEqual(recognizer([None, None])[0], [('Привет', .99), ('你好', .98)])

    def test_language_selection_and_auto_detection(self):
        from languages import script_language, alignment_language
        self.assertEqual(script_language('Всё начинается с мечты.'), 'ru')
        self.assertEqual(script_language('中文歌词'), 'zh')
        self.assertEqual(script_language('こんにちは'), 'ja')
        self.assertEqual(script_language('안녕하세요'), 'ko')
        self.assertIsNone(script_language('È già una bella città.'))
        self.assertEqual(alignment_language('it', 'Testo', None, ''), 'it')
        self.assertEqual(alignment_language('auto', 'Русская песня', None, ''), 'ru')
        mel = SimpleNamespace(to=lambda _: 'mel')
        detector = SimpleNamespace(dims=SimpleNamespace(n_mels=80), device='cpu',
                                   detect_language=lambda value: (None, {'it': .98, 'en': .02}))
        fake_whisper = SimpleNamespace(load_audio=lambda _: 'audio', pad_or_trim=lambda a: a,
                                       log_mel_spectrogram=lambda *a, **k: mel)
        with patch.dict(sys.modules, {'whisper': fake_whisper}):
            self.assertEqual(alignment_language('auto', 'Una canzone', detector, 'audio.wav'), 'it')

    def test_language_models_are_explicit_and_missing_files_are_reported(self):
        from languages import recognition_options
        self.assertEqual(recognition_options({})[0], 'zh')
        for language in ['ru', 'it']:
            selected, _, options = recognition_options({'ocr_language': language})
            self.assertEqual(selected, language)
            self.assertTrue(Path(options['rec_model_path']).is_file())
            with tempfile.TemporaryDirectory() as directory:
                with self.assertRaisesRegex(ValueError, '缺少'):
                    recognition_options({'ocr_language': language, 'ocr_models': directory})
        with self.assertRaises(ValueError):
            recognition_options({'ocr_language': 'unsupported'})

    def test_visual_comparison_ignores_color_and_small_noise(self):
        import cv2
        import numpy as np
        from subtitle_segments import signature, visual_difference
        a = np.zeros((100, 500, 3), np.uint8)
        cv2.putText(a, 'FIRST CAPTION', (20, 65), cv2.FONT_HERSHEY_SIMPLEX, 1.2, (255, 255, 255), 2)
        colored = a.copy()
        colored[:, :, 0] = 0
        b = np.zeros_like(a)
        cv2.putText(b, 'A DIFFERENT LINE', (20, 65), cv2.FONT_HERSHEY_SIMPLEX, 1.2, (255, 255, 255), 2)
        self.assertLess(visual_difference(signature(a), signature(colored)), .22)
        self.assertGreater(visual_difference(signature(a), signature(b)), .22)
        self.assertEqual(visual_difference(signature(a), signature(np.zeros_like(a))), 1.)

    def test_middle_frame_is_selected_instead_of_first_or_last(self):
        import numpy as np
        from subtitle_segments import FrameSpan
        edge = np.ones((10, 10), dtype=bool)
        span = FrameSpan(0, (edge, edge))
        try:
            for index in range(7):
                span.add(index * .5, np.full((20, 60, 3), index * 30, dtype=np.uint8))
            stamp, frame = span.middle()
            self.assertEqual(stamp, 1.5)
            self.assertLess(abs(float(frame.mean()) - 90), 2)
        finally:
            span.close()

    def test_fuzzy_merge_keeps_middle_text_and_blank_boundaries(self):
        from subtitle_segments import merge_text_segments
        def segment(start, end, text):
            return dict(start=start, end=end, text=text, confidence=.9, representative_time=(start+end)/2)
        first, middle = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ', 'ABCDEFGHI1KLMNOPQRSTUVWXYZ'
        result = merge_text_segments([segment(0, 1, first), segment(1, 5, middle), segment(5, 6, first),
                                      segment(6, 7, ''), segment(7, 9, first)])
        self.assertEqual(len(result), 2)
        self.assertEqual(result[0]['text'], middle)
        self.assertEqual((result[0]['start'], result[0]['end']), (0, 6))
        self.assertEqual(result[0]['representative_time'], 3)
        self.assertEqual(result[1]['start'], 7)
        self.assertEqual(result[0]['merged_segments'], 3)

    def test_similar_text_does_not_chain_into_different_lyrics(self):
        from subtitle_segments import merge_text_segments
        texts = ['AAAAAAAAAA', 'AAAAAAAABB', 'AAAAAABBBB', 'AAAABBBBBB']
        segments = [dict(start=i, end=i+1, text=t, confidence=.9, representative_time=i+.5)
                    for i, t in enumerate(texts)]
        result = merge_text_segments(segments, .75)
        self.assertEqual(len(result), 2)

    def test_bilingual_merge_uses_percentage_regardless_of_confidence(self):
        from subtitle_segments import caption_similarity, merge_text_segments
        chinese = '这是保持不变但非常长的一句中文翻译' * 8
        first, changed = 'Hello world\n' + chinese, 'Another day\n' + chinese
        self.assertLess(caption_similarity(first, changed), .94)
        self.assertEqual(caption_similarity('Hello, WORLD!', 'hello world'), 1.)
        def segment(start, text, confidence):
            return dict(start=start, end=start+1, text=text, confidence=confidence,
                        representative_time=start+.5)
        original = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'
        changed = 'ABCDEFGHI1KLMNOPQRSTUVWXYZ'
        self.assertGreater(caption_similarity(original, changed), .94)
        self.assertEqual(len(merge_text_segments([segment(0, original, .99), segment(1, changed, .99)])), 1)
        self.assertEqual(len(merge_text_segments([segment(0, original, .8), segment(1, changed, .8)])), 1)
        self.assertEqual(len(merge_text_segments([segment(0, 'ABCDEFGHIJ', .8), segment(1, 'ABCDEFGxyz', .8)])), 1)
        self.assertEqual(len(merge_text_segments([segment(0, 'ABCDEFGHIJ', .8), segment(1, 'ABCDEFGxyz', .8)], .94)), 2)
        # Identical text after a blank remains a new occurrence (e.g. a chorus).
        events = merge_text_segments([segment(0, 'Again', .99), segment(1, '', 0), segment(2, 'Again', .99)])
        self.assertEqual(len(events), 2)

    def test_missing_and_replaced_characters_both_follow_percentage(self):
        from subtitle_segments import merge_text_segments
        def segment(start, end, text):
            return dict(start=start, end=end, text=text, confidence=.98, representative_time=(start+end)/2)
        partial = 'Рацветали яблони и груши\n苹果树和梨树的花朵绽放'
        full = 'Расцветали яблони и груши\n苹果树和梨树的花朵绽放'
        events = merge_text_segments([segment(9, 9.5, partial), segment(9.5, 11, full),
                                      segment(11, 13, full)])
        self.assertEqual(len(events), 1)
        self.assertEqual((events[0]['start'], events[0]['end'], events[0]['text']), (9, 13, full))
        # Neither high scores, substitutions nor longer spans veto the percentage.
        self.assertEqual(len(merge_text_segments([segment(0, .5, 'ABCDEFGHIJ'),
                                                 segment(.5, 3, 'ABCDEFxHIJ')])), 1)
        self.assertEqual(len(merge_text_segments([segment(0, 2, partial), segment(2, 4, full)])), 1)
        self.assertEqual(len(merge_text_segments([segment(0, 1, full),
                           segment(1, 3, 'Расцветали яблони и груши\n另一句不同的翻译')])), 2)

    def test_percentage_boundary_and_actual_marmalade_substitutions(self):
        from subtitle_segments import merge_text_segments, caption_similarity
        def segment(start, end, text, confidence):
            return dict(start=start, end=end, text=text, confidence=confidence,
                        representative_time=(start+end)/2)
        for confidence in [0., .8, .95, 1.]:
            pair = [segment(0, .5, 'ABCDEFGHIJ', confidence), segment(.5, 3, 'ABCDEFxyzw', confidence)]
            self.assertEqual(caption_similarity(pair[0]['text'], pair[1]['text']), .6)
            self.assertEqual(len(merge_text_segments(pair, .6)), 1)
            self.assertEqual(len(merge_text_segments(pair, .600001)), 2)
        pair = [segment(126.5, 127, 'Отятьмнекажется,чтокружитсяголоаа\n我又开始头晕了', .98393),
                segment(127, 129.5, 'Опятьмнекажется,чтокружитсяголова\n我又开始头晕了', .98372)]
        merged = merge_text_segments(pair, .6)
        self.assertEqual(len(merged), 1)
        self.assertEqual((merged[0]['start'], merged[0]['end'], merged[0]['text']),
                         (126.5, 129.5, pair[1]['text']))
        pair = [segment(146.5, 147, 'Моймармеладный,янеправа\n我的软糖，我错了', .98380),
                segment(147, 153, 'Моймармеладный,янетрава\n我的软糖，我错了', .98516)]
        self.assertEqual(len(merge_text_segments(pair, .6)), 1)
        self.assertEqual(len(merge_text_segments(pair, .96)), 2)

    def test_same_version_timing_hints_validate_occurrences_and_bilingual_text(self):
        from alignment_quality import timing_hints
        lines = ['Hello world', 'Hello world']
        text = '[00:01.00]Hello world\n[00:01.00]你好世界\n[01:10.00]Hello world\n[01:10.00]你好世界'
        hints = timing_hints({'timed_text': text}, lines, 100)
        self.assertEqual([h['start'] for h in hints], [1, 70])
        self.assertEqual(timing_hints({}, lines, 100), [])
        with self.assertRaisesRegex(ValueError, '行数'):
            timing_hints({'timed_text': '[00:01.00]Hello world'}, lines, 100)
        for invalid in ['[00:01.00]Different verse\n[01:10.00]Hello world',
                        '[01:10.00]Hello world\n[00:01.00]Hello world',
                        '[00:01.00]Hello world\n[02:10.00]Hello world']:
            with self.assertRaisesRegex(ValueError, '不对应'):
                timing_hints({'timed_text': invalid}, lines, 100)

    def test_alignment_review_flags_long_instrumental_and_collapsed_words(self):
        from alignment_quality import review_reasons
        self.assertTrue(review_reasons(dict(start=19.7, end=36.08, words=[])))
        self.assertTrue(review_reasons(dict(start=66.38, end=66.38, words=[])))
        self.assertTrue(review_reasons(dict(start=2, end=5, words=[dict(start=2, end=5, probability=.2)])))
        self.assertEqual(review_reasons(dict(start=2, end=5, words=[dict(start=2, end=5, probability=.95)])), [])

    def test_gpu_mode_does_not_silently_accept_cpu_only_runtime(self):
        import onnxruntime
        from ocr_backend import create_engine
        with patch.object(onnxruntime, 'get_available_providers', return_value=['CPUExecutionProvider']):
            with self.assertRaisesRegex(ValueError, 'GPU'):
                create_engine({'ocr_device': 'gpu'}, lambda *_: None)

    def test_text_preserves_numbers_and_repeated_chorus(self):
        self.assertEqual(worker.plain_lines("[ti:歌曲]\n[00:01.20]同一句\n[00:05.00]123\n[00:07.00]同一句"),
                         ["同一句", "123", "同一句"])
        self.assertEqual(worker.plain_lines("1\n00:00:01,000 --> 00:00:02,000\nHello!\n\n2\n00:00:04,000 --> 00:00:05,000\nAgain"), ["Hello!", "Again"])

    def test_crop_is_lower_third_and_validated(self):
        self.assertEqual(worker.crop_bounds({}, 1920, 1080), (0, 720, 1920, 1080))
        with self.assertRaises(ValueError):
            worker.crop_bounds({"crop": [0, .8, 1, .4]}, 1920, 1080)

    def test_ocr_does_not_erase_repeated_chorus(self):
        result = worker.collect_ocr_events([(0, "Hello!", .9), (.5, "Hello!", .95), (1, "", 0),
                                            (2, "Hello!", .9), (3, "Next line", .9)])
        self.assertEqual([r["text"] for r in result], ["Hello!", "Hello!", "Next line"])
        self.assertEqual(result[1]["start"], 2)
        self.assertEqual(result[0]["end"], 1)

    def test_subtitle_display_holds_previous_line_without_blank_cues(self):
        events = worker.collect_ocr_events([(0, "First", .9), (.5, "First", .9),
                                            (1, "", 0), (1.5, "Second", .9)], 1.8)
        self.assertEqual(events[0]["end"], 1)
        self.assertEqual(events[1]["end"], 1.8)
        from subtitle_segments import fill_caption_gaps
        events = fill_caption_gaps(events, 1.8)
        self.assertEqual(events[0]['observed_end'], 1)
        self.assertEqual(events[0]['end'], 1.5)
        self.assertEqual(events[1]['end'], 1.8)
        self.assertEqual(worker.events_to_lrc(events), "[00:00.00]First\n[00:01.50]Second")

    def test_cancel_does_not_start_work(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "cancel"
            path.touch()
            with self.assertRaises(worker.Cancelled):
                worker.check_cancel({"cancel": str(path)})

    def test_remote_video_resolves_without_full_download(self):
        with tempfile.TemporaryDirectory() as temp:
            config = {"video": "https://www.bilibili.com/video/BV13x41117TL?p=2",
                      "work": temp, "ytdlp": "yt-dlp", "cancel": str(Path(temp) / "cancel")}
            info = {"url": "https://cdn.example/video.m4s", "width": 854, "height": 480,
                    "duration": 60, "http_headers": {"Referer": "https://www.bilibili.com/"}}
            with patch.object(worker, "capture_json", return_value=info) as resolve:
                result = worker.video_input(config)
                args = resolve.call_args[0][1]
                self.assertIn("--skip-download", args)
                self.assertIn("--dump-single-json", args)
                self.assertNotIn("-o", args)
                self.assertEqual(result["url"], info["url"])
                self.assertEqual(list(Path(temp).glob("*.video")), [])
                self.assertIn("-headers", worker.input_args(result))

    def test_failed_metadata_does_not_leave_a_cached_result(self):
        with tempfile.TemporaryDirectory() as temp:
            config = {"work": temp, "cancel": str(Path(temp) / "cancel")}
            with self.assertRaises(ValueError):
                worker.capture_json(config, [sys.executable, "-c", "raise SystemExit(1)"], "stream.json")
            self.assertFalse((Path(temp) / "stream.json").exists())
            self.assertFalse((Path(temp) / "stream.json.pending").exists())


if __name__ == "__main__":
    unittest.main()
