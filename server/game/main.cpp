#include "engine/core/engine.hpp"
#include <spdlog/spdlog.h>

int main() {
    spdlog::set_level(spdlog::level::debug);

    EngineConfig config;
    config.server_host = "0.0.0.0";
    config.server_port = 7777;
    config.max_players = 1000;
    config.tick_rate   = 20;
    config.scripts_dir = "scripts";

    Engine engine(config);
    try {
        engine.initialize();
        engine.run();
    } catch (const std::exception& e) {
        spdlog::critical("Erro fatal: {}", e.what());
        return 1;
    }
    return 0;
}
