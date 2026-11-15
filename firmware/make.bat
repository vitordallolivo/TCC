@echo off
rem Atalho para o make.ps1 — uso: make build | make flash | make clean | ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make.ps1" %*
exit /b %ERRORLEVEL%
