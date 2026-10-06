@echo off
title Fix IBVAP "AI Detection: Unavailable"
echo.
echo  Checking the AI model files of your installed IBVAP...
echo.
set "SELF=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$s=[IO.File]::ReadAllText($env:SELF); $i=$s.IndexOf('#PS#'+'START'); Invoke-Expression $s.Substring($i)"
echo.
pause
exit /b
#PS#START
$ErrorActionPreference = 'Stop'
try {
  # Where is the installed IBVAP.exe? (registered by Fix_START_IBVAP.bat / the installer)
  $cmd = (Get-ItemProperty -Path 'HKCU:\Software\Classes\ibvap\shell\open\command').'(default)'
  $exe = $cmd.Trim().Trim('"')
  if (-not (Test-Path -LiteralPath $exe)) { throw "IBVAP.exe not found at: $exe" }
  $appDir = Split-Path -Parent $exe
  $target = Join-Path $appDir 'models\detection'
  Write-Host "IBVAP folder : $appDir"

  $needParam = Join-Path $target 'yolov8s.ncnn.param'
  $needBin   = Join-Path $target 'yolov8s.ncnn.bin'
  if ((Test-Path -LiteralPath $needParam) -and (Test-Path -LiteralPath $needBin)) {
    $size = [math]::Round((Get-Item -LiteralPath $needBin).Length / 1MB, 1)
    Write-Host ''
    Write-Host "The YOLOv8s model is already installed ($size MB). Nothing to copy." -ForegroundColor Yellow
    Write-Host 'So the cause is something else. Please run IBVAP.exe from a terminal and send me the lines'
    Write-Host 'that start with "IBVAP AI:".'
    return
  }

  Write-Host 'The YOLOv8s model files are MISSING from the installed IBVAP. Looking for a copy on this PC...'
  $roots = @("$env:USERPROFILE\Downloads", "$env:USERPROFILE\Desktop", "$env:USERPROFILE\Documents") | Where-Object { Test-Path $_ }
  $hit = Get-ChildItem -Path $roots -Recurse -Filter 'yolov8s.ncnn.param' -ErrorAction SilentlyContinue |
         Where-Object { Test-Path -LiteralPath (Join-Path $_.DirectoryName 'yolov8s.ncnn.bin') } |
         Select-Object -First 1

  if ($hit) {
    $src = Split-Path -Parent $hit.DirectoryName      # the "models" folder
  } else {
    Write-Host 'Not found automatically. Please select the "models" folder of your IBVAP project...'
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
    $dlg.Description = 'Select the IBVAP project "models" folder (it contains detection, face, anpr)'
    if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { throw 'Cancelled.' }
    $src = $dlg.SelectedPath
    if (-not (Test-Path -LiteralPath (Join-Path $src 'detection\yolov8s.ncnn.param'))) {
      throw "That folder does not contain detection\yolov8s.ncnn.param"
    }
  }

  Write-Host "Copying models from: $src"
  $dest = Join-Path $appDir 'models'
  New-Item -ItemType Directory -Force -Path $dest | Out-Null
  Copy-Item -Path (Join-Path $src '*') -Destination $dest -Recurse -Force

  if ((Test-Path -LiteralPath $needParam) -and (Test-Path -LiteralPath $needBin)) {
    Write-Host ''
    Write-Host 'DONE. The AI models are now in the IBVAP folder.' -ForegroundColor Green
    Write-Host 'Close IBVAP completely, start it again, and check "AI Detection" on the camera.'
  } else {
    throw 'Copy finished but the model files are still not in place.'
  }
}
catch {
  Write-Host ''
  Write-Host ('FAILED: ' + $_.Exception.Message) -ForegroundColor Red
}
