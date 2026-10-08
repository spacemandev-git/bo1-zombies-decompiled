@echo off
rem Black Ops 1 Zombies co-op (not part of the original game): host a game. Asks for name, character, map and
rem player count; parameters go through to tools\coop.ps1 (e.g. coop-host.bat -Name Alice -Character 0 -Players 2).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0coop.ps1" -Host %*
if errorlevel 1 pause
