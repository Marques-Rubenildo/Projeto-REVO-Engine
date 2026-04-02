#pragma once
// engine/core/engine.hpp

#include <string>
#include <cstdint>

struct EngineConfig {
    std::string server_host = "0.0.0.0";
    int         server_port = 7777;
    int         max_players = 1000;
    int         tick_rate   = 20;
    std::string scripts_dir = "scripts";
};

class Engine {
public:
    Engine();
    explicit Engine(const EngineConfig& config);
    ~Engine();

    void initialize();  // valida config e prepara subsistemas
    void run();         // inicia o loop — bloqueia ate encerramento

private:
    EngineConfig m_config;
};
