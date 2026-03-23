add_executable(mmo_server
    game/main.cpp
    game/game_server.cpp
    zone/zone_manager.cpp
)
target_link_libraries(mmo_server PRIVATE mmo_engine)
