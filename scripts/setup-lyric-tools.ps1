param([switch]$CpuOnly)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $project
New-Item -ItemType Directory -Force -Path tools/lyrics-runtime,models,.cache | Out-Null
function Get-LyricResource([string]$Url,[string]$Destination,[string]$Sha256){
    if((Test-Path -LiteralPath $Destination) -and (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLower() -eq $Sha256){return}
    $partial="$Destination.download"
    & curl.exe -sSfL --retry 2 --connect-timeout 25 --max-time 1800 -o $partial $Url
    if($LASTEXITCODE){throw "Download failed: $Url"}
    if((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLower() -ne $Sha256){throw "Checksum mismatch: $Destination"}
    Move-Item -LiteralPath $partial -Destination $Destination -Force
}
Get-LyricResource 'https://www.python.org/ftp/python/3.11.9/python-3.11.9-embed-amd64.zip' '.cache/python-3.11.9-embed-amd64.zip' '009d6bf7e3b2ddca3d784fa09f90fe54336d5b60f0e0f305c37f400bf83cfd3b'
if(-not (Test-Path -LiteralPath tools/lyrics-runtime/python.exe)){
    Expand-Archive -LiteralPath .cache/python-3.11.9-embed-amd64.zip -DestinationPath tools/lyrics-runtime -Force
}
Set-Content -LiteralPath tools/lyrics-runtime/python311._pth -Encoding ascii -Value @('python311.zip','.','Lib/site-packages','import site')
if(-not (Test-Path -LiteralPath tools/lyrics-runtime/Lib/site-packages/pip)){
    & curl.exe -sSfL --connect-timeout 25 --max-time 180 -o .cache/get-pip.py https://bootstrap.pypa.io/get-pip.py
    if($LASTEXITCODE){throw 'pip bootstrap download failed'}
    & tools/lyrics-runtime/python.exe .cache/get-pip.py --disable-pip-version-check
    if($LASTEXITCODE){throw 'pip bootstrap failed'}
}
& tools/lyrics-runtime/python.exe -m pip install --disable-pip-version-check --no-warn-script-location torch==2.6.0+cpu torchaudio==2.6.0+cpu --index-url https://download.pytorch.org/whl/cpu
if($LASTEXITCODE){throw 'CPU inference runtime install failed'}
& tools/lyrics-runtime/python.exe -m pip install --disable-pip-version-check --no-warn-script-location --no-build-isolation -r scripts/lyric-requirements.txt
if($LASTEXITCODE){throw 'Lyric dependencies install failed'}
Get-LyricResource 'https://openaipublic.azureedge.net/main/whisper/models/9ecf779972d90ba49c06d968637d720dd632c55bbf19d441fb42bf17a411e794/small.pt' 'models/small.pt' '9ecf779972d90ba49c06d968637d720dd632c55bbf19d441fb42bf17a411e794'
& tools/lyrics-runtime/python.exe -c "import stable_whisper; from rapidocr_onnxruntime import RapidOCR; RapidOCR(); print('Alignment and OCR dependencies ready')"
if($LASTEXITCODE){throw 'Lyric dependency verification failed'}
if (-not $CpuOnly) { & "$PSScriptRoot\setup-ocr-gpu.ps1" }
& "$PSScriptRoot\setup-ocr-languages.ps1"
