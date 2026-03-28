#pragma once
// engine/network/message_dispatcher.hpp
// Conecta o ServerSocket ao protocolo: recebe strings brutas,
// desserializa e chama o handler correto registrado para cada MsgType.

#include "protocol.hpp"
#include "server_socket.hpp"
#include <unordered_map>
#include <functional>

using PacketHandler = std::function<void(uint32_t client_id, const json& payload)>;

class MessageDispatcher {
public:
    // Registra um handler para um tipo de mensagem
    // Ex: dispatcher.on(MsgType::C_Login, [](uint32_t id, const json& p) { ... });
    void on(MsgType type, PacketHandler handler) {
        m_handlers[static_cast<uint16_t>(type)] = std::move(handler);
    }

    // Conecta ao ServerSocket — deve ser chamado após registrar todos os handlers
    void attach(ServerSocket& socket) {
        socket.on_message([this](const Message& msg) {
            dispatch(msg.client_id, msg.data);
        });
    }

    // Processa uma mensagem bruta manualmente (útil para testes)
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
