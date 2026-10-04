@echo off
rem Starts Portal VR. Needs Steam, Portal and SteamVR installed.
rem Extra args go to tools\launch.ps1, e.g.  launch.bat -Flat   or   launch.bat -Map testchmb_a_08
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\play.ps1" %*
if errorlevel 1 pause
