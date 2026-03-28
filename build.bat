@echo off
echo === MMO Engine - Build Debug ===

cd /d "%~dp0"

echo Pasta do projeto: %CD%

if not exist CMakeLists.txt (
    echo ERRO: CMakeLists.txt nao encontrado em %CD%
    echo Certifique-se de que o build.bat esta na raiz do projeto.
    pause
    exit /b 1
)

if not exist build mkdir build
if not exist logs mkdir logs

set TIMESTAMP=%DATE:~6,4%-%DATE:~3,2%-%DATE:~0,2%_%TIME:~0,2%-%TIME:~3,2%-%TIME:~6,2%
set TIMESTAMP=%TIMESTAMP: =0%
set CMAKE_LOG=%~dp0logs\cmake_error_%TIMESTAMP%.log
set BUILD_LOG=%~dp0logs\build_error_%TIMESTAMP%.log

:: Arquivo temporario para capturar tudo antes de filtrar
set TEMP_LOG=%TEMP%\mmo_build_temp.log

echo.
cd build

:: ── CMake Configure ──────────────────────────────────────────────────────────
cmake .. -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON > "%TEMP_LOG%" 2>&1

if %ERRORLEVEL% NEQ 0 (
    :: Salva apenas linhas com erro
    powershell -Command "Get-Content '%TEMP_LOG%' | Where-Object { $_ -match '(?i)error' } | Set-Content '%CMAKE_LOG%'"
    echo.
    echo ERRO: Falha na configuracao do CMake.
    echo Erros salvos em: %CMAKE_LOG%
    echo.
    powershell -Command "Get-Content '%CMAKE_LOG%'"
    del "%TEMP_LOG%"
    pause
    exit /b 1
)
del "%TEMP_LOG%"

:: ── CMake Build ───────────────────────────────────────────────────────────────
cmake --build . --config Debug --parallel > "%TEMP_LOG%" 2>&1

if %ERRORLEVEL% NEQ 0 (
    :: Salva apenas linhas com "error" (case-insensitive), exclui "warning" e linhas vazias
    powershell -Command "Get-Content '%TEMP_LOG%' | Where-Object { $_ -match '(?i)\berror\b' -and $_ -notmatch '(?i)warning' } | Where-Object { $_.Trim() -ne '' } | Set-Content '%BUILD_LOG%'"
    echo.
    echo ERRO: Falha na compilacao.
    echo Erros salvos em: %BUILD_LOG%
    echo.
    powershell -Command "Get-Content '%BUILD_LOG%'"
    del "%TEMP_LOG%"
    pause
    exit /b 1
)
del "%TEMP_LOG%"

echo.
echo === Build concluido com sucesso! ===
echo Executavel: build\server\Debug\mmo_server.exe
pause
