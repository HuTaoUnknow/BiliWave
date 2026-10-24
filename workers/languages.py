"""Language selection shared by OCR and existing-lyric alignment."""
from pathlib import Path
import re

OCR_LANGUAGES = {
    "zh": ("中英", None),
    "en": ("中英", None),
    "ru": ("俄语", "cyrillic_PP-OCRv5_rec_mobile.onnx"),
    "it": ("意大利语", "latin_PP-OCRv5_rec_mobile.onnx"),
    "en_zh": ("英汉双语", None),
    "ru_zh": ("俄汉双语", "cyrillic_PP-OCRv5_rec_mobile.onnx"),
    "it_zh": ("意汉双语", "latin_PP-OCRv5_rec_mobile.onnx"),
}


def recognition_options(config):
    language = config.get("ocr_language", "zh")
    if language not in OCR_LANGUAGES:
        raise ValueError("请选择支持的单语或双语字幕模型")
    label, filename = OCR_LANGUAGES[language]
    if filename is None:
        return language, label, {}
    directory = Path(config.get("ocr_models") or Path(__file__).resolve().parents[1] / "models/ocr")
    model = directory / filename
    if not model.is_file():
        raise ValueError(f"缺少{label}字幕模型，请运行 scripts/setup-ocr-languages.ps1")
    return language, label, {"rec_model_path": str(model)}


def script_language(text):
    if re.search(r"[\u3040-\u30ff]", text):
        return "ja"
    if re.search(r"[\u3400-\u9fff]", text):
        return "zh"
    if re.search(r"[\uac00-\ud7af]", text):
        return "ko"
    if re.search(r"[\u0400-\u052f]", text):
        return "ru"
    # Latin script alone cannot distinguish Italian from English. Let the
    # existing multilingual audio model detect the language, or select it in UI.
    return None


def alignment_language(requested, text, model, audio_path):
    if requested != "auto":
        return requested
    known = script_language(text)
    if known:
        return known
    import whisper
    audio = whisper.pad_or_trim(whisper.load_audio(audio_path))
    mel = whisper.log_mel_spectrogram(audio, n_mels=model.dims.n_mels).to(model.device)
    _, probabilities = model.detect_language(mel)
    return max(probabilities, key=probabilities.get)
