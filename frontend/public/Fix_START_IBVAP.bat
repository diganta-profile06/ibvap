@echo off
title Fix the START IBVAP button
echo.
echo  Fixing the START IBVAP button for this Windows user...
echo.
set "SELF=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$s=[IO.File]::ReadAllText($env:SELF); $i=$s.IndexOf('#PS#'+'START'); Invoke-Expression $s.Substring($i)"
echo.
pause
exit /b
#PS#START
$ErrorActionPreference = 'Stop'
$list = New-Object System.Collections.ArrayList

function Add-Cand($p) {
  if ($p -and (Test-Path -LiteralPath $p)) {
    $full = (Resolve-Path -LiteralPath $p).Path
    if (-not $list.Contains($full)) { [void]$list.Add($full) }
  }
}

try {
  # 0) What the website currently opens
  try {
    $old = (Get-ItemProperty -Path 'HKCU:\Software\Classes\ibvap\shell\open\command' -ErrorAction Stop).'(default)'
    if ($old -match '([A-Za-z]:\\[^"]*IBVAP\.exe)') { Add-Cand $Matches[1] }
  } catch { }

  # 1) Uninstall entries
  foreach ($r in @('HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall','HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall','HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall')) {
    if (-not (Test-Path $r)) { continue }
    foreach ($k in Get-ChildItem $r -ErrorAction SilentlyContinue) {
      $p = Get-ItemProperty -LiteralPath $k.PSPath -ErrorAction SilentlyContinue
      if ($p -and $p.DisplayName -and ($p.DisplayName -like '*IBVAP*')) {
        if ($p.InstallLocation) { Add-Cand (Join-Path $p.InstallLocation 'IBVAP.exe') }
        if ($p.DisplayIcon) { Add-Cand (($p.DisplayIcon -replace ',\d+$','').Trim('"')) }
      }
    }
  }

  # 2) Shortcuts
  try {
    $shell = New-Object -ComObject WScript.Shell
    foreach ($d in @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('CommonDesktopDirectory'), "$env:APPDATA\Microsoft\Windows\Start Menu\Programs", "$env:ProgramData\Microsoft\Windows\Start Menu\Programs")) {
      if (-not (Test-Path $d)) { continue }
      foreach ($l in Get-ChildItem $d -Recurse -Filter 'IBVAP*.lnk' -ErrorAction SilentlyContinue) {
        $t = $shell.CreateShortcut($l.FullName).TargetPath
        if ($t -and ((Split-Path $t -Leaf) -ieq 'IBVAP.exe')) { Add-Cand $t }
      }
    }
  } catch { }

  # 3) Usual folders
  Add-Cand "$env:LOCALAPPDATA\Programs\IBVAP\IBVAP.exe"
  Add-Cand "$env:ProgramFiles\IBVAP\IBVAP.exe"
  Add-Cand "${env:ProgramFiles(x86)}\IBVAP\IBVAP.exe"
  Add-Cand 'C:\IBVAP\IBVAP.exe'

  # 4) Copies in Downloads / Desktop / Documents (for example build\bin)
  $roots = @("$env:USERPROFILE\Downloads", "$env:USERPROFILE\Desktop", "$env:USERPROFILE\Documents") | Where-Object { Test-Path $_ }
  foreach ($f in Get-ChildItem -Path $roots -Recurse -Depth 6 -Filter 'IBVAP.exe' -File -ErrorAction SilentlyContinue) { Add-Cand $f.FullName }

  if ($list.Count -eq 0) {
    Write-Host 'Could not find IBVAP.exe automatically. Please pick it in the window that opens...'
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title = 'Select the IBVAP.exe that works on this PC'
    $dlg.Filter = 'IBVAP.exe|IBVAP.exe'
    if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { throw 'Cancelled. Install IBVAP first, then run this again.' }
    Add-Cand $dlg.FileName
  }

  Write-Host 'IBVAP copies found on this PC:'
  $withModels = @()
  foreach ($c in $list) {
    $has = Test-Path -LiteralPath (Join-Path (Split-Path -Parent $c) 'models\detection\yolov8s.ncnn.bin')
    if ($has) { $withModels += $c }
    $tag = if ($has) { '[has AI models]' } else { '[NO AI models]' }
    Write-Host "  $tag  $c"
  }

  # Prefer a copy that has the AI models, otherwise fall back to the first one
  if ($withModels.Count -gt 0) { $exe = $withModels[0] } else { $exe = $list[0] }
  $dir = Split-Path -Parent $exe
  Write-Host ''
  Write-Host "Using: $exe"
  if ($withModels.Count -eq 0) {
    Write-Host 'WARNING: this copy has no AI models. Run Fix_AI_Detection.bat afterwards.' -ForegroundColor Yellow
  }

  # Register ibvap:// so the app always starts INSIDE its own folder (same as double-clicking it)
  $cmdExe = Join-Path $env:SystemRoot 'System32\cmd.exe'
  $command = '"' + $cmdExe + '" /c start "" /D "' + $dir + '" "' + $exe + '"'

  $root = 'HKCU:\Software\Classes\ibvap'
  New-Item -Path $root -Force | Out-Null
  Set-ItemProperty -Path $root -Name '(default)' -Value 'URL:IBVAP'
  Set-ItemProperty -Path $root -Name 'URL Protocol' -Value ''
  New-Item -Path "$root\DefaultIcon" -Force | Out-Null
  Set-ItemProperty -Path "$root\DefaultIcon" -Name '(default)' -Value ('"' + $exe + '",0')
  New-Item -Path "$root\shell\open\command" -Force | Out-Null
  Set-ItemProperty -Path "$root\shell\open\command" -Name '(default)' -Value $command

  Write-Host ''
  Write-Host 'DONE. The website START IBVAP button now opens:' -ForegroundColor Green
  Write-Host "  $exe"
  Write-Host ''
  Write-Host 'Go back to the IBVAP website and click START IBVAP (click Open when the browser asks).'
  Write-Host 'Starting IBVAP now as a test...'
  Start-Process 'ibvap://launch'
}
catch {
  Write-Host ''
  Write-Host ('FAILED: ' + $_.Exception.Message) -ForegroundColor Red
}
