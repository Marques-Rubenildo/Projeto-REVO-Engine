#pragma once
// engine/network/protocol.hpp
// Protocolo de mensagens do servidor MMO
// Canal TCP  (Channel::Reliable)   → login, chat, inventário, combate
// Canal UDP  (Channel::Unreliable) → posição, animação, inputs de movimento

#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>
#include <optional>
#include <stdexcept>

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Tipos de mensagem
// ─────────────────────────────────────────────────────────────────────────────
enum class MsgType : uint16_t {
    // ── Cliente → Servidor ──────────────────────────────────────────────────
    C_Login         = 1,   // TCP  autenticação
    C_Move          = 2,   // UDP  input de movimento
    C_Chat          = 3,   // TCP  mensagem de chat
    C_Attack        = 4,   // TCP  inicia ataque
    C_UseItem       = 5,   // TCP  usa item do inventário
    C_Logout        = 6,   // TCP  saída limpa

    // ── Servidor → Cliente ──────────────────────────────────────────────────
    S_LoginOk       = 100, // TCP  login aceito, envia estado inicial
    S_LoginFail     = 101, // TCP  login recusado (motivo)
    S_Snapshot      = 102, // UDP  estado de entidades próximas (posição+anim)
    S_ChatBroadcast = 103, // TCP  mensagem de chat para todos na zona
    S_CombatEvent   = 104, // TCP  resultado de combate
    S_Disconnect    = 105, // TCP  servidor encerrando conexão

    // ── Sistema ─────────────────────────────────────────────────────────────
    Ping            = 200, // TCP/UDP keepalive
    Pong            = 201,
    Error           = 255, // resposta genérica de erro
};

// ─────────────────────────────────────────────────────────────────────────────
// Envelope — todo pacote tem essa estrutura externa
// { "t": <MsgType>, "p": { ...payload... } }
// ─────────────────────────────────────────────────────────────────────────────
struct Envelope {
    MsgType type;
    json    payload;

    // Serializa para string JSON (pronto para enviar pelo ServerSocket)
    std::string serialize() const {
        json doc;
        doc["t"] = static_cast<uint16_t>(type);
        doc["p"] = payload;
        return doc.dump();
    }

    // Desserializa — lança std::runtime_error se inválido
    static Envelope parse(const std::string& raw) {
        try {
            auto doc = json::parse(raw);
            Envelope env;
            env.type    = static_cast<MsgType>(doc.at("t").get<uint16_t>());
            env.payload = doc.at("p");
            return env;
        } catch (const json::exception& e) {
            throw std::runtime_error(
                std::string("Envelope::parse falhou: ") + e.what());
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Payloads — um struct por tipo de mensagem
// Cada struct sabe se serializar / desserializar do json do envelope
// ─────────────────────────────────────────────────────────────────────────────

// ── C_Login ──────────────────────────────────────────────────────────────────
struct PktCLogin {
    std::string username;
    std::string password_hash; // SHA-256 feito no cliente

    json to_json() const {
        return { {"u", username}, {"h", password_hash} };
    }
    static PktCLogin from_json(const json& j) {
        return { j.at("u").get<std::string>(),
                 j.at("h").get<std::string>() };
    }
    Envelope envelope() const {
        return { MsgType::C_Login, to_json() };
    }
};

// ── S_LoginOk ────────────────────────────────────────────────────────────────
struct PktSLoginOk {
    uint32_t    entity_id;   // ID da entidade do jogador no ECS
    uint32_t    client_id;   // ID TCP do cliente (usar como ID UDP)
    std::string character;   // nome do personagem
    float       x, y, z;    // posição inicial

    json to_json() const {
        return { {"eid", entity_id}, {"cid", client_id},
                 {"name", character},
                 {"x", x}, {"y", y}, {"z", z} };
    }
    static PktSLoginOk from_json(const json& j) {
        PktSLoginOk r;
        r.entity_id = j.at("eid").get<uint32_t>();
        r.client_id = j.value("cid", uint32_t{0});
        r.character = j.at("name").get<std::string>();
        r.x = j.at("x").get<float>();
        r.y = j.at("y").get<float>();
        r.z = j.at("z").get<float>();
        return r;
    }
    Envelope envelope() const {
        return { MsgType::S_LoginOk, to_json() };
    }
};

// ── S_LoginFail ───────────────────────────────────────────────────────────────
struct PktSLoginFail {
    std::string reason; // "invalid_credentials" | "banned" | "server_full"

    json to_json() const { return { {"reason", reason} }; }
    Envelope envelope() const {
        return { MsgType::S_LoginFail, to_json() };
    }
};

// ── C_Move ────────────────────────────────────────────────────────────────────
struct PktCMove {
    uint32_t entity_id;
    float    x, y, z;     // posição desejada
    float    dir;          // direção (radianos)
    uint8_t  anim_state;   // 0=idle 1=walk 2=run 3=jump

    json to_json() const {
        return { {"eid", entity_id},
                 {"x", x}, {"y", y}, {"z", z},
                 {"d", dir}, {"a", anim_state} };
    }
    static PktCMove from_json(const json& j) {
        return { j.at("eid").get<uint32_t>(),
                 j.at("x").get<float>(),
                 j.at("y").get<float>(),
                 j.at("z").get<float>(),
                 j.at("d").get<float>(),
                 j.at("a").get<uint8_t>() };
    }
    Envelope envelope() const {
        return { MsgType::C_Move, to_json() };
    }
};

// ── S_Snapshot ────────────────────────────────────────────────────────────────
// Enviado via UDP a cada tick para todos os clientes da zona
struct EntityState {
    uint32_t entity_id;
    float    x, y, z;
    float    dir;
    uint8_t  anim_state;
    uint16_t hp;
};

struct PktSSnapshot {
    uint32_t                  tick;    // número do tick do servidor
    std::vector<EntityState>  entities;

    json to_json() const {
        json arr = json::array();
        for (auto& e : entities) {
            arr.push_back({ {"eid", e.entity_id},
                            {"x", e.x}, {"y", e.y}, {"z", e.z},
                            {"d", e.dir}, {"a", e.anim_state},
                            {"hp", e.hp} });
        }
        return { {"tick", tick}, {"e", arr} };
    }
    Envelope envelope() const {
        return { MsgType::S_Snapshot, to_json() };
    }
};

// ── C_Chat ────────────────────────────────────────────────────────────────────
struct PktCChat {
    std::string text; // máx 256 chars (validado no servidor)

    json to_json() const { return { {"msg", text} }; }
    static PktCChat from_json(const json& j) {
        return { j.at("msg").get<std::string>() };
    }
    Envelope envelope() const {
        return { MsgType::C_Chat, to_json() };
    }
};

// ── S_ChatBroadcast ───────────────────────────────────────────────────────────
struct PktSChatBroadcast {
    uint32_t    sender_id;
    std::string sender_name;
    std::string text;

    json to_json() const {
        return { {"sid", sender_id}, {"name", sender_name}, {"msg", text} };
    }
    Envelope envelope() const {
        return { MsgType::S_ChatBroadcast, to_json() };
    }
};

// ── C_Attack ──────────────────────────────────────────────────────────────────
struct PktCAttack {
    uint32_t attacker_id;
    uint32_t target_id;
    uint8_t  skill_id;   // 0 = ataque básico

    json to_json() const {
        return { {"atk", attacker_id}, {"tgt", target_id}, {"sk", skill_id} };
    }
    static PktCAttack from_json(const json& j) {
        return { j.at("atk").get<uint32_t>(),
                 j.at("tgt").get<uint32_t>(),
                 j.at("sk").get<uint8_t>() };
    }
    Envelope envelope() const {
        return { MsgType::C_Attack, to_json() };
    }
};

// ── S_CombatEvent ─────────────────────────────────────────────────────────────
struct PktSCombatEvent {
    uint32_t attacker_id;
    uint32_t target_id;
    int32_t  damage;       // negativo = cura
    uint16_t target_hp;    // HP restante do alvo
    bool     is_kill;

    json to_json() const {
        return { {"atk", attacker_id}, {"tgt", target_id},
                 {"dmg", damage}, {"hp", target_hp}, {"kill", is_kill} };
    }
    Envelope envelope() const {
        return { MsgType::S_CombatEvent, to_json() };
    }
};

// ── Ping / Pong ───────────────────────────────────────────────────────────────
struct PktPing {
    uint64_t timestamp_ms;
    json to_json() const { return { {"ts", timestamp_ms} }; }
    Envelope envelope() const { return { MsgType::Ping, to_json() }; }
};

struct PktPong {
    uint64_t timestamp_ms;
    json to_json() const { return { {"ts", timestamp_ms} }; }
    Envelope envelope() const { return { MsgType::Pong, to_json() }; }
};

// ── Error ─────────────────────────────────────────────────────────────────────
struct PktError {
    std::string code;    // ex: "invalid_packet", "not_authenticated"
    std::string detail;

    json to_json() const { return { {"code", code}, {"detail", detail} }; }
    Envelope envelope() const { return { MsgType::Error, to_json() }; }
};

// ── S_Disconnect ──────────────────────────────────────────────────────────────
struct PktSDisconnect {
    std::string reason; // "kicked" | "server_shutdown" | "timeout"
    json to_json() const { return { {"reason", reason} }; }
    Envelope envelope() const { return { MsgType::S_Disconnect, to_json() }; }
};
