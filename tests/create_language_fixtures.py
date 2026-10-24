"""Synthetic, original captions for Russian/Italian OCR integration checks."""
from pathlib import Path
import json
import subprocess
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / '.cache/language-validation'
CAPTIONS = {
    'ru': ['Привет, мир!', 'Всё начинается с мечты.'],
    'it': ['È già una bella città.', "L'amore è qui con noi."],
}


def create():
    WORK.mkdir(exist_ok=True)
    font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 48)
    for language, lines in CAPTIONS.items():
        for index, text in enumerate([lines[0], lines[0], '', lines[1], lines[1], '']):
            image = Image.new('RGB', (900, 600), 'black')
            draw = ImageDraw.Draw(image)
            draw.text((30, 35), 'OUTSIDE CROP', fill='white', font=font)
            draw.text((450, 470), text, anchor='mt', fill='white', font=font)
            image.save(WORK / f'{language}-{index:02d}.png')
        subprocess.run([str(ROOT/'tools/ffmpeg.exe'), '-v', 'error', '-y', '-framerate', '1',
                        '-i', str(WORK/f'{language}-%02d.png'), '-c:v', 'libx264', '-pix_fmt', 'yuv420p',
                        '-r', '10', str(WORK/f'{language}-subtitles.mp4')], check=True)
    (WORK/'captions.json').write_text(json.dumps(CAPTIONS, ensure_ascii=False, indent=2), 'utf-8')


if __name__ == '__main__':
    create()
