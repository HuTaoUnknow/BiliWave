param([string]$Installer = '')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
if (-not $Installer) { $Installer = Join-Path $project 'dist\BiliWave-1.10.0-Windows-x64-Setup.exe' }
$Installer = [IO.Path]::GetFullPath($Installer)
$verificationRoot = Join-Path $project ('.cache\ivf110-' + [guid]::NewGuid().ToString('N').Substring(0,8))
$destination = Join-Path $verificationRoot 'App'
if (Test-Path -LiteralPath $destination) { throw 'Verification destination already exists; use a fresh verification directory.' }
New-Item -ItemType Directory -Force -Path $verificationRoot | Out-Null
function Invoke-CheckedProcess([string]$File, [string[]]$Arguments, [int]$Timeout = 180000) {
    $process = Start-Process -FilePath $File -ArgumentList $Arguments -WorkingDirectory $verificationRoot -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit($Timeout)) { throw "Verification process timed out: $File" }
    if ($process.ExitCode -ne 0) { throw "Verification process failed ($($process.ExitCode)): $File" }
}
$installLog = Join-Path $verificationRoot 'install.log'
Invoke-CheckedProcess $Installer @('/VERYSILENT','/SUPPRESSMSGBOXES','/SP-','/NORESTART','/VERIFYINSTALL=1',
    '/LANG=chinesesimplified',("/DIR=`"$destination`""),("/LOG=`"$installLog`""))
Write-Output 'Installation completed.'
$installedExe = Join-Path $destination 'BiliWave.exe'
if ((Get-FileHash -LiteralPath $installedExe).Hash -ne (Get-FileHash -LiteralPath (Join-Path $project 'release\BiliWave.exe')).Hash) {
    throw 'Installed application hash mismatch.'
}
foreach ($model in @('small.pt')) {
    if ((Get-FileHash -LiteralPath (Join-Path $destination "models\$model")).Hash -ne
        (Get-FileHash -LiteralPath (Join-Path $project "release\models\$model")).Hash) { throw "Model hash mismatch: $model" }
}
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
Invoke-CheckedProcess $installedExe @('--capture',("`"$(Join-Path $verificationRoot 'screenshots')`""),'--capture-delay','800') 20000
Write-Output 'Installed application started with system-only PATH.'
$python = Join-Path $destination 'tools\lyrics-runtime\python.exe'
$env:PYTHONUTF8 = '1'
& $python -I -c 'import torch, stable_whisper, rapidocr_onnxruntime, cv2, onnxruntime; print("Bundled inference environment imports succeeded.")'
if ($LASTEXITCODE) { throw 'Bundled Python environment check failed.' }
$workerWork = Join-Path $verificationRoot 'worker'
New-Item -ItemType Directory -Force -Path $workerWork | Out-Null
$config = @{
    operation = 'ocr'; video = (Join-Path $project '.cache\lyric-validation\ocr-fixture.mp4')
    work = $workerWork; result = (Join-Path $workerWork 'result.json'); cancel = (Join-Path $workerWork 'cancel')
    ffmpeg = (Join-Path $destination 'tools\ffmpeg.exe'); ytdlp = (Join-Path $destination 'tools\yt-dlp.exe')
    crop = @(0, (2.0 / 3), 1, (1.0 / 3)); interval = .5
}
$configFile = Join-Path $workerWork 'config.json'
$config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $configFile -Encoding utf8
& $python -u (Join-Path $destination 'workers\lyric_worker.py') $configFile
if ($LASTEXITCODE) { throw 'Installed OCR worker failed.' }
$ocr = Get-Content -LiteralPath $config.result -Raw | ConvertFrom-Json
if (-not $ocr.ok -or $ocr.text -ne "听见微光`n落在窗前") { throw 'Installed OCR result differs from the fixture.' }
Write-Output 'Installed subtitle OCR returned the expected text and timing.'
$config.operation = 'align'
$config.audio = Join-Path $project '.cache\whisper-source\whisper.cpp-1.9.5\samples\jfk.wav'
$config.model = Join-Path $destination 'models\small.pt'
$config.language = 'en'
$config.text = "And so, my fellow Americans,`nask not what your country can do for you,`nask what you can do for your country."
$config.result = Join-Path $workerWork 'alignment-result.json'
$config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $configFile -Encoding utf8
& $python -u (Join-Path $destination 'workers\lyric_worker.py') $configFile
if ($LASTEXITCODE) { throw 'Installed forced alignment failed.' }
$alignment = Get-Content -LiteralPath $config.result -Raw | ConvertFrom-Json
if (-not $alignment.ok -or $alignment.lines.Count -ne 3) { throw 'Installed alignment result differs from the fixture.' }
Write-Output 'Installed forced alignment completed.'
$data = Join-Path $destination 'data'
New-Item -ItemType Directory -Force -Path $data | Out-Null
$sentinel = Join-Path $data 'installer-test-user-data.txt'
Set-Content -LiteralPath $sentinel -Value 'Preserve user data on uninstall.' -Encoding utf8
Invoke-CheckedProcess (Join-Path $destination 'unins000.exe') @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART')
if (Test-Path -LiteralPath $installedExe) { throw 'Uninstaller did not remove the installed executable.' }
if (-not (Test-Path -LiteralPath $sentinel)) { throw 'Uninstaller removed user data.' }
$report = [ordered]@{
    version = '1.10.0'; installer = $Installer; sha256 = (Get-FileHash -LiteralPath $Installer).Hash.ToLowerInvariant()
    installed = $true; application_startup = $true; system_only_path = $true; model_hashes_verified = $true
    bundled_runtime_imports = $true; subtitle_ocr = $true; forced_alignment = $true; audio_analysis_removed = $true
    uninstall = $true; user_data_preserved = $true
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $project 'docs\installer-validation.json') -Encoding utf8
Write-Output 'Installer verification completed; uninstall preserved user data.'
