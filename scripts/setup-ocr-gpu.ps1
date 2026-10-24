param()
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$python = Join-Path $project 'tools\lyrics-runtime\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Prepare the lyric runtime first with setup-lyric-tools.ps1.' }
& $python -m pip install --disable-pip-version-check --no-warn-script-location --no-deps --upgrade --require-hashes --target "$project\tools\ocr-gpu" -r "$project\scripts\ocr-gpu-requirements.txt" --report "$project\.cache\ocr-gpu-install.json"
if ($LASTEXITCODE) { throw 'GPU runtime installation failed' }
& $python "$project\scripts\verify-ocr-gpu.py"
if ($LASTEXITCODE) { throw 'GPU OCR verification failed' }
