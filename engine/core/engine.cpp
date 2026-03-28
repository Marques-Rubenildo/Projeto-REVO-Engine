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

// Sinal de encerramento limpo (Ctrl+C)
static GameLoop* g_loop_ptr = nullptr;
static void signal_handler(int) {
    spdlog::info("[Engine] Sinal recebido. Encerrando...");
    if (g_loop_ptr) g_loop_ptr->stop();
}

Engine::Engine()  = default;
Engine::~Engine() = default;

void Engine::run() {
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("=== REVO-ENGINE iniciando ===");

    // ── Subsistemas ──────────────────────────────────────────────────────────
    World             world;
    ServerSocket      server;
    MessageDispatcher dispatcher;
    PythonBridge      python;
    GameLoop          loop(20); // 20Hz = 50ms por tick

    g_loop_ptr = &loop;
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ── Python ───────────────────────────────────────────────────────────────
    python.initialize(std::filesystem::current_path() / "scripts");

    // ── Handlers de protocolo ─────────────────────────────────────────────────
    dispatcher.on(MsgType::C_Login, [&](uint32_t client_id, const json& p) {
        auto pkt    = PktCLogin::from_json(p);
        auto entity = world.create_entity();
        world.add<Identity>(entity, pkt.username, static_cast<uint64_t>(client_id));
        world.add<Position>(entity, 0.f, 0.f, 0.f);
        world.add<Health>(entity);

        PktSLoginOk resp;
        resp.entity_id = static_cast<uint32_t>(entity);
        resp.character  = pkt.username;
        resp.x = resp.y = resp.z = 0.f;
        server.send_to(client_id, resp.envelope().serialize());
        spdlog::info("[Login] {} → entidade {}", pkt.username, resp.entity_id);
    });

    dispatcher.on(MsgType::C_Move, [&](uint32_t, const json& p) {
        auto pkt = PktCMove::from_json(p);
        // Atualiza posição no ECS quando tivermos mapa entity→client
        spdlog::debug("[Move] eid={} ({:.1f},{:.1f})", pkt.entity_id, pkt.x, pkt.z);
    });

    dispatcher.on(MsgType::C_Chat, [&](uint32_t client_id, const json& p) {
        auto pkt = PktCChat::from_json(p);
        if (pkt.text.size() > 256) pkt.text.resize(256);
        PktSChatBroadcast bcast{ client_id,
                                  "Player" + std::to_string(client_id),
                                  pkt.text };
        server.broadcast(bcast.envelope().serialize());
        spdlog::info("[Chat] P{}: {}", client_id, pkt.text);
    });

    dispatcher.on(MsgType::Ping, [&](uint32_t client_id, const json& p) {
        server.send_to(client_id,
            PktPong{ p.value("ts", uint64_t{0}) }.envelope().serialize());
    });

    dispatcher.on(MsgType::C_Logout, [&](uint32_t client_id, const json&) {
        server.send_to(client_id,
            PktSDisconnect{"logout"}.envelope().serialize());
    });

    server.on_connect([](uint32_t id) {
        spdlog::info("[Net] Cliente {} conectou.", id);
    });
    server.on_disconnect([](uint32_t id) {
        spdlog::info("[Net] Cliente {} desconectou.", id);
    });

    dispatcher.attach(server);

    // ── Inicia servidor ───────────────────────────────────────────────────────
    server.listen("0.0.0.0", 7777);
    spdlog::info("TCP:7777  UDP:7778  |  {} entidades", world.entity_count());

    // ─────────────────────────────────────────────────────────────────────────
    // Sistemas do game loop (executados em ordem a cada tick de 50ms)
    // ─────────────────────────────────────────────────────────────────────────

    // Sistema 1 — processa mensagens de rede pendentes
    loop.add_system([&](const TickContext&) {
        server.poll();
    });

    // Sistema 2 — atualiza posicoes/fisica (placeholder para Fase 2)
    loop.add_system([&](const TickContext& ctx) {
        (void)ctx;
        // TODO: movement_system(world, ctx.delta_ms);
    });

    // Sistema 3 — executa eventos Python acumulados no tick
    loop.add_system([&](const TickContext& ctx) {
        if (!python.is_initialized()) return;
        // TODO: python.call("events", "flush_tick", ctx.tick);
        (void)ctx;
    });

    // Sistema 4 — envia snapshot UDP com estado das entidades
    loop.add_system([&](const TickContext& ctx) {
        if (server.client_count() == 0) return;

        PktSSnapshot snap;
        snap.tick = static_cast<uint32_t>(ctx.tick);

        world.each<Position, Health>([&](
            entt::entity e, const Position& pos, const Health& hp)
        {
            EntityState es;
            es.entity_id  = static_cast<uint32_t>(e);
            es.x          = pos.x;
            es.y          = pos.y;
            es.z          = pos.z;
            es.dir        = 0.f;
            es.anim_state = 0;
            es.hp         = static_cast<uint16_t>(hp.current);
            snap.entities.push_back(es);
        });

        if (!snap.entities.empty())
            server.broadcast(snap.envelope().serialize(), Channel::Unreliable);
    });

    // ── Inicia o loop — bloqueia ate Ctrl+C ───────────────────────────────────
    loop.run();

    // ── Encerramento limpo ────────────────────────────────────────────────────
    server.stop();
    spdlog::info("=== REVO-ENGINE encerrado. Ticks: {} ===", loop.tick_count());
}
