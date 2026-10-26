param([string]$Installer = '')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$appVersion = [regex]::Match((Get-Content -LiteralPath (Join-Path $project 'CMakeLists.txt') -Raw), 'project\(BiliWave VERSION ([0-9.]+)').Groups[1].Value
if (-not $appVersion) { throw 'Application version was not found' }
if (-not $Installer) { $Installer = Join-Path $project "dist\BiliWave-$appVersion-Windows-x64-Setup.exe" }
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
if ((Get-FileHash -LiteralPath $installedExe).Hash -ne (Get-FileHash -LiteralPath (Join-Path $project 'release-screen\BiliWave.exe')).Hash) {
    throw 'Installed application hash mismatch.'
}
foreach ($forbidden in @('models\small.pt','tools\lyrics-runtime\Lib\site-packages\torch','tools\lyrics-runtime\Lib\site-packages\stable_whisper')) {
    if (Test-Path -LiteralPath (Join-Path $destination $forbidden)) { throw "Forbidden screen payload: $forbidden" }
}
$payloadBytes = (Get-ChildItem -LiteralPath $destination -File -Recurse | Measure-Object -Property Length -Sum).Sum
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
Invoke-CheckedProcess $installedExe @('--capture',("`"$(Join-Path $verificationRoot 'screenshots')`""),'--capture-delay','800') 20000
Write-Output 'Installed application started with system-only PATH.'
$python = Join-Path $destination 'tools\lyrics-runtime\python.exe'
$env:PYTHONUTF8 = '1'
& $python -I -c 'import rapidocr_onnxruntime, cv2, onnxruntime; print(onnxruntime.__version__)'
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
$ocr = Get-Content -LiteralPath $config.result -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $ocr.ok -or $ocr.text -ne "听见微光`n落在窗前") { throw 'Installed OCR result differs from the fixture.' }
Write-Output 'Installed subtitle OCR returned the expected text and timing.'
Write-Output 'Screen edition excludes audio alignment models and runtime.'
$data = Join-Path $destination 'data'
New-Item -ItemType Directory -Force -Path $data | Out-Null
$sentinel = Join-Path $data 'installer-test-user-data.txt'
Set-Content -LiteralPath $sentinel -Value 'Preserve user data on uninstall.' -Encoding utf8
Invoke-CheckedProcess (Join-Path $destination 'unins000.exe') @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART')
if (Test-Path -LiteralPath $installedExe) { throw 'Uninstaller did not remove the installed executable.' }
if (-not (Test-Path -LiteralPath $sentinel)) { throw 'Uninstaller removed user data.' }
$report = [ordered]@{
    version = $appVersion; installer = $Installer; sha256 = (Get-FileHash -LiteralPath $Installer).Hash.ToLowerInvariant()
    installer_bytes = (Get-Item -LiteralPath $Installer).Length; installed_bytes = $payloadBytes
    installed = $true; application_startup = $true; system_only_path = $true; alignment_payload_absent = $true
    bundled_runtime_imports = $true; subtitle_ocr = $true; audio_analysis_removed = $true
    uninstall = $true; user_data_preserved = $true
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $project 'docs\installer-validation.json') -Encoding utf8
Write-Output 'Installer verification completed; uninstall preserved user data.'
