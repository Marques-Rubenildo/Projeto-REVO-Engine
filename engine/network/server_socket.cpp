#include "server_socket.hpp"
#include <spdlog/spdlog.h>

#define ASIO_STANDALONE
#include <asio.hpp>

#include <array>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

static constexpr std::size_t MAX_CLIENTS     = 1000;
static constexpr std::size_t UDP_BUFFER_SIZE = 1400;
static constexpr int         IO_THREADS      = 2;

// ── Sessão TCP ────────────────────────────────────────────────────────────────
class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
    using Ptr = std::shared_ptr<TcpSession>;
    using CloseCb = std::function<void(uint32_t)>;

    TcpSession(asio::ip::tcp::socket socket, uint32_t id,
               MessageHandler& msg_cb,
               DisconnectHandler& disc_cb,
               CloseCb close_cb)
        : m_socket(std::move(socket))
        , m_id(id)
        , m_msg_cb(msg_cb)
        , m_disc_cb(disc_cb)
        , m_close_cb(close_cb)
    {}

    void start() { do_read_header(); }

    void send(const std::string& data) {
        auto frame = std::make_shared<std::string>();
        frame->resize(4 + data.size());
        uint32_t len = static_cast<uint32_t>(data.size());
        std::memcpy(frame->data(), &len, 4);
        std::memcpy(frame->data() + 4, data.data(), data.size());

        auto self = shared_from_this();
        asio::async_write(m_socket, asio::buffer(*frame),
            [self, frame](std::error_code ec, std::size_t) {
                if (ec && ec != asio::error::operation_aborted) {
                    spdlog::debug("[TCP] Erro ao enviar para {}: {}",
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
    void on_disconnect() {
        if (m_notified.exchange(true)) return; // notifica só uma vez
        if (m_disc_cb) m_disc_cb(m_id);
        if (m_close_cb) m_close_cb(m_id);
    }

    void do_read_header() {
        auto self = shared_from_this();
        asio::async_read(m_socket, asio::buffer(m_header, 4),
            [self](std::error_code ec, std::size_t) {
                if (ec) {
                    self->on_disconnect();
                    return;
                }
                uint32_t msg_len = 0;
                std::memcpy(&msg_len, self->m_header.data(), 4);
                if (msg_len == 0 || msg_len > 65536) {
                    spdlog::warn("[TCP] Tamanho invalido {} de cliente {}",
                                 msg_len, self->m_id);
                    self->on_disconnect();
                    return;
                }
                self->m_body.resize(msg_len);
                self->do_read_body();
            });
    }

    void do_read_body() {
        auto self = shared_from_this();
        asio::async_read(m_socket, asio::buffer(m_body),
            [self](std::error_code ec, std::size_t) {
                if (ec) {
                    self->on_disconnect();
                    return;
                }
                if (self->m_msg_cb) {
                    Message msg;
                    msg.client_id = self->m_id;
                    msg.channel   = Channel::Reliable;
                    msg.data      = std::string(self->m_body.begin(),
                                                self->m_body.end());
                    try { self->m_msg_cb(msg); }
                    catch (const std::exception& e) {
                        spdlog::error("[TCP] Excecao no handler de {}: {}",
                                      self->m_id, e.what());
                    }
                }
                self->do_read_header();
            });
    }

    asio::ip::tcp::socket  m_socket;
    uint32_t               m_id;
    MessageHandler&        m_msg_cb;
    DisconnectHandler&     m_disc_cb;
    CloseCb                m_close_cb;
    std::atomic<bool>      m_notified{false};
    std::array<char, 4>    m_header{};
    std::vector<char>      m_body;
};

// ── Impl ──────────────────────────────────────────────────────────────────────
struct ServerSocket::Impl {
    asio::io_context                    io;
    asio::ip::tcp::acceptor             tcp_acceptor{io};
    asio::ip::udp::socket               udp_socket{io};
    asio::ip::udp::endpoint             udp_remote;
    std::array<char, UDP_BUFFER_SIZE>   udp_buf{};

    // work_guard como membro — mantém io_context vivo enquanto Impl existir
    asio::executor_work_guard<asio::io_context::executor_type>
        work_guard{io.get_executor()};

    std::unordered_map<uint32_t, TcpSession::Ptr> sessions;
    std::mutex                          sessions_mtx;

    std::unordered_map<uint32_t, asio::ip::udp::endpoint> udp_endpoints;
    std::mutex                          udp_mtx;

    std::atomic<uint32_t>               next_id{1};
    std::atomic<bool>                   running{false};
    std::vector<std::thread>            io_threads;

    MessageHandler    msg_cb;
    ConnectHandler    conn_cb;
    DisconnectHandler disc_cb;

    void remove_session(uint32_t id) {
        std::lock_guard<std::mutex> lk(sessions_mtx);
        sessions.erase(id);
        spdlog::info("[TCP] Cliente {} removido. Total: {}", id, sessions.size());
    }

    void do_accept() {
        tcp_acceptor.async_accept(
            [this](std::error_code ec, asio::ip::tcp::socket socket) {
                if (!running) return;
                if (ec) {
                    if (ec != asio::error::operation_aborted)
                        spdlog::error("[TCP] Erro ao aceitar: {}", ec.message());
                    do_accept();
                    return;
                }

                {
                    std::lock_guard<std::mutex> lk(sessions_mtx);
                    if (sessions.size() >= MAX_CLIENTS) {
                        spdlog::warn("[TCP] Limite atingido, recusando conexao.");
                        socket.close();
                        do_accept();
                        return;
                    }
                }

                socket.set_option(asio::ip::tcp::no_delay(true));
                uint32_t id = next_id.fetch_add(1);

                auto session = std::make_shared<TcpSession>(
                    std::move(socket), id, msg_cb, disc_cb,
                    // close_cb — sempre remove a sessão do mapa
                    [this](uint32_t sid) { remove_session(sid); }
                );

                {
                    std::lock_guard<std::mutex> lk(sessions_mtx);
                    sessions[id] = session;
                }

                spdlog::info("[TCP] Cliente {} conectado.", id);
                if (conn_cb) conn_cb(id);
                session->start();
                do_accept();
            });
    }

    void do_udp_receive() {
        udp_socket.async_receive_from(
            asio::buffer(udp_buf), udp_remote,
            [this](std::error_code ec, std::size_t bytes) {
                if (!running) return;
                if (ec) {
                    if (ec != asio::error::operation_aborted)
                        spdlog::warn("[UDP] Erro: {}", ec.message());
                    do_udp_receive();
                    return;
                }
                if (bytes >= 4) {
                    uint32_t client_id = 0;
                    std::memcpy(&client_id, udp_buf.data(), 4);
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
                        try { msg_cb(msg); }
                        catch (...) {}
                    }
                }
                do_udp_receive();
            });
    }
};

// ── API pública ───────────────────────────────────────────────────────────────
ServerSocket::ServerSocket() : m_impl(new Impl()) {}
ServerSocket::~ServerSocket() { stop(); delete m_impl; }

void ServerSocket::listen(const std::string& host, int port) {
    using tcp = asio::ip::tcp;
    using udp = asio::ip::udp;

    auto addr = asio::ip::make_address(host);

    tcp::endpoint tcp_ep(addr, static_cast<uint16_t>(port));
    m_impl->tcp_acceptor.open(tcp_ep.protocol());
    m_impl->tcp_acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
    m_impl->tcp_acceptor.bind(tcp_ep);
    m_impl->tcp_acceptor.listen(asio::socket_base::max_listen_connections);

    udp::endpoint udp_ep(addr, static_cast<uint16_t>(port + 1));
    m_impl->udp_socket.open(udp_ep.protocol());
    m_impl->udp_socket.bind(udp_ep);

    m_impl->running = true;
    m_impl->do_accept();
    m_impl->do_udp_receive();

    for (int i = 0; i < IO_THREADS; ++i) {
        m_impl->io_threads.emplace_back([this] {
            try { m_impl->io.run(); }
            catch (const std::exception& e) {
                spdlog::error("[IO Thread] Excecao: {}", e.what());
            } catch (...) {
                spdlog::error("[IO Thread] Excecao desconhecida.");
            }
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
    m_impl->disc_cb = std::move(handler);
}

void ServerSocket::send_to(uint32_t client_id, const std::string& data,
                            Channel channel) {
    if (channel == Channel::Reliable) {
        std::lock_guard<std::mutex> lk(m_impl->sessions_mtx);
        auto it = m_impl->sessions.find(client_id);
        if (it != m_impl->sessions.end())
            it->second->send(data);
    } else {
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
    m_impl->io.poll();
}

void ServerSocket::stop() {
    if (!m_impl->running.exchange(false)) return;

    m_impl->work_guard.reset(); // libera o io_context para parar
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
    spdlog::info("[Server] Encerrado.");
}

std::size_t ServerSocket::client_count() const {
    std::lock_guard<std::mutex> lk(m_impl->sessions_mtx);
    return m_impl->sessions.size();
}
