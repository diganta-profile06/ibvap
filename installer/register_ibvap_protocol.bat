@echo off
title Register IBVAP link
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0register_ibvap_protocol.ps1"
echo.
pause
