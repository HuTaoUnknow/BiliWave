from pathlib import Path
import json
import sys

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'workers'))
from ocr_backend import create_engine
import cv2
import numpy as np

engine, info, _ = create_engine({'ocr_device': 'gpu'}, lambda *_: None)
image = np.zeros((100, 640, 3), np.uint8)
cv2.putText(image, 'BiliWave GPU', (30, 65), cv2.FONT_HERSHEY_SIMPLEX, 1.5, (255, 255, 255), 2)
result, _ = engine(image, use_cls=False)
text = ' '.join(row[1] for row in result or [])
if 'BiliWave' not in text:
    raise RuntimeError('GPU test caption was not recognized: ' + text)
info['test_text'] = text
print(json.dumps(info, ensure_ascii=False, indent=2))
