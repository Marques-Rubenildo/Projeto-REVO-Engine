#include "game_loop.hpp"
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

// Renomeado para evitar conflito com clock_t do C runtime no MSVC
using ServerClock = std::chrono::steady_clock;
using us_t        = std::chrono::microseconds;
using ms_float    = std::chrono::duration<float, std::milli>;

GameLoop::GameLoop(int tick_rate_hz)
    : m_tick_rate_hz(tick_rate_hz)
    , m_tick_interval(us_t(1'000'000 / tick_rate_hz))
{}

void GameLoop::add_system(System system) {
    m_systems.push_back(std::move(system));
}

void GameLoop::run() {
    m_running    = true;
    m_tick_count = 0;

    spdlog::info("[GameLoop] Iniciando @ {}Hz ({:.1f}ms/tick)",
                 m_tick_rate_hz,
                 static_cast<float>(m_tick_interval.count()) / 1000.f);

    auto next_tick = ServerClock::now();
    auto last_tick = next_tick;

    while (m_running) {
        auto  now     = ServerClock::now();
        float real_ms = ms_float(now - last_tick).count();
        last_tick     = now;

        ++m_tick_count;

        TickContext ctx;
        ctx.tick     = m_tick_count;
        ctx.delta_ms = real_ms;
        ctx.alpha    = 0.f;

        auto tick_start = ServerClock::now();
        for (auto& sys : m_systems) {
            try {
                sys(ctx);
            } catch (const std::exception& e) {
                spdlog::error("[GameLoop] Excecao em sistema: {}", e.what());
            } catch (...) {
                spdlog::error("[GameLoop] Excecao desconhecida em sistema.");
            }
        }

        float tick_ms    = ms_float(ServerClock::now() - tick_start).count();
        float alpha      = 0.05f;
        m_avg_tick_ms    = (1.f - alpha) * m_avg_tick_ms + alpha * tick_ms;

        float interval_ms = static_cast<float>(m_tick_interval.count()) / 1000.f;
        if (tick_ms > interval_ms) {
            spdlog::warn("[GameLoop] Tick {} demorou {:.2f}ms (limite {:.1f}ms)",
                         m_tick_count, tick_ms, interval_ms);
        }

        if (m_tick_count % 200 == 0) {
            spdlog::debug("[GameLoop] tick={} avg={:.2f}ms",
                          m_tick_count, m_avg_tick_ms);
        }

        next_tick += m_tick_interval;
        if (ServerClock::now() > next_tick)
            next_tick = ServerClock::now() + m_tick_interval;
        else
            std::this_thread::sleep_until(next_tick);
    }

    spdlog::info("[GameLoop] Encerrado apos {} ticks. Avg: {:.2f}ms",
                 m_tick_count, m_avg_tick_ms);
}

void GameLoop::stop() {
    m_running = false;
}
