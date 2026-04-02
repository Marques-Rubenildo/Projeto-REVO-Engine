#include "engine.hpp"
#include "game_loop.hpp"
#include "network/server_socket.hpp"
#include "network/protocol.hpp"
#include "network/message_dispatcher.hpp"
#include "ecs/world.hpp"
#include "scripting/python_bridge.hpp"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <csignal>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <mutex>

static GameLoop* g_loop_ptr = nullptr;
static void signal_handler(int sig) {
    spdlog::info("[Engine] Sinal {} recebido. Encerrando...", sig);
    if (g_loop_ptr) g_loop_ptr->stop();
}

Engine::Engine() = default;
Engine::Engine(const EngineConfig& config) : m_config(config) {}
Engine::~Engine() = default;

void Engine::initialize() {
    if (m_config.server_port <= 0 || m_config.server_port > 65534)
        throw std::runtime_error("Porta invalida: " +
                                 std::to_string(m_config.server_port));
    if (m_config.tick_rate <= 0 || m_config.tick_rate > 128)
        throw std::runtime_error("tick_rate invalido: " +
                                 std::to_string(m_config.tick_rate));
    spdlog::info("[Engine] Config: {}:{} | {} jogadores | {}Hz",
                 m_config.server_host, m_config.server_port,
                 m_config.max_players, m_config.tick_rate);
}

void Engine::run() {
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("=== REVO-ENGINE iniciando ===");

    World             world;
    std::mutex        world_mtx;   // protege acesso ao world entre threads
    ServerSocket      server;
    MessageDispatcher dispatcher;
    PythonBridge      python;
    GameLoop          loop(m_config.tick_rate);

    g_loop_ptr = &loop;
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    python.initialize(std::filesystem::path(m_config.scripts_dir));

    // ── Handlers de protocolo ─────────────────────────────────────────────
    // ATENÇÃO: handlers rodam nas threads do Asio (io_context)
    // O world_mtx garante que não há acesso concorrente ao ECS

    dispatcher.on(MsgType::C_Login, [&](uint32_t client_id, const json& p) {
        try {
            auto pkt = PktCLogin::from_json(p);

            entt::entity entity;
            {
                std::lock_guard<std::mutex> lk(world_mtx);
                entity = world.create_entity();
                world.add<Identity>(entity, pkt.username,
                                    static_cast<uint64_t>(client_id));
                world.add<Position>(entity, 0.f, 0.f, 0.f);
                world.add<Health>(entity);
            }

            PktSLoginOk resp;
            resp.entity_id = static_cast<uint32_t>(entity);
            resp.client_id = client_id;
            resp.character  = pkt.username;
            resp.x = resp.y = resp.z = 0.f;
            server.send_to(client_id, resp.envelope().serialize());
            spdlog::info("[Login] {} -> entidade {}", pkt.username, resp.entity_id);
        } catch (const std::exception& e) {
            spdlog::error("[Login] Excecao: {}", e.what());
        }
    });

    dispatcher.on(MsgType::C_Move, [&](uint32_t, const json& p) {
        try {
            auto pkt = PktCMove::from_json(p);
            spdlog::debug("[Move] eid={} ({:.1f},{:.1f})",
                          pkt.entity_id, pkt.x, pkt.z);
        } catch (...) {}
    });

    dispatcher.on(MsgType::C_Chat, [&](uint32_t client_id, const json& p) {
        try {
            auto pkt = PktCChat::from_json(p);
            if (pkt.text.size() > 256) pkt.text.resize(256);
            PktSChatBroadcast bcast{ client_id,
                                      "Player" + std::to_string(client_id),
                                      pkt.text };
            server.broadcast(bcast.envelope().serialize());
            spdlog::info("[Chat] P{}: {}", client_id, pkt.text);
        } catch (const std::exception& e) {
            spdlog::error("[Chat] Excecao: {}", e.what());
        }
    });

    dispatcher.on(MsgType::Ping, [&](uint32_t client_id, const json& p) {
        try {
            server.send_to(client_id,
                PktPong{ p.value("ts", uint64_t{0}) }.envelope().serialize());
        } catch (...) {}
    });

    dispatcher.on(MsgType::C_Logout, [&](uint32_t client_id, const json&) {
        try {
            server.send_to(client_id,
                PktSDisconnect{"logout"}.envelope().serialize());
        } catch (...) {}
    });

    server.on_connect([](uint32_t id) {
        spdlog::info("[Net] Cliente {} conectou.", id);
    });

    server.on_disconnect([](uint32_t id) {
        spdlog::info("[Net] Cliente {} desconectou.", id);
    });

    dispatcher.attach(server);

    try {
        server.listen(m_config.server_host, m_config.server_port);
    } catch (const std::exception& e) {
        spdlog::critical("[Engine] Falha ao iniciar servidor: {}", e.what());
        return;
    }

    spdlog::info("TCP:{}  UDP:{}  | max:{} jogadores",
                 m_config.server_port,
                 m_config.server_port + 1,
                 m_config.max_players);

    // ── Sistemas do game loop (rodam no thread principal, sequencialmente) ─
    // poll() — processa callbacks de rede no thread principal
    loop.add_system([&](const TickContext&) {
        try { server.poll(); }
        catch (const std::exception& e) {
            spdlog::error("[poll] Excecao: {}", e.what());
        }
    });

    // Snapshot UDP — lê o world com mutex
    loop.add_system([&](const TickContext& ctx) {
        try {
            if (server.client_count() == 0) return;

            PktSSnapshot snap;
            snap.tick = static_cast<uint32_t>(ctx.tick);

            {
                std::lock_guard<std::mutex> lk(world_mtx);
                world.each<Position, Health>([&](
                    entt::entity e, const Position& pos, const Health& hp)
                {
                    EntityState es;
                    es.entity_id  = static_cast<uint32_t>(e);
                    es.x = pos.x; es.y = pos.y; es.z = pos.z;
                    es.dir = 0.f; es.anim_state = 0;
                    es.hp  = static_cast<uint16_t>(hp.current);
                    snap.entities.push_back(es);
                });
            }

            if (!snap.entities.empty())
                server.broadcast(snap.envelope().serialize(),
                                 Channel::Unreliable);
        } catch (const std::exception& e) {
            spdlog::error("[snapshot] Excecao: {}", e.what());
        }
    });

    spdlog::info("[Engine] Entrando no game loop...");
    try {
        loop.run();
    } catch (const std::exception& e) {
        spdlog::critical("[Engine] Excecao no game loop: {}", e.what());
    } catch (...) {
        spdlog::critical("[Engine] Excecao desconhecida no game loop.");
    }

    spdlog::info("[Engine] Saindo do game loop.");
    try { server.stop(); }
    catch (...) { spdlog::error("[Engine] Excecao ao parar servidor."); }

    spdlog::info("=== REVO-ENGINE encerrado. Ticks: {} ===", loop.tick_count());
}
