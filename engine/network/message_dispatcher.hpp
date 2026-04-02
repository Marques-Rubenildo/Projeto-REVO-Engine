#pragma once
// engine/network/message_dispatcher.hpp

#include "protocol.hpp"
#include "server_socket.hpp"
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <functional>

using PacketHandler = std::function<void(uint32_t client_id, const json& payload)>;

class MessageDispatcher {
public:
    void on(MsgType type, PacketHandler handler) {
        m_handlers[static_cast<uint16_t>(type)] = std::move(handler);
    }

    void attach(ServerSocket& socket) {
        socket.on_message([this](const Message& msg) {
            dispatch(msg.client_id, msg.data);
        });
    }

    void dispatch(uint32_t client_id, const std::string& raw) {
        try {
            auto env = Envelope::parse(raw);
            auto key = static_cast<uint16_t>(env.type);
            auto it  = m_handlers.find(key);
            if (it != m_handlers.end()) {
                it->second(client_id, env.payload);
            } else {
                spdlog::warn("[Dispatcher] Tipo desconhecido {} de cliente {}",
                             key, client_id);
            }
        } catch (const std::exception& e) {
            spdlog::error("[Dispatcher] Erro ao processar pacote de {}: {}",
                          client_id, e.what());
        }
    }

private:
    std::unordered_map<uint16_t, PacketHandler> m_handlers;
};
