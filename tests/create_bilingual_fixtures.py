"""Original bilingual captions and real-model OCR checks (CPU and GPU)."""
from pathlib import Path
import json
import subprocess
import sys
import unicodedata
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / '.cache/bilingual-validation'
CAPTIONS = {
    'en_zh': [('Hello, world!', '你好，世界！'), ('A new day begins.', '新的一天开始了。')],
    'ru_zh': [('Привет, мир!', '你好，世界！'), ('Всё начинается с мечты.', '一切从梦想开始。')],
    'it_zh': [('È già una bella città.', '这已经是一座美丽的城市。'), ("L'amore è qui con noi.", '爱与我们同在。')],
}


def normalized(text):
    return ''.join(c for c in unicodedata.normalize('NFC', text).casefold() if c.isalnum())


def create():
    WORK.mkdir(exist_ok=True)
    foreign = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 44)
    chinese = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 40)
    for language, pairs in CAPTIONS.items():
        for index, pair in enumerate([pairs[0], pairs[0], ('', ''), pairs[1], pairs[1], ('', '')]):
            image = Image.new('RGB', (1000, 600), 'black')
            draw = ImageDraw.Draw(image)
            draw.text((500, 425), pair[0], anchor='mt', fill='white', font=foreign)
            draw.text((500, 500), pair[1], anchor='mt', fill='white', font=chinese)
            image.save(WORK / f'{language}-{index:02d}.png')
        subprocess.run([str(ROOT/'tools/ffmpeg.exe'), '-v', 'error', '-y', '-framerate', '1',
                        '-i', str(WORK/f'{language}-%02d.png'), '-c:v', 'libx264', '-pix_fmt', 'yuv420p',
                        '-r', '10', str(WORK/f'{language}.mp4')], check=True)


def verify():
    records = []
    for language, pairs in CAPTIONS.items():
        for device in ['cpu', 'gpu']:
            directory = WORK / f'{language}-{device}'
            directory.mkdir(exist_ok=True)
            config = dict(operation='ocr', work=str(directory), cancel=str(directory/'cancel'),
                          video=str(WORK/f'{language}.mp4'), ffmpeg=str(ROOT/'tools/ffmpeg.exe'),
                          ffprobe=str(ROOT/'tools/ffprobe.exe'), ocr_language=language, ocr_device=device,
                          result=str(directory/'result.json'))
            path = directory/'config.json'
            path.write_text(json.dumps(config, ensure_ascii=False), 'utf-8')
            process = subprocess.run([sys.executable, str(ROOT/'workers/lyric_worker.py'), str(path)],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, encoding='utf-8')
            if process.returncode:
                raise RuntimeError(process.stdout + process.stderr)
            result = json.loads((directory/'result.json').read_text('utf-8'))
            actual = [e['text'].split('\n') for e in result['events']]
            assert [[normalized(t) for t in pair] for pair in actual] == [
                [normalized(t) for t in pair] for pair in pairs], (language, device, actual)
            records.append(dict(language=language, device=device, events=result['events'],
                                lrc=result['lrc'], backend=result['backend'], stats=result['stats']))
            print(language, device, 'PASS', flush=True)
    (ROOT/'docs/bilingual-ocr-validation.json').write_text(json.dumps(records, ensure_ascii=False, indent=2), 'utf-8')


if __name__ == '__main__':
    create()
    if '--verify' in sys.argv:
        verify()
