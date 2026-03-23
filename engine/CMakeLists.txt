add_library(mmo_engine STATIC
    core/engine.cpp
    ecs/world.cpp
    network/server_socket.cpp
    scripting/python_bridge.cpp
)

target_include_directories(mmo_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

target_compile_definitions(mmo_engine PUBLIC ENTT_ID_TYPE=uint32_t)

target_link_libraries(mmo_engine PUBLIC
    EnTT::EnTT
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    asio_interface          # rede assíncrona
)

target_link_libraries(mmo_engine PRIVATE
    pybind11::embed
    Python3::Python
)
