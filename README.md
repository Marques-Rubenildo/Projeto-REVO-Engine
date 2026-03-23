# MMO Engine

Engine customizada para MMO RPG — nucleo C++20 com logica de jogo em Python.

## Pre-requisitos (Windows)
- Visual Studio 2022 (com suporte C++)
- CMake 3.20+  https://cmake.org/download/
- Python 3.11+ https://python.org/downloads/
- Git           https://git-scm.com/

## Como compilar
```bat
build.bat
```
O CMake baixa automaticamente: EnTT, spdlog, nlohmann/json, pybind11, GoogleTest.

## Rodar o servidor
```bat
cd build\server\Debug
mmo_server.exe
```

## Estrutura
```
engine/       <- Nucleo C++ (ECS, rede, scripting)
server/       <- Executavel do servidor
scripts/      <- Logica de jogo em Python (edite aqui!)
  ai/         <- IA de NPCs
  quests/     <- Sistema de quests
  skills/     <- Habilidades
tests/        <- Testes automatizados
```
