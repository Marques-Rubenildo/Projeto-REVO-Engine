#pragma once
// engine/core/game_loop.hpp

#include <functional>
#include <chrono>
#include <atomic>
#include <vector>
#include <cstdint>

struct TickContext {
    uint64_t tick;
    float    delta_ms;
    float    alpha;
};

using System = std::function<void(const TickContext&)>;

class GameLoop {
public:
    explicit GameLoop(int tick_rate_hz = 20);
    void add_system(System system);
    void run();
    void stop();

    uint64_t tick_count()  const { return m_tick_count; }
    float    avg_tick_ms() const { return m_avg_tick_ms; }
    bool     is_running()  const { return m_running.load(); }

private:
    int                       m_tick_rate_hz;
    std::chrono::microseconds m_tick_interval;
    std::vector<System>  m_systems;
    std::atomic<bool>         m_running{false};
    uint64_t                  m_tick_count{0};
    float                     m_avg_tick_ms{0.f};
};
