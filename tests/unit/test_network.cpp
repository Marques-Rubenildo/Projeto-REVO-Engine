#include <gtest/gtest.h>
#include "engine/network/protocol.hpp"
#include "engine/network/message_dispatcher.hpp"
#include "engine/network/server_socket.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Testes do protocolo (serialização / desserialização)
// Estes rodam sem precisar de rede — apenas validam o protocol.hpp
// ─────────────────────────────────────────────────────────────────────────────

TEST(ProtocolTest, EnvelopeRoundtrip) {
    PktCLogin pkt{ "heroi", "abc123hash" };
    auto env     = pkt.envelope();
    auto raw     = env.serialize();
    auto env2    = Envelope::parse(raw);

    EXPECT_EQ(static_cast<uint16_t>(env2.type),
              static_cast<uint16_t>(MsgType::C_Login));

    auto pkt2 = PktCLogin::from_json(env2.payload);
    EXPECT_EQ(pkt2.username,      "heroi");
    EXPECT_EQ(pkt2.password_hash, "abc123hash");
}

TEST(ProtocolTest, MovePacketRoundtrip) {
    PktCMove pkt{ 42, 1.5f, 0.0f, 3.7f, 1.2f, 2 };
    auto env  = Envelope::parse(pkt.envelope().serialize());
    auto pkt2 = PktCMove::from_json(env.payload);

    EXPECT_EQ(pkt2.entity_id, 42u);
    EXPECT_FLOAT_EQ(pkt2.x, 1.5f);
    EXPECT_FLOAT_EQ(pkt2.z, 3.7f);
    EXPECT_EQ(pkt2.anim_state, 2);
}

TEST(ProtocolTest, SnapshotRoundtrip) {
    PktSSnapshot snap;
    snap.tick = 100;
    snap.entities.push_back({ 1, 10.f, 0.f, 20.f, 0.f, 1, 95 });
    snap.entities.push_back({ 2, 30.f, 0.f, 40.f, 1.f, 0, 50 });

    auto raw  = snap.envelope().serialize();
    auto env  = Envelope::parse(raw);

    EXPECT_EQ(static_cast<uint16_t>(env.type),
              static_cast<uint16_t>(MsgType::S_Snapshot));

    auto tick  = env.payload.at("tick").get<uint32_t>();
    auto arr   = env.payload.at("e");
    EXPECT_EQ(tick, 100u);
    EXPECT_EQ(arr.size(), 2u);
    EXPECT_FLOAT_EQ(arr[0]["x"].get<float>(), 10.f);
    EXPECT_EQ(arr[1]["hp"].get<uint16_t>(), 50u);
}

TEST(ProtocolTest, InvalidEnvelopeThrows) {
    EXPECT_THROW(Envelope::parse("not json"), std::runtime_error);
    EXPECT_THROW(Envelope::parse("{\"t\":1}"),  std::runtime_error); // sem "p"
    EXPECT_THROW(Envelope::parse("{}"),         std::runtime_error);
}

TEST(ProtocolTest, ChatPacketMaxLength) {
    // Simula truncamento de 256 chars (feito no engine.cpp)
    std::string long_msg(300, 'x');
    if (long_msg.size() > 256) long_msg.resize(256);
    PktCChat chat{ long_msg };
    auto pkt2 = PktCChat::from_json(
        Envelope::parse(chat.envelope().serialize()).payload);
    EXPECT_EQ(pkt2.text.size(), 256u);
}

TEST(ProtocolTest, LoginOkRoundtrip) {
    PktSLoginOk ok{ 7, "guerreiro", 10.f, 0.f, 25.f };
    auto env  = Envelope::parse(ok.envelope().serialize());
    auto ok2  = PktSLoginOk::from_json(env.payload);
    EXPECT_EQ(ok2.entity_id, 7u);
    EXPECT_EQ(ok2.character, "guerreiro");
    EXPECT_FLOAT_EQ(ok2.x, 10.f);
    EXPECT_FLOAT_EQ(ok2.z, 25.f);
}

TEST(ProtocolTest, PingPongRoundtrip) {
    PktPing ping{ 987654321ULL };
    auto env  = Envelope::parse(ping.envelope().serialize());
    EXPECT_EQ(static_cast<uint16_t>(env.type),
              static_cast<uint16_t>(MsgType::Ping));
    EXPECT_EQ(env.payload.at("ts").get<uint64_t>(), 987654321ULL);
}

// ─────────────────────────────────────────────────────────────────────────────
// Testes do MessageDispatcher (sem socket real)
// ─────────────────────────────────────────────────────────────────────────────

TEST(DispatcherTest, RotaParaHandlerCorreto) {
    MessageDispatcher disp;
    bool login_called = false;
    bool move_called  = false;

    disp.on(MsgType::C_Login, [&](uint32_t id, const json& p) {
        EXPECT_EQ(id, 1u);
        EXPECT_EQ(p.at("u").get<std::string>(), "test");
        login_called = true;
    });

    disp.on(MsgType::C_Move, [&](uint32_t, const json&) {
        move_called = true;
    });

    // Despacha login
    auto raw_login = PktCLogin{"test", "hash"}.envelope().serialize();
    disp.dispatch(1, raw_login);
    EXPECT_TRUE(login_called);
    EXPECT_FALSE(move_called);

    // Despacha move
    PktCMove mv{ 1, 1.f, 0.f, 2.f, 0.f, 1 };
    disp.dispatch(1, mv.envelope().serialize());
    EXPECT_TRUE(move_called);
}

TEST(DispatcherTest, MensagemInvalidaNaoQuebraServidor) {
    MessageDispatcher disp;
    // Sem handlers registrados e com JSON inválido — não deve lançar
    EXPECT_NO_THROW(disp.dispatch(1, "lixo"));
    EXPECT_NO_THROW(disp.dispatch(1, "{\"t\":999,\"p\":{}}"));
}

TEST(DispatcherTest, TipoSemHandlerNaoQuebraServidor) {
    MessageDispatcher disp;
    // Tipo 999 não tem handler — deve apenas logar warning
    auto raw = PktPing{123}.envelope().serialize();
    EXPECT_NO_THROW(disp.dispatch(1, raw));
}
