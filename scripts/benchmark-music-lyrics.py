"""Real-song OCR/alignment evaluation. Media and reference lyrics stay in .cache.

Use --sheets to inspect the downloaded videos, --run ocr/align to run workers.
Run one worker at a time so GPU and CPU contention do not skew timings.
"""
import argparse
import hashlib
import json
from pathlib import Path
from statistics import mean
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.cache/music-benchmark'
PYTHON = ROOT / 'tools/lyrics-runtime/python.exe'
FFMPEG = ROOT / 'tools/ffmpeg.exe'


def read(path):
    return json.loads(path.read_text('utf-8-sig'))


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), 'utf-8')


def sheets():
    import cv2
    from PIL import Image, ImageDraw
    for language in ('zh', 'en', 'ru', 'zh-single'):
        cap = cv2.VideoCapture(str(CACHE / (language + '.mp4')))
        duration = cap.get(cv2.CAP_PROP_FRAME_COUNT) / cap.get(cv2.CAP_PROP_FPS)
        stamps = [5, 15, 25, 40, 60, 85, 110, min(140, duration - 2)]
        sheet = Image.new('RGB', (960, 4 * 294), '#222222')
        draw = ImageDraw.Draw(sheet)
        for index, stamp in enumerate(stamps):
            cap.set(cv2.CAP_PROP_POS_MSEC, stamp * 1000)
            ok, frame = cap.read()
            if not ok:
                continue
            thumb = Image.fromarray(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
            thumb.thumbnail((480, 270))
            x, y = (index % 2) * 480, (index // 2) * 294
            sheet.paste(thumb, (x, y + 24))
            draw.text((x + 8, y + 4), f'{language}: {stamp:.2f} s', fill='white')
        cap.release()
        sheet.save(CACHE / (language + '-sheet.jpg'))


def audit_sheets():
    """Independent fixed-grid frame sheets, not OCR-selected representatives."""
    import cv2
    from PIL import Image, ImageDraw
    for sample in read(CACHE / 'manifest.json')['samples']:
        language = sample['language']
        cap = cv2.VideoCapture(str(CACHE / sample.get('video', language + '.mp4')))
        duration = cap.get(cv2.CAP_PROP_FRAME_COUNT) / cap.get(cv2.CAP_PROP_FPS)
        width, height = int(cap.get(3)), int(cap.get(4))
        x, y, w, h = sample['crop']
        for page, first in enumerate(range(0, int(duration), 60)):
            sheet = Image.new('RGB', (1200, 10 * 100), '#222222')
            draw = ImageDraw.Draw(sheet)
            for index, stamp in enumerate(range(first, min(first+60, int(duration)), 2)):
                cap.set(cv2.CAP_PROP_POS_MSEC, stamp*1000)
                ok, frame = cap.read()
                if not ok:
                    continue
                crop = frame[int(height*y):round(height*(y+h)), int(width*x):round(width*(x+w))]
                thumb = Image.fromarray(cv2.cvtColor(crop, cv2.COLOR_BGR2RGB))
                thumb.thumbnail((400, 78))
                left, top = (index % 3)*400, (index // 3)*100
                sheet.paste(thumb, (left, top+20))
                draw.text((left+4, top+2), f'{language} {stamp:.2f} s', fill='white')
            sheet.save(CACHE / f'{language}-audit-{page}.jpg')
        cap.release()


def run(kind, tag, selected=None):
    manifest = read(CACHE / 'manifest.json')
    for sample in manifest['samples']:
        language = sample['language']
        if selected and language != selected:
            continue
        work = CACHE / (language + '-' + kind + '-' + tag)
        work.mkdir(parents=True, exist_ok=True)
        config = dict(operation=kind, work=str(work), cancel=str(work / 'cancel'),
                      result=str(work / 'result.json'), ffmpeg=str(FFMPEG))
        if kind == 'ocr':
            config.update(video=str(CACHE / sample.get('video', language + '.mp4')), crop=sample['crop'],
                          interval=sample.get('interval', .5), ocr_language=sample['ocr_language'],
                          ocr_device='gpu', text_similarity=.6, change_threshold=.22,
                          gpu_runtime=str(ROOT/'tools/ocr-gpu'), ocr_models=str(ROOT/'models/ocr'),
                          song_title=sample.get('title', ''), song_artist=sample.get('artist', ''))
        else:
            reference = read(CACHE / sample['reference'])
            config.update(audio=str(CACHE / sample.get('video', language + '.mp4')), language=language,
                          text=reference.get('alignment_text', '\n'.join(cue['text'] for cue in reference['cues'])),
                          model=str(ROOT / 'models/small.pt'))
            if tag == 'tuned':
                # Use machine OCR times, never the manually annotated reference
                # times. Correct lyrics are the input in both alignment runs.
                config['timing_hints'] = read(CACHE / (language+'-ocr-tuned/result.json'))['events']
        write(work / 'config.json', config)
        start = time.perf_counter()
        with (work / 'worker.log').open('wb') as log:
            worker = CACHE / 'baseline-workers/lyric_worker.py' if tag == 'baseline' else ROOT / 'workers/lyric_worker.py'
            child = subprocess.run([str(PYTHON), str(worker),
                                    str(work / 'config.json')], stdout=log, stderr=log,
                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        result = read(work / 'result.json')
        measurement = dict(language=language, kind=kind, tag=tag, ok=result['ok'],
                           seconds=round(time.perf_counter()-start, 3),
                           stats=result.get('stats'), error=result.get('error'))
        write(work / 'measurement.json', measurement)
        print(json.dumps(measurement, ensure_ascii=False), flush=True)
        if child.returncode:
            raise RuntimeError(str(work / 'worker.log'))


def boundaries():
    import cv2
    from PIL import Image, ImageDraw
    for sample in read(CACHE / 'manifest.json')['samples']:
        language = sample['language']
        cap = cv2.VideoCapture(str(CACHE / sample.get('video', language+'.mp4')))
        width, height = int(cap.get(3)), int(cap.get(4))
        x, y, w, h = sample['crop']
        cues = read(CACHE / sample['reference'])['cues']
        for page, offset in enumerate(range(0, len(cues), 10)):
            sheet = Image.new('RGB', (1600, 10*96), '#222222')
            draw = ImageDraw.Draw(sheet)
            for row, cue in enumerate(cues[offset:offset+10]):
                for col, delta in enumerate([-.5, -.25, 0, .25, .5]):
                    stamp = cue['start'] + delta
                    cap.set(cv2.CAP_PROP_POS_MSEC, stamp*1000)
                    ok, frame = cap.read()
                    if not ok:
                        continue
                    crop = frame[int(height*y):round(height*(y+h)), int(width*x):round(width*(x+w))]
                    thumb = Image.fromarray(cv2.cvtColor(crop, cv2.COLOR_BGR2RGB))
                    thumb.thumbnail((320, 72))
                    left, top = col*320, row*96
                    sheet.paste(thumb, (left, top+22))
                    draw.text((left+3, top+3), f'{offset+row+1}: {stamp:.2f}', fill='white')
            sheet.save(CACHE / f'{language}-boundaries-{page}.png')
        cap.release()


def edit_distance(a, b):
    previous = list(range(len(b)+1))
    for i, x in enumerate(a, 1):
        current = [i]
        for j, y in enumerate(b, 1):
            current.append(min(current[-1]+1, previous[j]+1, previous[j-1]+(x != y)))
        previous = current
    return previous[-1]


def match_cues(reference, actual):
    """Monotonic edit alignment, gated to three seconds for visual OCR cues.
    Preserve insertions/deletions; do not give duplicate occurrences free credit.
    """
    sys.path.insert(0, str(ROOT/'workers'))
    from subtitle_segments import text_key
    n, m = len(reference), len(actual)
    cost = [[float('inf')]*(m+1) for _ in range(n+1)]
    step = {}
    cost[0][0] = 0
    for i in range(n+1):
        for j in range(m+1):
            candidates = []
            if i:
                candidates.append((cost[i-1][j]+1, i-1, j))
            if j:
                candidates.append((cost[i][j-1]+1, i, j-1))
            if i and j and abs(reference[i-1]['start']-actual[j-1]['start']) < 3:
                a, b = text_key(reference[i-1]['text']), text_key(actual[j-1]['text'])
                ratio = edit_distance(a,b)/max(1,len(a),len(b))
                if ratio < .5:
                    candidates.append((cost[i-1][j-1]+ratio+.001*abs(reference[i-1]['start']-actual[j-1]['start']), i-1,j-1))
            if candidates:
                best, pi, pj = min(candidates)
                cost[i][j], step[i,j] = best, (pi,pj)
    pairs, missing, extra = [], [], []
    i,j = n,m
    while i or j:
        pi,pj = step[i,j]
        if pi == i:
            extra.append(j-1)
        elif pj == j:
            missing.append(i-1)
        else:
            pairs.append((i-1,j-1))
        i,j = pi,pj
    return list(reversed(pairs)), missing, extra


def timing_summary(errors):
    ordered = sorted(errors)
    return dict(mean_seconds=round(mean(errors),3), max_seconds=round(max(errors),3),
                p95_seconds=round(ordered[min(len(ordered)-1, int((len(ordered)-1)*.95+.5))],3),
                over_2_seconds=sum(e>2 for e in errors))


def score():
    sys.path.insert(0, str(ROOT/'workers'))
    from subtitle_segments import text_key
    manifest = read(CACHE/'manifest.json')
    records = []
    for sample in manifest['samples']:
        language = sample['language']
        reference = read(CACHE/sample['reference'])
        if not reference.get('timing_reviewed'):
            raise ValueError('Reference frame times have not been manually reviewed')
        cues = reference['cues']
        video = CACHE/sample.get('video', language+'.mp4')
        record = dict(language=language, url=sample['url'], video_sha256=hashlib.sha256(video.read_bytes()).hexdigest(),
                      crop=sample['crop'], reference_cues=len(cues),
                      reference_sha256=hashlib.sha256((CACHE/sample['reference']).read_bytes()).hexdigest(),
                      baseline={}, tuned={})
        before_audio = CACHE/f'{language}-align-baseline/alignment.wav'
        after_audio = CACHE/f'{language}-align-tuned/alignment.wav'
        before_hash = hashlib.sha256(before_audio.read_bytes()).hexdigest()
        assert before_hash == hashlib.sha256(after_audio.read_bytes()).hexdigest(), 'Baseline/tuned audio differs'
        record['analysis_audio_sha256'] = before_hash
        for tag in ('baseline','tuned'):
            ocr = read(CACHE/f'{language}-ocr-{tag}/result.json')
            pairs, missing, extra = match_cues(cues, ocr['events'])
            edits = sum(edit_distance(text_key(cues[i]['text']),text_key(ocr['events'][j]['text'])) for i,j in pairs)
            edits += sum(len(text_key(cues[i]['text'])) for i in missing)
            edits += sum(len(text_key(ocr['events'][j]['text'])) for j in extra)
            total = sum(len(text_key(c['text'])) for c in cues)
            result = dict(output_cues=len(ocr['events']), matched_cues=len(pairs), missing_cues=len(missing), extra_cues=len(extra),
                          exact_cues=sum(text_key(cues[i]['text']) == text_key(ocr['events'][j]['text']) for i,j in pairs),
                          character_errors=edits, reference_characters=total, cer=round(edits/total,5),
                          timing=timing_summary([abs(cues[i]['start']-ocr['events'][j]['start']) for i,j in pairs]),
                          stats=ocr['stats'])
            if language == 'ru':
                from subtitle_segments import caption_parts
                result['russian_exact_cues'] = sum(caption_parts(cues[i]['text'])[0] == caption_parts(ocr['events'][j]['text'])[0] for i,j in pairs)
                result['chinese_translation_exact_cues'] = sum(caption_parts(cues[i]['text'])[1] == caption_parts(ocr['events'][j]['text'])[1] for i,j in pairs)
            alignment = read(CACHE/f'{language}-align-{tag}/result.json')
            assert len(alignment['lines']) == len(cues), 'Never silently drop alignment lines'
            errors = [abs(a['start']-b['start']) for a,b in zip(cues,alignment['lines'])]
            measurement = CACHE/f'{language}-align-{tag}/measurement.json'
            record[tag] = dict(ocr=result, alignment=dict(timing=timing_summary(errors),
                                caption_fallback_lines=alignment.get('caption_fallback_lines',[]),
                                review_lines=[i+1 for i,row in enumerate(alignment['lines']) if row['review']],
                                wall_seconds=read(measurement)['seconds'] if measurement.exists() else None))
        records.append(record)
    report = dict(date='2026-10-22', reference='Manually reviewed video subtitle appearance, quarter-second brackets (+/-0.125 s). Not an independent vocal-onset gold standard.',
                  matching_input='Same video audio stream, corrected existing lyrics; tuned alignment hints are raw machine OCR times, NOT reference times.',
                  normalization='NFKC, casefold, letters/numbers only; spaces and punctuation excluded. CER includes unmatched cue insertions/deletions.',
                  samples=records)
    write(ROOT/'docs/music-lyrics-benchmark.json', report)
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sheets', action='store_true')
    parser.add_argument('--audit', action='store_true')
    parser.add_argument('--boundaries', action='store_true')
    parser.add_argument('--run', choices=['ocr', 'align'])
    parser.add_argument('--tag', default='baseline')
    parser.add_argument('--language', choices=['zh', 'en', 'ru'])
    parser.add_argument('--score', action='store_true')
    args = parser.parse_args()
    if args.sheets:
        sheets()
    if args.audit:
        audit_sheets()
    if args.boundaries:
        boundaries()
    if args.run:
        run(args.run, args.tag, args.language)
    if args.score:
        score()
