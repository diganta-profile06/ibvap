@echo off
reg delete "HKCU\Software\Classes\ibvap" /f
echo Removed the ibvap:// link.
pause
