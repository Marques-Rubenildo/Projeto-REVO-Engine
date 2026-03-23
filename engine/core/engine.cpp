#include "engine.hpp"
#include "engine/ecs/world.hpp"
#include "engine/scripting/python_bridge.hpp"
#include "engine/network/server_socket.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <thread>

struct Engine::Subsystems {
    World        world;
    PythonBridge python;
    ServerSocket network;
};

Engine::Engine(EngineConfig config)
    : m_config(std::move(config))
    , m_subsystems(std::make_unique<Subsystems>())
{}

Engine::~Engine() {
    if (m_running) shutdown();
}

void Engine::initialize() {
    spdlog::info("=== MMO Engine v0.1 inicializando ===");
    m_subsystems->python.initialize(m_config.scripts_dir);
    m_subsystems->network.listen(m_config.server_host, m_config.server_port);
    m_running = true;
    spdlog::info("Engine pronta. Aguardando conexoes...");
}

void Engine::run() {
    using clock = std::chrono::steady_clock;
    using ms    = std::chrono::milliseconds;
    const auto tick_duration = ms(1000 / m_config.tick_rate);

    while (m_running) {
        auto tick_start = clock::now();
        m_subsystems->network.poll();
        // Fase 2: fisica, IA, sistemas ECS...
        auto elapsed = clock::now() - tick_start;
        auto sleep   = tick_duration - elapsed;
        if (sleep > ms(0))
            std::this_thread::sleep_for(sleep);
    }
}

void Engine::shutdown() {
    spdlog::info("Desligando engine...");
    m_subsystems->network.stop();
    m_running = false;
    spdlog::info("Engine encerrada.");
}
