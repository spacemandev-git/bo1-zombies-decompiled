@echo off
rem Black Ops 1 Zombies co-op (not part of the original game): join a game. Asks for the host's address, name and
rem character; parameters go through to tools\coop.ps1 (e.g. coop-join.bat -Join 192.168.1.20 -Name Bob -Character 1).
if "%~1"=="" (powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0coop.ps1" -Join ?) else (powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0coop.ps1" %*)
if errorlevel 1 pause
