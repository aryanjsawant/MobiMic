@echo off
cd /d "%~dp0"
rem Bigger buffer: ~0.3 s delay, but rides out Wi-Fi hiccups without dropouts. Best for recording.
python server.py --buffer-ms 300 %*
pause
