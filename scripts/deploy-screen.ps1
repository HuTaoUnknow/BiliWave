param([string]$QtRoot='D:\PC\EXE\QtCreator\6.11.1\mingw_64')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$release = Join-Path $project 'release-screen'
New-Item -ItemType Directory -Force -Path $release | Out-Null
Copy-Item -LiteralPath "$project\build\BiliWave.exe" -Destination $release -Force
& "$QtRoot\bin\windeployqt.exe" --release --no-translations --compiler-runtime "$release\BiliWave.exe"
if ($LASTEXITCODE) { throw 'Screen edition Qt deployment failed' }
$runtime = Join-Path $release 'tools\lyrics-runtime'
$packages = Join-Path $runtime 'Lib\site-packages'
New-Item -ItemType Directory -Force -Path $packages | Out-Null
Get-ChildItem -LiteralPath "$project\tools\lyrics-runtime" -File | Copy-Item -Destination $runtime -Force
# An explicit OCR dependency list keeps audio models, Torch and developer tools
# out of the published screen edition; the development runtime is untouched.
$allowed = @('numpy','numpy.libs','cv2','PIL','onnxruntime','rapidocr_onnxruntime','pyclipper',
    'shapely','shapely.libs','yaml','_yaml','six.py','tqdm','colorama','flatbuffers',
    'google','packaging','typing_extensions.py')
foreach ($name in $allowed) {
    $source = Join-Path $project "tools\lyrics-runtime\Lib\site-packages\$name"
    if (-not (Test-Path -LiteralPath $source)) { throw "OCR dependency missing: $name" }
    Copy-Item -LiteralPath $source -Destination $packages -Recurse -Force
}
$distributions = @('numpy','opencv_python','pillow','onnxruntime','rapidocr_onnxruntime','pyclipper',
    'shapely','pyyaml','six','tqdm','colorama','flatbuffers','protobuf','packaging','typing_extensions')
foreach ($distribution in $distributions) {
    Get-ChildItem -LiteralPath "$project\tools\lyrics-runtime\Lib\site-packages" -Directory -Filter "$distribution-*.dist-info" |
        Copy-Item -Destination $packages -Recurse -Force
}
foreach ($folder in @('workers','docs')) {
    & robocopy.exe (Join-Path $project $folder) (Join-Path $release $folder) /E /COPY:DAT /DCOPY:T /R:1 /W:1 /NFL /NDL /NJH /NJS /NP /XJ /XD __pycache__ /XF *.pyc
    if ($LASTEXITCODE -gt 7) { throw "Deployment failed: $folder" }
}
New-Item -ItemType Directory -Force -Path "$release\models" | Out-Null
Copy-Item -LiteralPath "$project\models\ocr" -Destination "$release\models" -Recurse -Force
Copy-Item -LiteralPath "$project\tools\ocr-gpu" -Destination "$release\tools" -Recurse -Force
foreach ($tool in @('ffmpeg.exe','ffprobe.exe','yt-dlp.exe')) {
    Copy-Item -LiteralPath "$project\tools\$tool" -Destination "$release\tools" -Force
}
Copy-Item -LiteralPath "$project\README.md","$project\THIRD_PARTY_NOTICES.md" -Destination $release -Force
foreach ($forbidden in @('models\small.pt','models\ggml-small.bin','tools\whisper-cli.exe',
    'tools\lyrics-runtime\Lib\site-packages\torch','tools\lyrics-runtime\Lib\site-packages\stable_whisper')) {
    if (Test-Path -LiteralPath (Join-Path $release $forbidden)) { throw "Forbidden screen payload: $forbidden" }
}
& "$runtime\python.exe" -I -c 'import cv2, numpy, rapidocr_onnxruntime, onnxruntime; print(onnxruntime.__version__)'
if ($LASTEXITCODE) { throw 'Screen OCR runtime import failed' }
$global:LASTEXITCODE = 0
