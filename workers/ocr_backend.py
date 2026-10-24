"""Project-local GPU OCR; no system Python or driver changes are required."""
import os
from pathlib import Path
import sys


def graphics_adapters():
    """DirectML device_id follows DXGI order, which may put an iGPU first."""
    if sys.platform != "win32":
        return []
    import ctypes as c
    import uuid

    class Luid(c.Structure):
        _fields_ = [("low", c.c_uint32), ("high", c.c_int32)]

    class Description(c.Structure):
        _fields_ = [("name", c.c_wchar * 128), ("vendor", c.c_uint32),
                    ("device", c.c_uint32), ("subsystem", c.c_uint32), ("revision", c.c_uint32),
                    ("video_memory", c.c_size_t), ("system_memory", c.c_size_t),
                    ("shared_memory", c.c_size_t), ("luid", Luid), ("flags", c.c_uint32)]

    def method(obj, index, restype, *args):
        table = c.cast(obj, c.POINTER(c.POINTER(c.c_void_p))).contents
        return c.WINFUNCTYPE(restype, c.c_void_p, *args)(table[index])

    factory = c.c_void_p()
    guid = c.create_string_buffer(uuid.UUID("770aae78-f26f-4dba-a829-253c83d1b387").bytes_le)
    create = c.WinDLL("dxgi").CreateDXGIFactory1
    create.argtypes, create.restype = [c.c_void_p, c.POINTER(c.c_void_p)], c.c_int32
    if create(guid, c.byref(factory)) < 0:
        return []
    result = []
    try:
        enumerate_adapter = method(factory, 12, c.c_int32, c.c_uint32, c.POINTER(c.c_void_p))
        for index in range(16):
            adapter = c.c_void_p()
            if enumerate_adapter(factory, index, c.byref(adapter)) < 0:
                break
            try:
                desc = Description()
                if method(adapter, 10, c.c_int32, c.POINTER(Description))(adapter, c.byref(desc)) >= 0:
                    if not desc.flags & 2:
                        result.append({"id": index, "name": desc.name, "vendor": desc.vendor,
                                       "memory_bytes": desc.video_memory})
            finally:
                method(adapter, 2, c.c_uint32)(adapter)
    finally:
        method(factory, 2, c.c_uint32)(factory)
    return result


def create_engine(config, report):
    from languages import recognition_options
    from ocr_spacing import apply_spacing, appearance_probability
    language, language_label, recognition = recognition_options(config)
    space_probability = appearance_probability(config)
    # Isolate the DML wheel from the CPU runtime used by text/audio alignment.
    gpu_path = Path(config.get("gpu_runtime") or Path(__file__).resolve().parents[1] / "tools/ocr-gpu")
    if (gpu_path / "onnxruntime").is_dir() and "onnxruntime" not in sys.modules:
        sys.path.insert(0, str(gpu_path))
    import onnxruntime as ort
    from rapidocr_onnxruntime import RapidOCR
    from rapidocr_onnxruntime.utils.infer_engine import OrtInferSession

    mode = config.get("ocr_device", "auto")
    if mode not in {"auto", "gpu", "cpu"}:
        raise ValueError("OCR 计算方式无效")
    available = ort.get_available_providers()
    provider = "CPUExecutionProvider"
    adapter = {}
    if mode != "cpu":
        if "DmlExecutionProvider" in available:
            adapters = graphics_adapters()
            if adapters:
                adapter = max(adapters, key=lambda a: (a["memory_bytes"], a["vendor"] == 0x10de))
                provider = "DmlExecutionProvider"
        elif "CUDAExecutionProvider" in available:
            provider = "CUDAExecutionProvider"
            if hasattr(ort, "preload_dlls"):
                ort.preload_dlls()
    if mode == "gpu" and provider == "CPUExecutionProvider":
        raise ValueError("未找到可用的 GPU OCR 后端，请运行 scripts/setup-ocr-gpu.ps1 或选择自动 / CPU")

    original_options = OrtInferSession._init_sess_opts
    original_providers = OrtInferSession._get_ep_list
    profile_index = 0

    def options(cfg):
        nonlocal profile_index
        value = original_options(cfg)
        if provider == "DmlExecutionProvider":
            value.enable_mem_pattern = False
            value.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        if config.get("ocr_profile"):
            value.enable_profiling = True
            value.profile_file_prefix = str(Path(config["work"]) / f"ocr-profile-{profile_index}")
            profile_index += 1
        return value

    def providers(_):
        _.use_cuda = provider == "CUDAExecutionProvider"
        _.use_directml = provider == "DmlExecutionProvider"
        if provider == "DmlExecutionProvider":
            return [(provider, {"device_id": adapter.get("id", 0)}), "CPUExecutionProvider"]
        if provider == "CUDAExecutionProvider":
            return [(provider, {"device_id": 0, "cudnn_conv_algo_search": "HEURISTIC"}), "CPUExecutionProvider"]
        return ["CPUExecutionProvider"]

    def construct():
        # Pinned RapidOCR 1.4.4 does not set the mandatory DML session options.
        # This process-local compatibility shim leaves third-party files untouched.
        OrtInferSession._init_sess_opts = staticmethod(options)
        OrtInferSession._get_ep_list = providers
        try:
            engine = RapidOCR(intra_op_num_threads=max(1, min(4, os.cpu_count() or 1)),
                            inter_op_num_threads=1, det_limit_type="max", det_limit_side_len=960,
                            **recognition)
            apply_spacing(engine.text_rec, config)
            if language in {"ru_zh", "it_zh"}:
                from rapidocr_onnxruntime.main import DEFAULT_CFG_PATH, read_yaml, update_model_path
                from rapidocr_onnxruntime.ch_ppocr_rec import TextRecognizer
                from bilingual import DualRecognizer
                rec_config = update_model_path(read_yaml(DEFAULT_CFG_PATH))["Rec"]
                rec_config.update(intra_op_num_threads=max(1, min(4, os.cpu_count() or 1)),
                                  inter_op_num_threads=1)
                engine.chinese_rec = TextRecognizer(rec_config)
                apply_spacing(engine.chinese_rec, config)
                engine.text_rec = DualRecognizer(engine.text_rec, engine.chinese_rec)
            return engine
        finally:
            OrtInferSession._init_sess_opts = staticmethod(original_options)
            OrtInferSession._get_ep_list = original_providers

    warning = None
    try:
        engine = construct()
        sessions = [engine.text_det.infer.session, engine.text_cls.infer.session,
                    engine.text_rec.session.session]
        if hasattr(engine, "chinese_rec"):
            sessions.append(engine.chinese_rec.session.session)
        if provider != "CPUExecutionProvider" and any(s.get_providers()[0] != provider for s in sessions):
            raise RuntimeError("GPU session fell back to CPU")
    except Exception as error:
        if mode == "gpu" or provider == "CPUExecutionProvider":
            raise
        warning = "GPU 初始化失败，已切换 CPU：" + str(error)
        provider, adapter = "CPUExecutionProvider", {}
        engine = construct()
        sessions = [engine.text_det.infer.session, engine.text_cls.infer.session,
                    engine.text_rec.session.session]
        if hasattr(engine, "chinese_rec"):
            sessions.append(engine.chinese_rec.session.session)
    info = {"provider": provider, "adapter": adapter, "runtime_version": ort.__version__,
            "sessions": [s.get_providers() for s in sessions], "ocr_language": language,
            "language_name": language_label,
            "recognition_model": recognition.get("rec_model_path", "bundled Chinese/English")}
    info["spacing"] = {"probability": space_probability}
    if warning:
        info["warning"] = warning
        report(warning, 0)
    label = "GPU · " + adapter.get("name", provider) if provider != "CPUExecutionProvider" else "CPU"
    info["label"] = label
    report("字幕 OCR 使用 " + label + " · " + language_label, 0)
    return engine, info, sessions
