param([string]$QtTools='D:\PC\EXE\QtCreator\Tools')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $project
New-Item -ItemType Directory -Force -Path tools,models,.cache,docs/licenses | Out-Null

function Get-Checked([string]$Url,[string]$Destination,[string]$Sha256){
    if((Test-Path -LiteralPath $Destination) -and (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLower() -eq $Sha256){return}
    $partial="$Destination.download"
    & curl.exe -sSfL --retry 2 --connect-timeout 25 --max-time 1800 -o $partial $Url
    if($LASTEXITCODE){throw "Download failed: $Url"}
    if((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLower() -ne $Sha256){throw "Checksum mismatch: $Destination"}
    Move-Item -LiteralPath $partial -Destination $Destination -Force
}

Get-Checked 'https://github.com/yt-dlp/yt-dlp/releases/download/2026.08.19/yt-dlp.exe' 'tools/yt-dlp.exe' '66674953fe251b89f4d08c5f0e35e0728679bd67ab3d7d05c0562af101dd3e7a'
Get-Checked 'https://github.com/GyanD/codexffmpeg/releases/download/9.0.2/ffmpeg-9.0.2-essentials_build.zip' '.cache/ffmpeg-github.zip' '60f467265b1e312373dbcd92200c2618a74850f98d3d078e94296bb3fa2047ba'
if(-not (Test-Path -LiteralPath .cache/ffmpeg-extracted/ffmpeg-9.0.2-essentials_build/bin/ffmpeg.exe)){
    Expand-Archive -LiteralPath .cache/ffmpeg-github.zip -DestinationPath .cache/ffmpeg-extracted -Force
}
Copy-Item -LiteralPath .cache/ffmpeg-extracted/ffmpeg-9.0.2-essentials_build/bin/ffmpeg.exe,.cache/ffmpeg-extracted/ffmpeg-9.0.2-essentials_build/bin/ffprobe.exe -Destination tools -Force
Copy-Item -LiteralPath .cache/ffmpeg-extracted/ffmpeg-9.0.2-essentials_build/LICENSE -Destination docs/licenses/LICENSE -Force
Write-Host 'Audio dependencies downloaded and verified. Subtitle OCR and lyric alignment use setup-lyric-tools.ps1.'
