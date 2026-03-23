"""
Quest de tutorial.
"""
QUEST_ID   = "tutorial_01"
QUEST_NAME = "Primeiros Passos"

def on_accept(player_id: str):
    print(f"[Quest] {player_id} aceitou '{QUEST_NAME}'")
    return {"objetivo": "Matar 3 goblins", "progresso": 0, "meta": 3}

def on_progress(player_id: str, current: int, target: int):
    print(f"[Quest] {player_id}: {current}/{target} goblins mortos")

def on_complete(player_id: str):
    print(f"[Quest] {player_id} completou '{QUEST_NAME}'!")
    return {"xp": 100, "gold": 50}
