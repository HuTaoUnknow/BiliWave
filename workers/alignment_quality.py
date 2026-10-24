"""Validate same-version timing hints and expose alignment uncertainty."""
import math
import re


def timing_hints(config, lines, duration):
    """Hints are opt-in, never inferred from a similar track duration.

    Require a complete ordered correspondence, keeping repeated occurrences.
    Allow small OCR corrections, but reject missing/extra lines and languages.
    """
    from bilingual import alignment_pairs
    from subtitle_segments import caption_similarity
    hints = config.get('timing_hints')
    if hints is None and config.get('timed_text'):
        hints = []
        for raw in config['timed_text'].splitlines():
            match = re.match(r'^\s*\[(\d+):(\d+(?:\.\d+)?)\](.*)$', raw)
            if not match or not match[3].strip():
                continue
            stamp = int(match[1])*60 + float(match[2])
            if hints and abs(stamp-hints[-1]['start']) < .001:
                hints[-1]['text'] += '\n' + match[3].strip()
            else:
                hints.append(dict(start=stamp, text=match[3].strip()))
    if hints is None:
        return []
    if not isinstance(hints, list) or len(hints) != len(lines):
        raise ValueError('定位时间与歌词行数不一致；请保留同一版本的逐句时间和副歌次数，或关闭时间定位')
    checked, last = [], -1.
    for index, (hint, text) in enumerate(zip(hints, lines)):
        start = float(hint['start'])
        original, _ = alignment_pairs(hint['text'].splitlines())
        original = ' '.join(original)
        if (not math.isfinite(start) or start < 0 or start <= last or start >= duration
                or caption_similarity(original, text) < .85):
            raise ValueError(f'第 {index+1} 句的定位时间或文字不对应；请检查演唱版本和副歌次数')
        end = float(hint.get('observed_end', hint.get('end', start+12)))
        if not math.isfinite(end) or end <= start:
            raise ValueError(f'第 {index+1} 句的定位结束时间无效')
        checked.append(dict(start=start, end=min(duration, end), text=text))
        last = start
    return checked


def segment_data(segment, offset=0):
    words = segment.words or []
    return dict(start=float(segment.start)+offset, end=float(segment.end)+offset,
                words=[dict(start=float(w.start)+offset, end=float(w.end)+offset,
                            probability=float(getattr(w, 'probability', 0) or 0)) for w in words])


def review_reasons(row, previous=-1):
    start, end, words = row['start'], row['end'], row.get('words', [])
    reasons = []
    if end-start < .08 or start <= previous:
        reasons.append('时间重叠或句长过短')
    if any(w['end'] <= w['start'] for w in words):
        reasons.append('部分字词未能定位')
    if end-start > 12 or any(w['end']-w['start'] > 4 for w in words):
        reasons.append('异常长句或长字词，可能跨过前奏/间奏')
    if words and sum(w['probability'] for w in words)/len(words) < .5:
        reasons.append('音频匹配分数偏低')
    return reasons
