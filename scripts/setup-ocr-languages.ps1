param()
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$destination = Join-Path $project 'models\ocr'
New-Item -ItemType Directory -Force -Path $destination | Out-Null
$models = @(
    @{Name='cyrillic_PP-OCRv5_rec_mobile.onnx'; Hash='90f761b4bfcce0c8c561c0cb5c887b0971d3ec01c32164bdf7374a35b0982711'},
    @{Name='latin_PP-OCRv5_rec_mobile.onnx'; Hash='b20bd37c168a570f583afbc8cd7925603890efbcdc000a59e22c269d160b5f5a'}
)
foreach ($model in $models) {
    $target = Join-Path $destination $model.Name
    if ((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() -eq $model.Hash) { continue }
    $url = 'https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.10.0/onnx/PP-OCRv5/rec/' + $model.Name
    $partial = $target + '.download'
    & curl.exe --ssl-revoke-best-effort -sSfL --retry 2 --connect-timeout 25 --max-time 300 -o $partial $url
    if ($LASTEXITCODE) { throw "OCR model download failed: $($model.Name)" }
    if ((Get-FileHash -LiteralPath $partial).Hash.ToLowerInvariant() -ne $model.Hash) { throw 'OCR model checksum mismatch' }
    Move-Item -LiteralPath $partial -Destination $target -Force
}
$license = Join-Path $destination 'LICENSE-PaddleOCR.txt'
$licenseHash = '3840c5c0c61c294264d2dd77b8777be6ddd90121ef4e0e64abcd22edea581d6e'
if (-not (Test-Path -LiteralPath $license) -or (Get-FileHash -LiteralPath $license).Hash.ToLowerInvariant() -ne $licenseHash) {
    & curl.exe -sSfL --connect-timeout 20 --max-time 60 -o "$license.download" 'https://raw.githubusercontent.com/PaddlePaddle/PaddleOCR/main/LICENSE'
    if ($LASTEXITCODE -or (Get-FileHash -LiteralPath "$license.download").Hash.ToLowerInvariant() -ne $licenseHash) { throw 'PaddleOCR license verification failed' }
    Move-Item -LiteralPath "$license.download" -Destination $license -Force
}
Get-ChildItem -LiteralPath $destination -File | Select-Object Name,Length
