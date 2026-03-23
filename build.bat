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
cd build

cmake .. -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERRO: Falha na configuracao do CMake.
    pause
    exit /b 1
)

cmake --build . --config Debug --parallel

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERRO: Falha na compilacao.
    pause
    exit /b 1
)

echo.
echo === Build concluido com sucesso! ===
echo Executavel: build\server\Debug\mmo_server.exe
pause
