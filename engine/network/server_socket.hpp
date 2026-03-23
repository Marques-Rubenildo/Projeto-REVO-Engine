#pragma once
#include <string>
#include <functional>
#include <cstdint>

// Canal de transporte — usado para rotear mensagens corretamente
enum class Channel : uint8_t {
    Reliable   = 0,  // TCP  — login, chat, inventário, combate crítico
    Unreliable = 1,  // UDP  — posição, animação, inputs de movimento
};

struct Message {
    uint32_t client_id;
    Channel  channel;
    std::string data;
};

using MessageHandler    = std::function<void(const Message&)>;
using ConnectHandler    = std::function<void(uint32_t client_id)>;
using DisconnectHandler = std::function<void(uint32_t client_id)>;

class ServerSocket {
public:
    ServerSocket();
    ~ServerSocket();

    // Inicia os dois listeners: TCP na porta indicada, UDP na porta+1
    // Ex: listen("0.0.0.0", 7777)  →  TCP:7777  UDP:7778
    void listen(const std::string& host, int port);

    // Callbacks de eventos
    void on_message   (MessageHandler    handler);
    void on_connect   (ConnectHandler    handler);
    void on_disconnect(DisconnectHandler handler);

    // Envia para um cliente específico pelo canal adequado
    void send_to(uint32_t client_id, const std::string& data,
                 Channel channel = Channel::Reliable);

    // Broadcast para todos os clientes conectados
    void broadcast(const std::string& data,
                   Channel channel = Channel::Reliable);

    // Processa eventos pendentes (chame no loop principal do servidor)
    void poll();

    void stop();

    // Retorna número de clientes conectados (TCP)
    std::size_t client_count() const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};
