@echo off
cd /d "%~dp0"
if not exist logs mkdir logs

echo Iniciando REVO-ENGINE servidor...
echo.

build\server\RelWithDebInfo\mmo_server.exe

echo.
echo ==============================
echo Servidor encerrado (codigo: %ERRORLEVEL%)
echo ==============================
pause
