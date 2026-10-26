param(
    [string]$Compiler = '',
    [string]$OutputDirectory = '',
    [switch]$Lite,
    [switch]$CopyToDesktop
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$payload = Join-Path $project $(if ($Lite) { 'release-lite' } else { 'release-screen' })
$buildDirectory = if ($Lite) { 'build-lite' } else { 'build' }
if (-not $Compiler) {
    $Compiler = Join-Path $project '.cache\installer-tools\inno\ISCC.exe'
}
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Inno Setup ISCC.exe was not found. Pass -Compiler with its path.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $project 'dist' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$cmakeText = Get-Content -LiteralPath (Join-Path $project 'CMakeLists.txt') -Raw
$version = [regex]::Match($cmakeText, 'project\(BiliWave VERSION ([0-9.]+)').Groups[1].Value
if (-not $version) { throw 'Application version was not found in CMakeLists.txt.' }
$requiredFiles = @('BiliWave.exe','platforms\qwindows.dll','Qt6Core.dll','multimedia\ffmpegmediaplugin.dll',
    'tools\ffmpeg.exe','tools\ffprobe.exe','tools\yt-dlp.exe')
if ($Lite) {
    foreach ($forbidden in @('models','workers','tools\lyrics-runtime','tools\ocr-gpu','tools\whisper-cli.exe')) {
        if (Test-Path -LiteralPath (Join-Path $payload $forbidden)) { throw "Forbidden Lite payload: $forbidden" }
    }
} else {
    $requiredFiles += @('tools\lyrics-runtime\python.exe','workers\lyric_worker.py',
        'workers\languages.py','models\ocr\cyrillic_PP-OCRv5_rec_mobile.onnx','models\ocr\latin_PP-OCRv5_rec_mobile.onnx')
}
foreach ($obsolete in @('models\small.pt','models\ggml-small.bin','tools\whisper-cli.exe',
    'tools\lyrics-runtime\Lib\site-packages\torch','tools\lyrics-runtime\Lib\site-packages\stable_whisper')) {
    if (Test-Path -LiteralPath (Join-Path $payload $obsolete)) { throw "Obsolete transcription payload: $obsolete" }
}
foreach ($relative in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $payload $relative))) { throw "Release payload missing: $relative" }
}
if ((Get-FileHash -LiteralPath (Join-Path $project "$buildDirectory\BiliWave.exe")).Hash -ne
    (Get-FileHash -LiteralPath (Join-Path $payload 'BiliWave.exe')).Hash) {
    throw 'Release executable differs from the current build. Deploy the build first.'
}
$script = Join-Path $project 'packaging\BiliWave.iss'
$log = Join-Path $OutputDirectory $(if ($Lite) { 'installer-lite-build.log' } else { 'installer-build.log' })
$liteValue = if ($Lite) { 1 } else { 0 }
& $Compiler "/DAppVersion=$version" "/DPayloadDir=$payload" "/DPackageOutputDir=$OutputDirectory" "/DLiteEdition=$liteValue" $script *> $log
if ($LASTEXITCODE) {
    Get-Content -LiteralPath $log -Tail 30
    throw "Installer compiler failed with exit code $LASTEXITCODE."
}
$prefix = if ($Lite) { 'BiliWave-Lite' } else { 'BiliWave' }
$installer = Join-Path $OutputDirectory "$prefix-$version-Windows-x64-Setup.exe"
if (-not (Test-Path -LiteralPath $installer)) { throw 'Installer output is missing.' }
$digest = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
$digestFile = "$installer.sha256"
[IO.File]::WriteAllText($digestFile, "$digest  $([IO.Path]::GetFileName($installer))`n", [Text.UTF8Encoding]::new($false))
if ($CopyToDesktop) {
    $desktop = [Environment]::GetFolderPath('Desktop')
    Copy-Item -LiteralPath $installer,$digestFile -Destination $desktop -Force
}
Get-Item -LiteralPath $installer | Select-Object FullName,Length
Write-Output "SHA-256: $digest"
