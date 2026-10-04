@echo off
title IBVAP web page
cd /d "%~dp0frontend"
if not exist node_modules (
  echo Installing frontend packages (first time only)...
  call npm install
)
echo.
echo Opening http://localhost:5173 - click START IBVAP there to open the IBVAP app.
echo Leave this window open. Close it to stop the web page.
echo.
start "" "http://localhost:5173"
call npm run dev
pause
