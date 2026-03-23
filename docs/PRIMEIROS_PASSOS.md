# Primeiros Passos

## 1. Instalar pre-requisitos
- Visual Studio 2022: https://visualstudio.microsoft.com/
  - Selecione "Desenvolvimento para desktop com C++"
- CMake 3.20+: https://cmake.org/download/
- Python 3.11+: https://python.org/downloads/
  - Marque "Add Python to PATH" durante a instalacao

## 2. Compilar
```bat
build.bat
```
Na primeira execucao o CMake vai baixar ~50MB de dependencias.

## 3. Adicionar um NPC em Python
Crie scripts/ai/meu_npc.py:
```python
def on_spawn(entity_id: str):
    print(f"NPC {entity_id} criado!")

def on_player_nearby(player_id: str):
    print(f"Jogador {player_id} proximo!")
```

Chame do C++:
```cpp
m_subsystems->python.call("ai.meu_npc", "on_player_nearby", player_id);
```
