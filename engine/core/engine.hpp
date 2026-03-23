#pragma once
#include <string>
#include <memory>

struct EngineConfig {
    std::string server_host = "0.0.0.0";
    int         server_port = 7777;
    int         max_players = 1000;
    int         tick_rate   = 20;
    std::string scripts_dir = "scripts";
};

class Engine {
public:
    explicit Engine(EngineConfig config);
    ~Engine();

    void initialize();
    void run();
    void shutdown();

    bool is_running() const { return m_running; }

private:
    EngineConfig m_config;
    bool         m_running = false;

    struct Subsystems;
    std::unique_ptr<Subsystems> m_subsystems;
};
