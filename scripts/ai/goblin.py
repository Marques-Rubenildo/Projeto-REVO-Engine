"""
IA do Goblin.
Chamada pelo C++: python_bridge.call("ai.goblin", "on_player_nearby", player_id)
"""

def on_spawn(entity_id: str):
    print(f"[Goblin {entity_id}] Spawnou no mundo.")

def on_player_nearby(player_id: str):
    print(f"[Goblin] Jogador {player_id} detectado -- entrando em combate!")

def on_death(entity_id: str):
    print(f"[Goblin {entity_id}] Morreu. Gerando loot...")
    return {"gold": 5, "items": ["espada_enferrujada"]}
