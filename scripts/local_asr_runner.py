#!/usr/bin/env python3
"""Yumu Studio local ASR bridge.

Contract: local_asr_runner.py ENGINE MODEL_DIR AUDIO_WAV OUTPUT_SRT LANGUAGE DEVICE PRECISION
The Qt application deliberately talks to a process boundary so Python/CUDA/ONNX
dependencies can be updated without rebuilding the desktop application.
"""
import os
import sys
import math
import re
import wave
from pathlib import Path


def write_srt(segments, output):
    def stamp(seconds):
        ms = max(0, int(float(seconds) * 1000))
        h, ms = divmod(ms, 3600000)
        m, ms = divmod(ms, 60000)
        s, ms = divmod(ms, 1000)
        return f"{h:02d}:{m:02d}:{s:02d},{ms:03d}"

    with open(output, "w", encoding="utf-8") as f:
        cue_index = 1
        for start, end, text in segments:
            text = _clean_asr_text(text)
            if not text:
                continue
            f.write(f"{cue_index}\n{stamp(start)} --> {stamp(end)}\n{text}\n\n")
            cue_index += 1


def _load_glossary_terms(path):
    """Return the preferred spellings from the creator glossary (hotword hint)."""
    if not path:
        return []
    try:
        import json
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception:
        return []
    terms = []
    for item in data if isinstance(data, list) else []:
        if not isinstance(item, dict):
            continue
        term = str(item.get("replacement") or item.get("term") or "").strip()
        if term:
            terms.append(term)
    return terms


def _select_top_glossary_terms(terms, max_terms):
    """Lightweight H-PRM: prioritize CJK/long terms, preserve original order.
    Mirrors Glossary::selectTopEntries (C++) for Python-side consistency."""
    if max_terms <= 0 or len(terms) <= max_terms:
        return terms
    scored = []
    for idx, term in enumerate(terms):
        t = term.strip()
        cjk = sum(1 for ch in t if ('\u4e00' <= ch <= '\u9fff') or ('\u3400' <= ch <= '\u4dbf') or ('\u3000' <= ch <= '\u303f'))
        score = cjk * 10 + len(t)
        scored.append((score, idx, term))
    scored.sort(key=lambda x: (-x[0], x[1]))
    keep = {idx for _, idx, _ in scored[:max_terms]}
    return [terms[i] for i in range(len(terms)) if i in keep]


def _load_glossary_terms_limited(path, max_terms):
    """Load glossary, deduplicate, and apply lightweight H-PRM top-N truncation."""
    raw = _load_glossary_terms(path)
    if not raw:
        return []
    if len(raw) > max_terms:
        selected = _select_top_glossary_terms(raw, max_terms)
        print(f"Glossary: {len(raw)} terms -> top {len(selected)} via lightweight H-PRM (max {max_terms})", file=sys.stderr)
        raw = selected
    # Deduplicate case-insensitive, preserve order
    seen = set()
    out = []
    for t in raw:
        k = t.strip().lower()
        if not k or k in seen:
            continue
        seen.add(k)
        out.append(t.strip())
    return out


def _natural_prompt_for_whisper(terms):
    """Build natural-sentence prompt for whisper.cpp (reference, main is C++)."""
    if not terms:
        return ""
    joined = "、".join(terms)
    prompt = f"這段音訊涉及以下專有名詞與關鍵術語，請保持拼寫一致：{joined}。"
    if len(prompt) > 800 and len(terms) > 20:
        prompt = f"這段音訊涉及以下專有名詞與關鍵術語，請保持拼寫一致：{'、'.join(terms[:20])}。"
    return prompt


def _prompt_kwarg(func, prompt, candidates=("prompt", "text_prompt", "initial_prompt")):
    """Best-effort prompt injection for engines whose generate()/factory API
    may (depending on package version) accept a free-text prompt kwarg.
    Returns {kwarg: prompt} when the signature plausibly accepts it, else {}.
    Callers must keep a working no-prompt fallback: a wrong guess raises
    TypeError, which existing fallback chains already absorb."""
    if not prompt:
        return {}
    try:
        import inspect
        params = inspect.signature(func).parameters
    except Exception:
        return {}
    for key in candidates:
        if key in params:
            return {key: prompt}
    if any(p.kind == inspect.Parameter.VAR_KEYWORD for p in params.values()):
        return {"prompt": prompt}
    return {}


def run_faster_whisper(model_dir, audio, language, requested_device, precision, glossary_path=None, prompt=None):
    _ensure_cuda_dll_paths()
    from faster_whisper import WhisperModel
    device = _resolve_device(requested_device, "faster-whisper", supports_vulkan=False)
    compute_type = _resolve_precision(precision, device)
    print(f"local ASR backend: faster-whisper device={device} compute_type={compute_type}", file=sys.stderr)
    try:
        model = WhisperModel(str(model_dir), device=device, compute_type=compute_type)
    except Exception as exc:
        if device == "cuda" and _is_cuda_runtime_error(exc):
            print(f"YUMU_WARNING: faster-whisper 無法使用 CUDA 執行，已改用 CPU：{exc}", file=sys.stderr)
            device = "cpu"
            compute_type = _resolve_precision(precision, device)
            model = WhisperModel(str(model_dir), device=device, compute_type=compute_type)
        else:
            raise
    lang = None if not language or language == "auto" else language
    # Hotwords: space-joined string, lightweight H-PRM top-50 (224 token limit)
    glossary_terms = _load_glossary_terms_limited(glossary_path, 50)
    hotwords = " ".join(glossary_terms) or None
    if hotwords:
        print(f"faster-whisper hotwords: {hotwords[:200]}{'...' if len(hotwords)>200 else ''}", file=sys.stderr)
    if prompt:
        print(f"faster-whisper initial_prompt: {prompt[:200]}{'...' if len(prompt)>200 else ''}", file=sys.stderr)
    parts, _ = model.transcribe(str(audio), language=lang, vad_filter=True, hotwords=hotwords,
                                initial_prompt=prompt or None)
    return [(p.start, p.end, p.text) for p in parts]


def run_funasr(engine, model_dir, audio, language, requested_device, precision, glossary_path=None, prompt=None):
    model_dir = Path(model_dir)
    _ensure_cuda_dll_paths()
    # The catalog's SenseVoice Small model is a sherpa-onnx export, not a
    # native FunASR checkpoint. Route it to the matching ONNX recognizer.
    if ("sensevoice" in model_dir.name.lower()
            and (model_dir / "model.int8.onnx").exists()
            and (model_dir / "tokens.txt").exists()):
        return run_sherpa("SenseVoice", model_dir, audio, language, requested_device, glossary_path)
    # Fun-ASR-Nano branch (LLM-based, hotwords + itn)
    normalized_engine = (engine or "FunASR").strip().lower().replace(" ", "").replace("-", "").replace("_", "")
    if normalized_engine in {"funasrnano", "funasr nano", "fun-asr-nano"} or "nano" in model_dir.name.lower():
        return _run_fun_asr_nano(model_dir, audio, language, requested_device, glossary_path, prompt)
    # Native FunASR (SeACo-Paraformer / Paraformer) with hotword support
    try:
        from funasr import AutoModel
    except ModuleNotFoundError as exc:
        if exc.name == "torchaudio":
            raise RuntimeError(
                "FunASR requires torchaudio in the selected Python runtime. "
                "Install the dependencies listed in scripts\\requirements-local-asr.txt."
            ) from exc
        raise
    device = _resolve_device(requested_device, "FunASR", supports_vulkan=False)
    print(f"local ASR backend: FunASR device={device}", file=sys.stderr)
    hotword_terms = _load_glossary_terms_limited(glossary_path, 50)
    hotword_arg = None
    tmp_hotword_file = None
    if hotword_terms:
        # SeACo-Paraformer prefers a hotword file (supports large lists via ASF)
        import tempfile
        try:
            tf = tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', delete=False, suffix='.txt')
            tf.write("\n".join(hotword_terms))
            tf.close()
            tmp_hotword_file = tf.name
            hotword_arg = tmp_hotword_file
            print(f"FunASR hotwords ({len(hotword_terms)}): {', '.join(hotword_terms[:5])}{'...' if len(hotword_terms)>5 else ''}", file=sys.stderr)
        except Exception as exc:
            print(f"YUMU_WARNING: FunASR hotword file creation failed: {exc}", file=sys.stderr)
            hotword_arg = " ".join(hotword_terms)
    try:
        model = AutoModel(model=str(model_dir), vad_model="fsmn-vad", device=device)
    except Exception as exc:
        if device == "cuda" and _is_cuda_runtime_error(exc):
            print(f"YUMU_WARNING: FunASR 無法使用 CUDA 執行，已改用 CPU：{exc}", file=sys.stderr)
            model = AutoModel(model=str(model_dir), vad_model="fsmn-vad", device="cpu")
        else:
            if tmp_hotword_file and os.path.exists(tmp_hotword_file):
                try: os.remove(tmp_hotword_file)
                except: pass
            raise
    try:
        if hotword_arg:
            try:
                result = model.generate(input=str(audio), batch_size_s=300, hotword=hotword_arg)
            except TypeError:
                # Fallback: some FunASR versions use 'hotwords' kwarg
                result = model.generate(input=str(audio), batch_size_s=300, hotwords=hotword_terms)
        else:
            result = model.generate(input=str(audio), batch_size_s=300)
    finally:
        if tmp_hotword_file and os.path.exists(tmp_hotword_file):
            try: os.remove(tmp_hotword_file)
            except: pass
    return _funasr_segments(result)


def _run_fun_asr_nano(model_dir, audio, language, requested_device, glossary_path=None, prompt=None):
    """Run Fun-ASR-Nano-2512 / MLT-Nano with hotwords + ITN."""
    _ensure_cuda_dll_paths()
    try:
        from funasr import AutoModel
    except ModuleNotFoundError as exc:
        if exc.name == "torchaudio":
            raise RuntimeError("Fun-ASR-Nano requires torchaudio. Install scripts\\requirements-local-asr.txt") from exc
        raise
    device = _resolve_device(requested_device, "Fun-ASR-Nano", supports_vulkan=False)
    print(f"local ASR backend: Fun-ASR-Nano device={device}", file=sys.stderr)
    hotwords = _load_glossary_terms_limited(glossary_path, 20)
    if hotwords:
        print(f"Fun-ASR-Nano hotwords ({len(hotwords)}): {', '.join(hotwords[:5])}{'...' if len(hotwords)>5 else ''}", file=sys.stderr)
    lang_map = {"zh": "中文", "yue": "中文", "en": "en", "ja": "日文"}
    fun_lang = lang_map.get((language or "auto").strip().lower(), "中文" if not language or language == "auto" else language)
    # Map auto -> 中文 for Nano's language param; Nano also supports en/ja etc.
    if not language or language == "auto":
        fun_lang = "中文"
    try:
        model = AutoModel(model=str(model_dir), trust_remote_code=True, device=device, vad_model="fsmn-vad", vad_kwargs={"max_single_segment_time": 30000})
    except Exception as exc:
        if device == "cuda" and _is_cuda_runtime_error(exc):
            print(f"YUMU_WARNING: Fun-ASR-Nano 無法使用 CUDA，已改用 CPU：{exc}", file=sys.stderr)
            model = AutoModel(model=str(model_dir), trust_remote_code=True, device="cpu", vad_model="fsmn-vad", vad_kwargs={"max_single_segment_time": 30000})
        else:
            raise
    # A free-text prompt is attempted only when this funasr version's
    # generate() advertises a prompt-like kwarg (probed, never assumed).
    prompt_kwargs = _prompt_kwarg(model.generate, prompt)
    if prompt_kwargs:
        print(f"Fun-ASR-Nano custom prompt via '{next(iter(prompt_kwargs))}'", file=sys.stderr)
    try:
        # Newer FunASR supports hotwords + language + itn
        if hotwords:
            try:
                result = model.generate(input=str(audio), cache={}, batch_size_s=300, hotwords=hotwords, language=fun_lang, itn=True, **prompt_kwargs)
            except TypeError:
                result = model.generate(input=str(audio), batch_size_s=300, hotword=" ".join(hotwords))
        else:
            result = model.generate(input=str(audio), cache={}, batch_size_s=300, language=fun_lang, itn=True, **prompt_kwargs)
    except TypeError:
        if prompt_kwargs:
            print("YUMU_WARNING: Fun-ASR-Nano rejected the custom prompt kwarg; continuing without it", file=sys.stderr)
        # Fallback for older funasr signature
        result = model.generate(input=str(audio), batch_size_s=300)
    return _funasr_segments(result)


def _funasr_segments(result):
    if isinstance(result, dict):
        result = [result]
    out = []
    for item in result or []:
        text = item.get("text", "") if isinstance(item, dict) else str(item)
        timestamps = item.get("timestamp", []) if isinstance(item, dict) else []
        if timestamps:
            for start, end, *rest in timestamps:
                out.append((float(start) / 1000, float(end) / 1000, text))
        elif text:
            out.append((0, 0, text))
    return out


def _ensure_cuda_dll_paths():
    """Add bundled nvidia / torch CUDA libraries to PATH on Windows.

    sherpa-onnx's bundled ONNX Runtime provider DLL loads dependent CUDA
    libraries via the Windows loader.  The nvidia-*-cu12 pip packages place
    their DLLs in site-packages/nvidia/*/bin, and torch bundles its own
    copies in torch/lib.  Neither directory is automatically on PATH when the
    Qt application launches this script as a subprocess, so the provider
    fails with 'Failed to load shared library' (Error 126) or DLL init
    failure (Error 1114)."""
    if sys.platform != "win32":
        return
    try:
        from pathlib import Path
        import site
    except Exception:
        return
    # Collect site-packages roots (user + system)
    roots = set(Path(p) for p in site.getsitepackages())
    user_site = site.getusersitepackages()
    if user_site:
        roots.add(Path(user_site))
    # Also try deriving from the current script's Python
    roots.add(Path(sys.executable).parent / "Lib" / "site-packages")
    extra_paths = []
    for root in roots:
        if not root.exists():
            continue
        # nvidia-*-cu12 packages place DLLs under nvidia/<lib>/bin
        nvidia_dir = root / "nvidia"
        if nvidia_dir.exists():
            for sub in nvidia_dir.iterdir():
                bin_dir = sub / "bin"
                if bin_dir.is_dir():
                    extra_paths.append(str(bin_dir))
        # torch bundles its own CUDA libs (e.g. cufft64_11.dll)
        torch_lib = root / "torch" / "lib"
        if torch_lib.is_dir():
            extra_paths.append(str(torch_lib))
    if extra_paths:
        # Prepend so bundled libs shadow any incompatible system CUDA
        current_path = os.environ.get("PATH", "")
        os.environ["PATH"] = ";".join(extra_paths + [current_path])

def _clean_asr_text(text):
    """Remove tokenizer control tags that must not appear in subtitle text."""
    normalized = re.sub(r"<\|[^>]+\|>", "", str(text))
    return re.sub(r"\s+", " ", normalized).strip()


def _split_sentences(text):
    """Split SenseVoice text at sentence-ending punctuation."""
    normalized = _clean_asr_text(text)
    if not normalized:
        return []
    parts = re.findall(r".+?(?:[。！？!?；;]+|$)", normalized)
    return [part.strip() for part in parts if part.strip()]


def _estimated_sentence_segments(text, duration):
    sentences = _split_sentences(text)
    if not sentences:
        return []
    if len(sentences) == 1:
        return [(0, duration, sentences[0])]

    weights = [max(1, len(re.sub(r"\s+", "", sentence))) for sentence in sentences]
    total_weight = float(sum(weights))
    segments = []
    start = 0.0
    for index, (sentence, weight) in enumerate(zip(sentences, weights)):
        end = duration if index == len(sentences) - 1 else start + duration * weight / total_weight
        segments.append((start, end, sentence))
        start = end
    return segments


def _speech_regions(samples, rate, max_region_seconds=8.0, fallback_to_full=True):
    """Find speech-like regions without requiring another model download."""
    frame_size = max(1, int(rate * 0.03))
    energies = []
    for start in range(0, len(samples), frame_size):
        frame = samples[start:start + frame_size]
        if frame:
            energies.append(math.sqrt(sum(value * value for value in frame) / len(frame)))
    if not energies:
        return [(0, len(samples))] if fallback_to_full else []

    ordered = sorted(energies)
    noise_count = max(1, len(ordered) // 5)
    noise_floor = sum(ordered[:noise_count]) / noise_count
    peak = max(energies)
    threshold = max(0.008, noise_floor * 2.5, peak * 0.08)
    voiced = [energy >= threshold for energy in energies]

    # Fill short gaps and discard isolated noise bursts.
    gap_frames = max(1, int(0.24 / 0.03))
    short_frames = max(1, int(0.18 / 0.03))
    index = 0
    while index < len(voiced):
        if voiced[index]:
            index += 1
            continue
        end = index
        while end < len(voiced) and not voiced[end]:
            end += 1
        if index > 0 and end < len(voiced) and end - index <= gap_frames:
            voiced[index:end] = [True] * (end - index)
        index = end

    regions = []
    index = 0
    while index < len(voiced):
        if not voiced[index]:
            index += 1
            continue
        end = index
        while end < len(voiced) and voiced[end]:
            end += 1
        if end - index >= short_frames:
            pad = int(0.18 * rate)
            regions.append((max(0, index * frame_size - pad),
                            min(len(samples), end * frame_size + pad)))
        index = end

    if not regions:
        return [(0, len(samples))] if fallback_to_full else []

    merged = []
    merge_gap = int(0.45 * rate)
    for start, end in regions:
        if merged and start - merged[-1][1] <= merge_gap:
            merged[-1] = (merged[-1][0], end)
        else:
            merged.append((start, end))

    # Continuous speech may contain no detectable pause. Keep captions usable
    # by splitting very long regions near eight-second boundaries.
    max_region = max(1, int(max_region_seconds * rate))
    split_regions = []
    for start, end in merged:
        while end - start > max_region:
            target = start + max_region
            search_start = max(start + int(4 * rate), target - int(1.5 * rate))
            search_end = min(end - int(1.0 * rate), target + int(1.5 * rate))
            if search_start >= search_end:
                cut = target
            else:
                first = max(0, search_start // frame_size)
                last = min(len(energies) - 1, search_end // frame_size)
                quiet = min(range(first, last + 1), key=lambda item: energies[item])
                cut = max(start + int(2 * rate), quiet * frame_size)
            split_regions.append((start, cut))
            start = cut
        if end > start:
            split_regions.append((start, end))
    return split_regions


def _sensevoice_segments(recognizer, samples, rate):
    segments = []
    for start, end in _speech_regions(samples, rate):
        stream = recognizer.create_stream()
        stream.accept_waveform(rate, samples[start:end])
        recognizer.decode_stream(stream)
        text = stream.result.text
        duration = (end - start) / rate
        for local_start, local_end, sentence in _estimated_sentence_segments(text, duration):
            segments.append((start / rate + local_start,
                             start / rate + local_end,
                             sentence))
    return segments


def _chunked_sherpa_segments(recognizer, samples, rate, engine, max_region_seconds,
                             fallback_to_full=False):
    """Decode long audio in model-sized pieces and restore source timestamps."""
    segments = []
    regions = _speech_regions(samples, rate,
                              max_region_seconds=max_region_seconds,
                              fallback_to_full=fallback_to_full)
    for start, end in regions:
        if end <= start:
            continue
        stream = recognizer.create_stream()
        stream.accept_waveform(rate, samples[start:end])
        try:
            recognizer.decode_stream(stream)
        except RuntimeError as exc:
            begin = start / rate
            finish = end / rate
            raise RuntimeError(
                f"{engine} failed while decoding {begin:.1f}-{finish:.1f}s: {exc}"
            ) from exc

        text = _clean_asr_text(stream.result.text)
        if not text:
            continue
        duration = (end - start) / rate
        for local_start, local_end, sentence in _estimated_sentence_segments(text, duration):
            sentence = _clean_asr_text(sentence)
            if sentence:
                segments.append((start / rate + local_start,
                                 start / rate + local_end,
                                 sentence))
    return segments


def _build_sherpa_recognizer(engine, model_dir, provider, language, glossary_path=None, hotwords_file=None, prompt=None):
    _ensure_cuda_dll_paths()
    import sherpa_onnx
    model_dir = Path(model_dir)
    files = {p.name: p for p in model_dir.rglob("*") if p.is_file()}
    if engine == "Qwen3-ASR":
        tokenizer_dir = model_dir / "tokenizer"
        required = [
            model_dir / "conv_frontend.onnx",
            model_dir / "encoder.int8.onnx",
            model_dir / "decoder.int8.onnx",
            tokenizer_dir / "vocab.json",
            tokenizer_dir / "merges.txt",
            tokenizer_dir / "tokenizer_config.json",
        ]
        missing = [str(path.relative_to(model_dir)) for path in required if not path.exists()]
        if missing:
            raise RuntimeError("Qwen3-ASR model is incomplete; missing: " + ", ".join(missing))
        # Qwen3-ASR hotwords: sherpa-onnx >=1.12.35 supports prompt-style hotwords (comma-separated).
        # A free-text prompt is a different channel: only wired when this
        # sherpa-onnx version's factory advertises a prompt-like kwarg.
        qwen_prompt_kwargs = _prompt_kwarg(
            sherpa_onnx.OfflineRecognizer.from_qwen3_asr, prompt)
        if qwen_prompt_kwargs:
            print(f"Qwen3-ASR custom prompt via '{next(iter(qwen_prompt_kwargs))}'", file=sys.stderr)
        elif prompt:
            print("YUMU_WARNING: Qwen3-ASR has no free-text prompt parameter in this sherpa-onnx; custom prompt ignored (glossary hotwords still apply)", file=sys.stderr)
        qwen_hotwords = ""
        if glossary_path:
            qwen_terms = _load_glossary_terms_limited(glossary_path, 10)
            if qwen_terms:
                qwen_hotwords = ", ".join(qwen_terms)
                print(f"Qwen3-ASR hotwords ({len(qwen_terms)}): {qwen_hotwords[:200]}{'...' if len(qwen_hotwords)>200 else ''}", file=sys.stderr)
        base_kwargs = dict(
            conv_frontend=str(model_dir / "conv_frontend.onnx"),
            encoder=str(model_dir / "encoder.int8.onnx"),
            decoder=str(model_dir / "decoder.int8.onnx"),
            tokenizer=str(tokenizer_dir),
            num_threads=4,
            sample_rate=16000,
            feature_dim=128,
            provider=provider,
            max_new_tokens=128,
        )
        # Try hotwords-aware API (1.13.x), fallback to base if not supported.
        # The probed prompt kwargs ride along; a TypeError here may come from
        # either kwarg, so hotwords are retried clean before giving up on them:
        # a bad prompt guess must never degrade the working hotwords channel.
        if qwen_hotwords:
            for key in ("hotwords", "hot_words"):
                try:
                    return sherpa_onnx.OfflineRecognizer.from_qwen3_asr(**{**base_kwargs, **qwen_prompt_kwargs, key: qwen_hotwords})
                except TypeError:
                    continue
            if qwen_prompt_kwargs:
                for key in ("hotwords", "hot_words"):
                    try:
                        return sherpa_onnx.OfflineRecognizer.from_qwen3_asr(**{**base_kwargs, key: qwen_hotwords})
                    except TypeError:
                        continue
            print("YUMU_WARNING: Qwen3-ASR hotwords not supported by current sherpa-onnx, using glossary post-processing only", file=sys.stderr)
        try:
            return sherpa_onnx.OfflineRecognizer.from_qwen3_asr(**{**base_kwargs, **qwen_prompt_kwargs})
        except TypeError:
            if qwen_prompt_kwargs:
                print("YUMU_WARNING: Qwen3-ASR rejected the custom prompt kwarg; continuing without it", file=sys.stderr)
                return sherpa_onnx.OfflineRecognizer.from_qwen3_asr(**base_kwargs)
            raise
    tokens = files.get("tokens.txt")
    if not tokens:
        raise RuntimeError("tokens.txt is missing from the model directory")
    if engine == "SenseVoice":
        return sherpa_onnx.OfflineRecognizer.from_sense_voice(
            model=str(files["model.int8.onnx"]),
            tokens=str(tokens),
            num_threads=4,
            provider=provider,
            language="" if not language or language == "auto" else language,
            use_itn=True,
        )
    if engine == "Parakeet":
        parakeet_files = {
            "encoder": files.get("encoder.int8.onnx"),
            "decoder": files.get("decoder.int8.onnx"),
            "joiner": files.get("joiner.int8.onnx"),
        }
        missing = [name for name, path in parakeet_files.items() if not path]
        if missing:
            raise RuntimeError(
                "Parakeet model is incomplete; missing: "
                + ", ".join(f"{name}.int8.onnx" for name in missing)
            )
        # Parakeet hotwords via Aho-Corasick, requires modified_beam_search
        if hotwords_file and Path(hotwords_file).exists():
            print(f"Parakeet hotwords file: {hotwords_file}", file=sys.stderr)
            # Try full hotwords API
            for kwargs in (
                dict(hotwords_file=str(hotwords_file), hotwords_score=2.0, decoding_method="modified_beam_search"),
                dict(hotwords_file=str(hotwords_file), hotwords_score=1.5, decoding_method="modified_beam_search"),
                dict(hotwords_file=str(hotwords_file), decoding_method="modified_beam_search"),
            ):
                try:
                    return sherpa_onnx.OfflineRecognizer.from_transducer(
                        encoder=str(parakeet_files["encoder"]),
                        decoder=str(parakeet_files["decoder"]),
                        joiner=str(parakeet_files["joiner"]),
                        tokens=str(tokens),
                        num_threads=4,
                        model_type="nemo_transducer",
                        provider=provider,
                        **kwargs,
                    )
                except TypeError as e:
                    last_err = str(e)
                    continue
            print(f"YUMU_WARNING: Parakeet hotwords API not supported ({last_err}), fallback without hotwords", file=sys.stderr)
        return sherpa_onnx.OfflineRecognizer.from_transducer(
            encoder=str(parakeet_files["encoder"]),
            decoder=str(parakeet_files["decoder"]),
            joiner=str(parakeet_files["joiner"]),
            tokens=str(tokens),
            num_threads=4,
            model_type="nemo_transducer",
            provider=provider,
        )
    if engine == "FireRedASR":
        # FireRedASR has no hotwords support (AED/CTC), log for transparency
        if glossary_path and _load_glossary_terms(glossary_path):
            print("YUMU_WARNING: FireRedASR does not support hotwords; using glossary post-processing only", file=sys.stderr)
        return sherpa_onnx.OfflineRecognizer.from_fire_red_asr(
            encoder=str(files["encoder.int8.onnx"]),
            decoder=str(files["decoder.int8.onnx"]),
            tokens=str(tokens), num_threads=4, provider=provider)
    return sherpa_onnx.OfflineRecognizer.from_paraformer(
        paraformer=str(files.get("model.int8.onnx", files["encoder.int8.onnx"])),
        tokens=str(tokens), num_threads=4, provider=provider)


def _is_cuda_runtime_error(exc):
    message = str(exc).lower()
    return any(
        keyword in message
        for keyword in ("cuda", "cublas", "cudnn", "nvidia", "onnxruntime_providers_cuda")
    )


def run_sherpa(engine, model_dir, audio, language, requested_device, glossary_path=None, prompt=None):
    _ensure_cuda_dll_paths()
    import sherpa_onnx
    provider = _sherpa_provider(requested_device, engine)
    # H-07: 模型完整性檢查應前置於硬件探測，避免缺文件被誤判為 CUDA 錯誤
    try:
        _verify_sherpa_runtime(provider, engine, sherpa_onnx, requested_device)
    except RuntimeError as exc:
        msg = str(exc)
        if "YUMU_WARNING" in msg and _normalize_device(requested_device) == "auto":
            print(msg, file=sys.stderr)
            provider = "cpu"
        elif "YUMU_WARNING" in msg and provider == "cuda":
            print(msg, file=sys.stderr)
            provider = "cpu"
        else:
            raise
    print(f"local ASR backend: {engine} provider={provider} runtime={_sherpa_runtime_label(sherpa_onnx)}", file=sys.stderr)
    # Prepare Parakeet hotwords file if needed
    tmp_hotwords_file = None
    if engine == "Parakeet" and glossary_path:
        terms = _load_glossary_terms_limited(glossary_path, 50)
        if terms:
            import tempfile
            try:
                tf = tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', delete=False, suffix='.txt')
                tf.write("\n".join(terms))
                tf.close()
                tmp_hotwords_file = tf.name
                print(f"Parakeet prepared {len(terms)} hotwords for Aho-Corasick boosting", file=sys.stderr)
            except Exception as exc:
                print(f"YUMU_WARNING: Parakeet hotwords file failed: {exc}", file=sys.stderr)
    try:
        try:
            recognizer = _build_sherpa_recognizer(engine, model_dir, provider, language, glossary_path, tmp_hotwords_file, prompt)
        except RuntimeError as exc:
            if provider == "cuda" and _is_cuda_runtime_error(exc):
                print(f"YUMU_WARNING: {engine} 無法使用 CUDA 執行，已改用 CPU：{exc}", file=sys.stderr)
                recognizer = _build_sherpa_recognizer(engine, model_dir, "cpu", language, glossary_path, tmp_hotwords_file, prompt)
            elif "YUMU_WARNING" in str(exc) and provider == "cuda":
                print(f"YUMU_WARNING: {engine} 回退到 CPU：{exc}", file=sys.stderr)
                recognizer = _build_sherpa_recognizer(engine, model_dir, "cpu", language, glossary_path, tmp_hotwords_file, prompt)
            else:
                raise
    finally:
        # Keep hotwords file during recognition; delete after segments collected
        pass
    # Decode dispatch
    try:
        if engine == "SenseVoice":
            samples, rate = _read_wav(audio)
            return _sensevoice_segments(recognizer, samples, rate)
        samples, rate = _read_wav(audio)
        if engine == "FireRedASR":
            return _chunked_sherpa_segments(recognizer, samples, rate, engine, 35.0)
        if engine == "Qwen3-ASR":
            return _chunked_sherpa_segments(recognizer, samples, rate, engine, 30.0)
        if engine == "Parakeet":
            return _chunked_sherpa_segments(
                recognizer, samples, rate, engine, 8.0, fallback_to_full=True)
        stream = recognizer.create_stream()
        stream.accept_waveform(rate, samples)
        recognizer.decode_stream(stream)
        duration = len(samples) / rate
        text = _clean_asr_text(stream.result.text)
        return [(0, duration, text)] if text else []
    finally:
        if tmp_hotwords_file and os.path.exists(tmp_hotwords_file):
            try: os.remove(tmp_hotwords_file)
            except: pass


def _read_wav(path):
    with wave.open(str(path), "rb") as f:
        if f.getnchannels() != 1 or f.getsampwidth() != 2:
            raise RuntimeError("runner requires mono 16-bit WAV")
        raw = f.readframes(f.getnframes())
        rate = f.getframerate()
    import array
    values = array.array("h")
    values.frombytes(raw)
    return [v / 32768.0 for v in values], rate


def _onnxruntime_providers():
    try:
        import onnxruntime as ort
        return set(ort.get_available_providers())
    except Exception:
        return set()



def _sherpa_runtime_label(sherpa_onnx):
    version = getattr(sherpa_onnx, "__version__", "unknown")
    try:
        from importlib.metadata import version as package_version
        version = package_version("sherpa-onnx")
    except Exception:
        pass
    return str(version)


def _sherpa_native_cuda_support(sherpa_onnx):
    """True when the imported sherpa-onnx wheel bundles a CUDA provider.

    Only inspects the wheel metadata and its native DLLs; never imports pip
    onnxruntime, CTranslate2 or Torch.
    """
    label = _sherpa_runtime_label(sherpa_onnx).lower()
    if "+cuda" in label:
        return True
    native_dir = Path(sherpa_onnx.__file__).parent / "lib"
    if not native_dir.is_dir():
        return False
    return any(
        "cuda" in path.name.lower() or "onnxruntime_providers_cuda" in path.name.lower()
        for path in native_dir.glob("*.dll")
    )


def _sherpa_cuda_capable():
    """Probe the installed sherpa-onnx wheel for CUDA support.

    Unlike _cuda_available(), this must not import pip onnxruntime or Torch:
    loading a second ONNX Runtime into the process crashes the bundled
    sherpa-onnx runtime. The CUDA provider bundled with a "+cuda" wheel is the
    only signal we can rely on without importing anything.
    """
    try:
        import sherpa_onnx
    except Exception:
        return False
    return _sherpa_native_cuda_support(sherpa_onnx)


def _verify_sherpa_runtime(provider, engine, sherpa_onnx, requested_device="cuda"):
    """Reject a CPU-only sherpa wheel when CUDA was explicitly selected.

    H-06: 對 auto 請求不應硬失敗，而是發出 YUMU_WARNING 並由上層回退到 cpu。
    显式 cuda 亦通过异常交给 run_sherpa 的 YUMU_WARNING 分支处理。
    """
    if provider != "cuda":
        return
    if _sherpa_native_cuda_support(sherpa_onnx):
        return
    # 若 caller 是 auto 或顯式 cuda，均以 YUMU_WARNING 形式拋出，讓上層可回退到 cpu（H-06）
    msg = (
        f"YUMU_WARNING: {engine} 已選擇 CUDA，但目前載入的是 CPU 版 sherpa-onnx（{_sherpa_runtime_label(sherpa_onnx)}）。"
        "請在設定中重新安裝 ONNX Runtime CUDA 版本。"
    )
    print(msg, file=sys.stderr)
    raise RuntimeError(msg)


def _cuda_available():
    if "CUDAExecutionProvider" in _onnxruntime_providers():
        return True
    try:
        import ctranslate2
        if ctranslate2.get_cuda_device_count() > 0:
            return True
    except Exception:
        pass
    try:
        import torch
        return bool(torch.cuda.is_available())
    except Exception:
        return False


def _runtime_cuda_available(engine):
    """Whether the engine's actual runtime can drive CUDA.

    Probes only the runtime the engine really uses so a missing or unrelated
    heavy package does not produce a false negative. sherpa-onnx engines are
    checked against the installed wheel's bundled CUDA provider without
    importing pip onnxruntime/CTranslate2/Torch (loading a second ONNX Runtime
    into the process would crash Parakeet).
    """
    lower = (engine or "").strip().lower()
    if lower in {"parakeet", "qwen3-asr", "fireredasr", "sensevoice"}:
        return _sherpa_cuda_capable()
    if lower == "faster-whisper":
        try:
            import ctranslate2
            return ctranslate2.get_cuda_device_count() > 0
        except Exception:
            return False
    if lower == "funasr":
        try:
            import torch
            return bool(torch.cuda.is_available())
        except Exception:
            return False
    return _cuda_available()


def _normalize_device(value):
    value = (value or "auto").strip().lower()
    compact = value.replace(" ", "")
    aliases = {
        "automatic": "auto",
        "gpu": "cuda",
        "gpu/cuda": "cuda",
        "gpu/vulkan": "vulkan",
    }
    value = aliases.get(compact, value)
    if value not in {"auto", "cpu", "cuda", "vulkan"}:
        raise RuntimeError(f"unknown compute device: {value}")
    return value


def _resolve_device(requested, engine, supports_vulkan=False):
    """Resolve the user choice without silently changing an explicit mode."""
    requested = _normalize_device(requested)
    if requested == "auto":
        # H-04/H-05: auto 一律嘗試 cuda，失敗再由各引擎 wrapper 以 YUMU_WARNING 回退到 cpu。
        # 此前依賴 torch/sherpa 輪的預檢門檻導致在全新安裝（輪未就緒）時永遠 cpu，違背 UI 文案
        # 「自動選擇最優硬件」。現在與 faster-whisper 已實現的邏輯一致。
        return "cuda"
    if requested == "cpu":
        return "cpu"
    if requested == "cuda":
        # Always attempt the engine's CUDA runtime. Each engine wrapper falls
        # back to CPU (with a YUMU_WARNING marker) when CUDA cannot start, so
        # an explicit GPU request never hard-fails on a stale pre-probe.
        return "cuda"
    if not supports_vulkan:
        raise RuntimeError(
            f"{engine} does not provide a Vulkan backend. Select CPU or GPU / CUDA for this engine."
        )
    return "vulkan"


def _resolve_precision(requested, device):
    requested = (requested or "auto").strip().lower()
    if requested not in {"auto", "int8", "float16", "float32"}:
        raise RuntimeError(f"unknown precision: {requested}")
    if requested != "auto":
        return requested
    return "float16" if device == "cuda" else "int8"


def _sherpa_provider(requested_device, engine):
    """Select sherpa-onnx's provider explicitly for every recognizer.

    The Python sherpa-onnx API exposes CPU and CUDA providers. Vulkan is a
    whisper.cpp/ggml backend and is not an ONNX provider in this runtime.
    """
    device = _resolve_device(requested_device, engine, supports_vulkan=False)
    return device


def main(argv):
    if len(argv) not in {6, 8, 9}:
        raise SystemExit(
            "usage: local_asr_runner.py ENGINE MODEL_DIR AUDIO_WAV OUTPUT_SRT LANGUAGE [DEVICE PRECISION [GLOSSARY_JSON]]"
        )
    engine, model_dir, audio, output, language = argv[1:6]
    requested_device = argv[6] if len(argv) >= 7 else "auto"
    precision = argv[7] if len(argv) >= 8 else "auto"
    glossary_path = argv[8] if len(argv) >= 9 else None
    # Free-text custom prompt travels via environment (never argv): older
    # callers/scripts simply ignore the unknown variable.
    custom_prompt = os.environ.get("YUMU_ASR_PROMPT", "").strip() or None
    # Normalize engine for case-insensitive matching (Qt passes exact IDs)
    norm_engine = (engine or "").strip()
    lower_engine = norm_engine.lower().replace(" ", "").replace("_", "-")
    if lower_engine in {"faster-whisper", "fasterwhisper"}:
        segments = run_faster_whisper(model_dir, audio, language, requested_device, precision,
                                      glossary_path=glossary_path, prompt=custom_prompt)
    elif lower_engine in {"funasr", "fun-asr-nano", "funasrnano"}:
        # Dispatch to unified FunASR handler which branches to Nano vs SeACo
        segments = run_funasr(norm_engine, model_dir, audio, language, requested_device, precision,
                              glossary_path=glossary_path, prompt=custom_prompt)
    elif lower_engine in {"qwen3-asr", "qwen3asr", "fireredasr", "fire-red-asr", "parakeet", "sensevoice"}:
        # Map to canonical names expected by sherpa helpers
        canon = {"qwen3asr": "Qwen3-ASR", "qwen3-asr": "Qwen3-ASR", "fireredasr": "FireRedASR", "fire-red-asr": "FireRedASR",
                 "parakeet": "Parakeet", "sensevoice": "SenseVoice"}[lower_engine]
        segments = run_sherpa(canon, model_dir, audio, language, requested_device, glossary_path, custom_prompt)
    elif engine in {"Qwen3-ASR", "FireRedASR", "Parakeet", "SenseVoice"}:
        segments = run_sherpa(engine, model_dir, audio, language, requested_device, glossary_path, custom_prompt)
    else:
        raise RuntimeError(f"unsupported engine: {engine}")
    write_srt(segments, output)


if __name__ == "__main__":
    try:
        main(sys.argv)
    except Exception as exc:
        print(f"local ASR runner error: {exc}", file=sys.stderr)
        raise
