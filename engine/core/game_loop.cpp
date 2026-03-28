#include "game_loop.hpp"
#include <spdlog/spdlog.h>
#include <thread>

using clock_t  = std::chrono::steady_clock;
using us_t     = std::chrono::microseconds;
using ms_float = std::chrono::duration<float, std::milli>;

GameLoop::GameLoop(int tick_rate_hz)
    : m_tick_rate_hz(tick_rate_hz)
    , m_tick_interval(us_t(1'000'000 / tick_rate_hz))
{}

void GameLoop::add_system(System system) {
    m_systems.push_back(std::move(system));
}

void GameLoop::run() {
    m_running = true;
    m_tick_count = 0;

    spdlog::info("[GameLoop] Iniciando @ {}Hz ({:.1f}ms/tick)",
                 m_tick_rate_hz,
                 static_cast<float>(m_tick_interval.count()) / 1000.f);

    auto next_tick  = clock_t::now();
    auto last_tick  = next_tick;
    float avg_acc   = 0.f;             // acumulador para media movel
    const float EMA = 0.05f;           // fator da media movel exponencial

    while (m_running) {
        auto now      = clock_t::now();
        float real_ms = ms_float(now - last_tick).count();
        last_tick     = now;

        ++m_tick_count;

        // Monta contexto do tick
        TickContext ctx;
        ctx.tick     = m_tick_count;
        ctx.delta_ms = real_ms;
        ctx.alpha    = 0.f; // preenchido pelo render (nao usado no servidor)

        // Executa todos os sistemas registrados
        auto tick_start = clock_t::now();
        for (auto& sys : m_systems) {
            sys(ctx);
        }
        float tick_ms = ms_float(clock_t::now() - tick_start).count();

        // Media movel exponencial do tempo de tick
        m_avg_tick_ms = (1.f - EMA) * m_avg_tick_ms + EMA * tick_ms;

        // Aviso se o tick demorou mais que o intervalo
        float interval_ms = static_cast<float>(m_tick_interval.count()) / 1000.f;
        if (tick_ms > interval_ms) {
            spdlog::warn("[GameLoop] Tick {} demorou {:.2f}ms (limite {:.1f}ms)",
                         m_tick_count, tick_ms, interval_ms);
        }

        // Log de estatisticas a cada 200 ticks (~10s em 20Hz)
        if (m_tick_count % 200 == 0) {
            spdlog::debug("[GameLoop] tick={} avg={:.2f}ms",
                          m_tick_count, m_avg_tick_ms);
        }

        // Dorme ate o proximo tick (acumula atraso se o sistema estiver lento)
        next_tick += m_tick_interval;
        auto sleep_until = next_tick;
        if (clock_t::now() > sleep_until) {
            // Atraso acumulado: pula para o proximo tick futuro
            next_tick = clock_t::now() + m_tick_interval;
        } else {
            std::this_thread::sleep_until(sleep_until);
        }
    }

    spdlog::info("[GameLoop] Encerrado apos {} ticks. Avg tick: {:.2f}ms",
                 m_tick_count, m_avg_tick_ms);
}

void GameLoop::stop() {
    m_running = false;
}
