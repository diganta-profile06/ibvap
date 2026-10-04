# Makes the START IBVAP button on the web page open the installed IBVAP app.
# It registers the ibvap:// link for the current Windows user (no admin needed).
# After this, Chrome/Edge show "Open IBVAP?" -> click Open -> IBVAP.exe starts.
param([string]$ExePath, [switch]$FindOnly)

$ErrorActionPreference = "Stop"

function Find-IbvapExe([string]$Given) {
  if ($Given -and (Test-Path -LiteralPath $Given)) { return (Resolve-Path -LiteralPath $Given).Path }

  # 1) The Inno Setup uninstall entry created by IBVAP_Setup.exe
  $roots = @(
    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall",
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall",
    "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall"
  )
  foreach ($r in $roots) {
    if (-not (Test-Path $r)) { continue }
    foreach ($k in Get-ChildItem $r -ErrorAction SilentlyContinue) {
      $p = Get-ItemProperty -LiteralPath $k.PSPath -ErrorAction SilentlyContinue
      if ($p -and $p.DisplayName -and ($p.DisplayName -like "*IBVAP*")) {
        foreach ($dir in @($p.InstallLocation, $p."Inno Setup: App Path")) {
          if ($dir) {
            $c = Join-Path $dir "IBVAP.exe"
            if (Test-Path -LiteralPath $c) { return $c }
          }
        }
        if ($p.DisplayIcon) {
          $c = ($p.DisplayIcon -replace ',\d+$', '').Trim('"')
          if ((Split-Path $c -Leaf) -ieq "IBVAP.exe" -and (Test-Path -LiteralPath $c)) { return $c }
        }
      }
    }
  }

  # 2) Desktop / Start menu shortcuts
  try {
    $shell = New-Object -ComObject WScript.Shell
    $dirs = @(
      [Environment]::GetFolderPath("Desktop"),
      [Environment]::GetFolderPath("CommonDesktopDirectory"),
      "$env:APPDATA\Microsoft\Windows\Start Menu\Programs",
      "$env:ProgramData\Microsoft\Windows\Start Menu\Programs"
    )
    foreach ($d in $dirs) {
      if (-not (Test-Path $d)) { continue }
      foreach ($l in Get-ChildItem $d -Recurse -Filter "IBVAP*.lnk" -ErrorAction SilentlyContinue) {
        $t = $shell.CreateShortcut($l.FullName).TargetPath
        if ($t -and ((Split-Path $t -Leaf) -ieq "IBVAP.exe") -and (Test-Path -LiteralPath $t)) { return $t }
      }
    }
  } catch { }

  # 3) Usual folders, and a build next to this project
  $guesses = @(
    "$env:LOCALAPPDATA\Programs\IBVAP\IBVAP.exe",
    "$env:ProgramFiles\IBVAP\IBVAP.exe",
    "${env:ProgramFiles(x86)}\IBVAP\IBVAP.exe",
    "C:\IBVAP\IBVAP.exe",
    (Join-Path $PSScriptRoot "..\build\bin\IBVAP.exe")
  )
  foreach ($g in $guesses) { if ($g -and (Test-Path -LiteralPath $g)) { return (Resolve-Path -LiteralPath $g).Path } }

  return $null
}

try {
  $exe = Find-IbvapExe $ExePath

  if ($FindOnly) {
    if ($exe) { Write-Output $exe; exit 0 } else { exit 1 }
  }

  if (-not $exe) {
    Write-Host "Could not find IBVAP.exe automatically. Pick it in the window that opens..."
    Add-Type -AssemblyName System.Windows.Forms
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title = "Select the installed IBVAP.exe"
    $dlg.Filter = "IBVAP.exe|IBVAP.exe"
    if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { Write-Host "Cancelled."; exit 1 }
    $exe = $dlg.FileName
  }

  Write-Host "Using: $exe"

  $root = "HKCU:\Software\Classes\ibvap"
  New-Item -Path $root -Force | Out-Null
  Set-ItemProperty -Path $root -Name "(default)" -Value "URL:IBVAP"
  Set-ItemProperty -Path $root -Name "URL Protocol" -Value ""
  New-Item -Path "$root\DefaultIcon" -Force | Out-Null
  Set-ItemProperty -Path "$root\DefaultIcon" -Name "(default)" -Value "`"$exe`",0"
  New-Item -Path "$root\shell\open\command" -Force | Out-Null
  Set-ItemProperty -Path "$root\shell\open\command" -Name "(default)" -Value "`"$exe`""

  # read it back to be sure it was written
  $check = (Get-ItemProperty -Path "$root\shell\open\command")."(default)"
  Write-Host ""
  Write-Host "Registered ibvap:// ->  $check"
  Write-Host ""
  Write-Host "Now open the IBVAP web page, click START IBVAP, and click Open in the browser prompt."
  Write-Host "(Tick 'Always allow' in that prompt to skip it next time.)"
  Write-Host ""

  $ans = Read-Host "Test it now? Windows should start IBVAP (y/n)"
  if ($ans -match '^[yY]') { Start-Process "ibvap://launch" }
}
catch {
  Write-Host ""
  Write-Host "FAILED: $($_.Exception.Message)" -ForegroundColor Red
  exit 1
}
