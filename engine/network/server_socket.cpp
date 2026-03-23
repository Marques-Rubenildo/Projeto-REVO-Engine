#include "server_socket.hpp"
#include <spdlog/spdlog.h>

// Asio standalone (sem Boost) — já incluído via FetchContent no CMakeLists
#define ASIO_STANDALONE
#include <asio.hpp>

#include <array>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

// ─────────────────────────────────────────────────────────────────────────────
// Constantes de configuração
// ─────────────────────────────────────────────────────────────────────────────
static constexpr std::size_t MAX_CLIENTS     = 1000;
static constexpr std::size_t TCP_READ_BUFFER = 4096;   // bytes por leitura TCP
static constexpr std::size_t UDP_BUFFER_SIZE = 1400;   // MTU seguro para UDP
static constexpr int         IO_THREADS      = 2;      // threads do io_context

// ─────────────────────────────────────────────────────────────────────────────
// Sessão TCP (uma por cliente conectado)
// ─────────────────────────────────────────────────────────────────────────────
class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
    using Ptr = std::shared_ptr<TcpSession>;

    TcpSession(asio::ip::tcp::socket socket, uint32_t id,
               MessageHandler& msg_cb, DisconnectHandler& disc_cb)
        : m_socket(std::move(socket))
        , m_id(id)
        , m_msg_cb(msg_cb)
        , m_disc_cb(disc_cb)
    {}

    void start() { do_read(); }

    void send(const std::string& data) {
        // Prefixo de 4 bytes com o tamanho — framing simples
        auto frame = std::make_shared<std::string>();
        frame->resize(4 + data.size());
        uint32_t len = static_cast<uint32_t>(data.size());
        std::memcpy(frame->data(), &len, 4);
        std::memcpy(frame->data() + 4, data.data(), data.size());

        auto self = shared_from_this();
        asio::async_write(m_socket, asio::buffer(*frame),
            [self, frame](std::error_code ec, std::size_t) {
                if (ec) {
                    spdlog::warn("[TCP] Erro ao enviar para cliente {}: {}",
                                 self->m_id, ec.message());
                }
            });
    }

    uint32_t id() const { return m_id; }

    void close() {
        std::error_code ec;
        m_socket.close(ec);
    }

private:
    void do_read() {
        auto self = shared_from_this();
        // Lê os 4 bytes do header (tamanho da mensagem)
        asio::async_read(m_socket,
            asio::buffer(m_header, 4),
            [self](std::error_code ec, std::size_t) {
                if (ec) {
                    if (ec != asio::error::eof &&
                        ec != asio::error::connection_reset) {
                        spdlog::warn("[TCP] Erro de leitura cliente {}: {}",
                                     self->m_id, ec.message());
                    }
                    if (self->m_disc_cb)
                        self->m_disc_cb(self->m_id);
                    return;
                }
                uint32_t msg_len = 0;
                std::memcpy(&msg_len, self->m_header.data(), 4);
                if (msg_len == 0 || msg_len > TCP_READ_BUFFER * 4) {
                    spdlog::warn("[TCP] Cliente {} enviou tamanho inválido: {}",
                                 self->m_id, msg_len);
                    if (self->m_disc_cb) self->m_disc_cb(self->m_id);
                    return;
                }
                self->m_body.resize(msg_len);
                self->do_read_body();
            });
    }

    void do_read_body() {
        auto self = shared_from_this();
        asio::async_read(m_socket,
            asio::buffer(m_body),
            [self](std::error_code ec, std::size_t) {
                if (ec) {
                    if (self->m_disc_cb) self->m_disc_cb(self->m_id);
                    return;
                }
                if (self->m_msg_cb) {
                    Message msg;
                    msg.client_id = self->m_id;
                    msg.channel   = Channel::Reliable;
                    msg.data      = std::string(self->m_body.begin(),
                                                self->m_body.end());
                    self->m_msg_cb(msg);
                }
                self->do_read();  // lê a próxima mensagem
            });
    }

    asio::ip::tcp::socket m_socket;
    uint32_t              m_id;
    MessageHandler&       m_msg_cb;
    DisconnectHandler&    m_disc_cb;
    std::array<char, 4>   m_header{};
    std::vector<char>     m_body;
};

// ─────────────────────────────────────────────────────────────────────────────
// Implementação interna (Pimpl)
// ─────────────────────────────────────────────────────────────────────────────
struct ServerSocket::Impl {
    asio::io_context                    io;
    asio::ip::tcp::acceptor             tcp_acceptor{io};
    asio::ip::udp::socket               udp_socket{io};
    asio::ip::udp::endpoint             udp_remote;
    std::array<char, UDP_BUFFER_SIZE>   udp_buf{};

    // Mapa client_id → sessão TCP
    std::unordered_map<uint32_t, TcpSession::Ptr> sessions;
    std::mutex                          sessions_mtx;

    // Mapa client_id → endpoint UDP (registrado no primeiro pacote recebido)
    std::unordered_map<uint32_t, asio::ip::udp::endpoint> udp_endpoints;
    std::mutex                          udp_mtx;

    std::atomic<uint32_t>               next_id{1};
    std::atomic<bool>                   running{false};
    std::vector<std::thread>            io_threads;

    MessageHandler    msg_cb;
    ConnectHandler    conn_cb;
    DisconnectHandler disc_cb;

    // ── Aceita novas conexões TCP ──────────────────────────────────────────
    void do_accept() {
        tcp_acceptor.async_accept(
            [this](std::error_code ec, asio::ip::tcp::socket socket) {
                if (!running) return;
                if (ec) {
                    spdlog::error("[TCP] Erro ao aceitar: {}", ec.message());
                    do_accept();
                    return;
                }

                {
                    std::lock_guard<std::mutex> lk(sessions_mtx);
                    if (sessions.size() >= MAX_CLIENTS) {
                        spdlog::warn("[TCP] Limite de {} clientes atingido, "
                                     "conexão recusada.", MAX_CLIENTS);
                        socket.close();
                        do_accept();
                        return;
                    }
                }

                // Configura TCP_NODELAY para baixa latência
                socket.set_option(asio::ip::tcp::no_delay(true));

                uint32_t id = next_id.fetch_add(1);
                auto ep = socket.remote_endpoint();
                spdlog::info("[TCP] Cliente {} conectado de {}:{}",
                             id, ep.address().to_string(), ep.port());

                auto session = std::make_shared<TcpSession>(
                    std::move(socket), id, msg_cb, disc_cb);

                {
                    std::lock_guard<std::mutex> lk(sessions_mtx);
                    sessions[id] = session;
                }

                if (conn_cb) conn_cb(id);
                session->start();

                do_accept();
            });
    }

    // ── Recebe datagramas UDP ──────────────────────────────────────────────
    void do_udp_receive() {
        udp_socket.async_receive_from(
            asio::buffer(udp_buf), udp_remote,
            [this](std::error_code ec, std::size_t bytes) {
                if (!running) return;
                if (ec) {
                    spdlog::warn("[UDP] Erro ao receber: {}", ec.message());
                    do_udp_receive();
                    return;
                }
                if (bytes < 4) { do_udp_receive(); return; }

                // Protocolo UDP simples:
                // [4 bytes: client_id][N bytes: payload]
                uint32_t client_id = 0;
                std::memcpy(&client_id, udp_buf.data(), 4);

                // Registra/atualiza endpoint do cliente
                {
                    std::lock_guard<std::mutex> lk(udp_mtx);
                    udp_endpoints[client_id] = udp_remote;
                }

                if (msg_cb) {
                    Message msg;
                    msg.client_id = client_id;
                    msg.channel   = Channel::Unreliable;
                    msg.data      = std::string(udp_buf.data() + 4,
                                                udp_buf.data() + bytes);
                    msg_cb(msg);
                }

                do_udp_receive();
            });
    }

    // ── Remove sessão TCP desconectada ─────────────────────────────────────
    void remove_session(uint32_t id) {
        std::lock_guard<std::mutex> lk(sessions_mtx);
        auto it = sessions.find(id);
        if (it != sessions.end()) {
            it->second->close();
            sessions.erase(it);
            spdlog::info("[TCP] Cliente {} desconectado.", id);
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// API pública
// ─────────────────────────────────────────────────────────────────────────────
ServerSocket::ServerSocket() : m_impl(new Impl()) {}

ServerSocket::~ServerSocket() {
    stop();
    delete m_impl;
}

void ServerSocket::listen(const std::string& host, int port) {
    using tcp = asio::ip::tcp;
    using udp = asio::ip::udp;

    auto addr = asio::ip::make_address(host);

    // ── TCP ──────────────────────────────────────────────────────────────
    tcp::endpoint tcp_ep(addr, static_cast<uint16_t>(port));
    m_impl->tcp_acceptor.open(tcp_ep.protocol());
    m_impl->tcp_acceptor.set_option(
        asio::ip::tcp::acceptor::reuse_address(true));
    m_impl->tcp_acceptor.bind(tcp_ep);
    m_impl->tcp_acceptor.listen(asio::socket_base::max_listen_connections);

    // ── UDP (porta + 1) ──────────────────────────────────────────────────
    udp::endpoint udp_ep(addr, static_cast<uint16_t>(port + 1));
    m_impl->udp_socket.open(udp_ep.protocol());
    m_impl->udp_socket.bind(udp_ep);

    m_impl->running = true;

    // Inicia operações assíncronas
    m_impl->do_accept();
    m_impl->do_udp_receive();

    // Garante que o io_context não pare enquanto não houver trabalho
    auto work = asio::make_work_guard(m_impl->io);

    // Sobe threads do io_context
    for (int i = 0; i < IO_THREADS; ++i) {
        m_impl->io_threads.emplace_back([this] {
            m_impl->io.run();
        });
    }

    spdlog::info("[Server] Escutando em {}:{} (TCP) e {}:{} (UDP)",
                 host, port, host, port + 1);
}

void ServerSocket::on_message(MessageHandler handler) {
    m_impl->msg_cb = std::move(handler);
}

void ServerSocket::on_connect(ConnectHandler handler) {
    m_impl->conn_cb = std::move(handler);
}

void ServerSocket::on_disconnect(DisconnectHandler handler) {
    // Wrappeia o handler para também remover a sessão do mapa
    m_impl->disc_cb = [this, h = std::move(handler)](uint32_t id) {
        m_impl->remove_session(id);
        if (h) h(id);
    };
}

void ServerSocket::send_to(uint32_t client_id, const std::string& data,
                            Channel channel) {
    if (channel == Channel::Reliable) {
        std::lock_guard<std::mutex> lk(m_impl->sessions_mtx);
        auto it = m_impl->sessions.find(client_id);
        if (it != m_impl->sessions.end())
            it->second->send(data);
    } else {
        // UDP: [4 bytes client_id][payload]
        std::string frame(4 + data.size(), '\0');
        std::memcpy(frame.data(), &client_id, 4);
        std::memcpy(frame.data() + 4, data.data(), data.size());

        std::lock_guard<std::mutex> lk(m_impl->udp_mtx);
        auto it = m_impl->udp_endpoints.find(client_id);
        if (it != m_impl->udp_endpoints.end()) {
            m_impl->udp_socket.async_send_to(
                asio::buffer(frame), it->second,
                [](std::error_code, std::size_t) {});
        }
    }
}

void ServerSocket::broadcast(const std::string& data, Channel channel) {
    if (channel == Channel::Reliable) {
        std::lock_guard<std::mutex> lk(m_impl->sessions_mtx);
        for (auto& [id, session] : m_impl->sessions)
            session->send(data);
    } else {
        std::string frame(4 + data.size(), '\0');
        // client_id = 0 para broadcast UDP
        std::memcpy(frame.data() + 4, data.data(), data.size());

        std::lock_guard<std::mutex> lk(m_impl->udp_mtx);
        for (auto& [id, ep] : m_impl->udp_endpoints) {
            std::memcpy(frame.data(), &id, 4);
            m_impl->udp_socket.async_send_to(
                asio::buffer(frame), ep,
                [](std::error_code, std::size_t) {});
        }
    }
}

void ServerSocket::poll() {
    // poll() processa eventos pendentes sem bloquear.
    // Use no loop do servidor quando quiser controle manual do tick.
    // As threads do io_context já correm em background após listen(),
    // então poll() aqui serve para processar callbacks no thread principal.
    m_impl->io.poll();
}

void ServerSocket::stop() {
    if (!m_impl->running.exchange(false)) return;

    m_impl->tcp_acceptor.close();
    m_impl->udp_socket.close();

    {
        std::lock_guard<std::mutex> lk(m_impl->sessions_mtx);
        for (auto& [id, session] : m_impl->sessions)
            session->close();
        m_impl->sessions.clear();
    }

    m_impl->io.stop();

    for (auto& t : m_impl->io_threads)
        if (t.joinable()) t.join();

    m_impl->io_threads.clear();
    spdlog::info("[Server] Servidor encerrado.");
}

std::size_t ServerSocket::client_count() const {
    std::lock_guard<std::mutex> lk(m_impl->sessions_mtx);
    return m_impl->sessions.size();
}
