@echo off
rem Launches Portal VR (VR mode, main menu). Extra args are passed through to tools\launch.ps1,
rem e.g.  launch.bat -Flat   or   launch.bat -Map testchmb_a_08
set "PORTAL=C:\Program Files (x86)\Steam\steamapps\common\Portal"
if not exist "%PORTAL%\portalvr\gameinfo.txt" (
	echo Mod folder not linked into Portal, running deploy...
	powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\deploy.ps1"
)
if not exist "%~dp0sp\game\portalvr\bin\client.dll" (
	echo client.dll not built yet, building...
	powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build.ps1" || (pause & exit /b 1)
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\launch.ps1" %*
