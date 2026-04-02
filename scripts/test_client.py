#!/usr/bin/env python3
"""
test_client.py — Cliente de teste para o servidor REVO-ENGINE
Testa: login, chat, movimento (UDP), ping/pong, logout

Uso:
    python scripts/test_client.py
    python scripts/test_client.py --host 127.0.0.1 --port 7777 --users 5
"""

import socket
import json
import struct
import time
import threading
import argparse
import sys
import random
import hashlib
from dataclasses import dataclass, field
from typing import Optional

# ── Tipos de mensagem (espelho do protocol.hpp) ───────────────────────────────
class MsgType:
    C_LOGIN          = 1
    C_MOVE           = 2
    C_CHAT           = 3
    C_ATTACK         = 4
    C_USE_ITEM       = 5
    C_LOGOUT         = 6
    S_LOGIN_OK       = 100
    S_LOGIN_FAIL     = 101
    S_SNAPSHOT       = 102
    S_CHAT_BROADCAST = 103
    S_COMBAT_EVENT   = 104
    S_DISCONNECT     = 105
    PING             = 200
    PONG             = 201
    ERROR            = 255

MSG_NAMES = {v: k for k, v in vars(MsgType).items() if not k.startswith("_")}

# ── Serialização (espelho do Envelope em protocol.hpp) ────────────────────────
def make_packet(msg_type: int, payload: dict) -> bytes:
    """Serializa um pacote com o framing de 4 bytes do servidor."""
    body = json.dumps({"t": msg_type, "p": payload}).encode("utf-8")
    header = struct.pack("<I", len(body))  # uint32 little-endian
    return header + body

def recv_packet(sock: socket.socket) -> Optional[dict]:
    """Lê um pacote do socket TCP com framing de 4 bytes."""
    try:
        header = _recv_exact(sock, 4)
        if not header:
            return None
        length = struct.unpack("<I", header)[0]
        if length == 0 or length > 65536:
            return None
        body = _recv_exact(sock, length)
        if not body:
            return None
        return json.loads(body.decode("utf-8"))
    except Exception:
        return None

def _recv_exact(sock: socket.socket, n: int) -> Optional[bytes]:
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf

# ── Estatísticas de um cliente ────────────────────────────────────────────────
@dataclass
class ClientStats:
    name:            str
    connected:       bool  = False
    logged_in:       bool  = False
    entity_id:       int   = 0
    client_id:       int   = 0   # ID TCP atribuído pelo servidor
    packets_sent:    int   = 0
    packets_recv:    int   = 0
    snapshots_recv:  int   = 0
    chat_recv:       int   = 0
    ping_ms:         float = 0.0
    errors:          list  = field(default_factory=list)

# ── Cliente individual ────────────────────────────────────────────────────────
class TestClient:
    def __init__(self, host: str, tcp_port: int, username: str):
        self.host      = host
        self.tcp_port  = tcp_port
        self.udp_port  = tcp_port + 1
        self.username  = username
        self.stats     = ClientStats(name=username)
        self._tcp           = None
        self._udp           = None
        self._running       = False
        self._recv_thread   = None
        self._udp_thread    = None

    def connect(self) -> bool:
        try:
            self._tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._tcp.settimeout(5.0)
            self._tcp.connect((self.host, self.tcp_port))
            self._tcp.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

            self._udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._udp.settimeout(1.0)

            self.stats.connected = True
            self._running = True

            # Thread de recepção TCP
            self._recv_thread = threading.Thread(
                target=self._recv_loop, daemon=True)
            self._recv_thread.start()

            # Thread receptora UDP para snapshots
            self._udp_thread = threading.Thread(
                target=self._udp_recv_loop, daemon=True)
            self._udp_thread.start()

            return True
        except Exception as e:
            self.stats.errors.append(f"connect: {e}")
            return False

    def disconnect(self):
        self._running = False
        try:
            self._send(MsgType.C_LOGOUT, {})
            time.sleep(0.1)
        except Exception:
            pass
        if self._tcp:
            self._tcp.close()
        if self._udp:
            self._udp.close()

    def login(self) -> bool:
        pwd_hash = hashlib.sha256(b"test_password").hexdigest()
        self._send(MsgType.C_LOGIN, {"u": self.username, "h": pwd_hash})
        # Aguarda S_LoginOk por até 3s
        deadline = time.time() + 3.0
        while time.time() < deadline:
            if self.stats.logged_in:
                return True
            time.sleep(0.05)
        self.stats.errors.append("login timeout")
        return False

    def send_move(self, x: float, y: float, z: float):
        payload = {
            "eid": self.stats.entity_id,
            "x": x, "y": y, "z": z,
            "d": random.uniform(0, 6.28),
            "a": 1  # walking
        }
        # Movimento vai via UDP
        body = json.dumps({"t": MsgType.C_MOVE, "p": payload}).encode()
        # Usa client_id TCP como identificador no protocolo UDP
        udp_id = self.stats.client_id if self.stats.client_id > 0 else self.stats.entity_id
        prefix = struct.pack("<I", udp_id)
        try:
            self._udp.sendto(prefix + body, (self.host, self.udp_port))
            self.stats.packets_sent += 1
        except Exception as e:
            self.stats.errors.append(f"udp_send: {e}")

    def send_chat(self, text: str):
        self._send(MsgType.C_CHAT, {"msg": text})

    def ping(self) -> float:
        ts = int(time.time() * 1000)
        self._send(MsgType.PING, {"ts": ts})
        # ping_ms atualizado pelo recv_loop
        time.sleep(0.2)
        return self.stats.ping_ms

    def _send(self, msg_type: int, payload: dict):
        try:
            self._tcp.sendall(make_packet(msg_type, payload))
            self.stats.packets_sent += 1
        except Exception as e:
            self.stats.errors.append(f"send: {e}")

    def _udp_recv_loop(self):
        """Recebe snapshots UDP do servidor."""
        while self._running:
            try:
                data, _ = self._udp.recvfrom(4096)
                if len(data) < 4:
                    continue
                # Remove prefixo de 4 bytes (client_id) e parseia
                body = data[4:]
                try:
                    pkt = json.loads(body.decode("utf-8"))
                    if pkt.get("t") == MsgType.S_SNAPSHOT:
                        self.stats.snapshots_recv += 1
                except Exception:
                    pass
            except socket.timeout:
                continue
            except Exception:
                break

    def _recv_loop(self):
        while self._running:
            pkt = recv_packet(self._tcp)
            if pkt is None:
                if self._running:
                    self.stats.errors.append("connection lost")
                break

            self.stats.packets_recv += 1
            t   = pkt.get("t", -1)
            pay = pkt.get("p", {})

            if t == MsgType.S_LOGIN_OK:
                self.stats.logged_in = True
                self.stats.entity_id = pay.get("eid", 0)
                self.stats.client_id = pay.get("cid", 0)

            elif t == MsgType.S_LOGIN_FAIL:
                self.stats.errors.append(f"login_fail: {pay.get('reason')}")

            elif t == MsgType.S_SNAPSHOT:
                self.stats.snapshots_recv += 1

            elif t == MsgType.S_CHAT_BROADCAST:
                self.stats.chat_recv += 1

            elif t == MsgType.PONG:
                sent_ts = pay.get("ts", 0)
                self.stats.ping_ms = time.time() * 1000 - sent_ts

            elif t == MsgType.ERROR:
                self.stats.errors.append(f"server_error: {pay.get('code')}")

# ── Suite de testes ───────────────────────────────────────────────────────────
class TestSuite:
    def __init__(self, host: str, port: int):
        self.host  = host
        self.port  = port
        self.passed = 0
        self.failed = 0

    def _assert(self, name: str, condition: bool, detail: str = ""):
        if condition:
            print(f"  [PASS] {name}")
            self.passed += 1
        else:
            print(f"  [FAIL] {name}" + (f" — {detail}" if detail else ""))
            self.failed += 1

    def run_single_client(self):
        print("\n── Teste 1: Cliente único ──────────────────────────────")
        c = TestClient(self.host, self.port, "heroi_teste")

        self._assert("Conexão TCP",
                     c.connect(), str(c.stats.errors))

        self._assert("Login aceito",
                     c.login(), str(c.stats.errors))

        self._assert("Login OK recebido (entity atribuído)",
                     c.stats.logged_in,
                     f"entity_id={c.stats.entity_id} logged={c.stats.logged_in}")

        ping = c.ping()
        self._assert("Ping/Pong respondido",
                     ping > 0, f"ping={ping:.1f}ms")

        c.send_chat("Ola servidor!")
        time.sleep(0.3)
        self._assert("Chat broadcast recebido",
                     c.stats.chat_recv > 0,
                     f"chat_recv={c.stats.chat_recv}")

        # Envia 5 pacotes de movimento UDP
        for i in range(5):
            c.send_move(float(i), 0.0, float(i))
            time.sleep(0.06)

        # Aguarda snapshots (servidor envia a cada 50ms)
        time.sleep(0.5)
        self._assert("Snapshot UDP recebido",
                     c.stats.snapshots_recv > 0,
                     f"snapshots={c.stats.snapshots_recv}")

        c.disconnect()
        print(f"  Pacotes enviados: {c.stats.packets_sent}")
        print(f"  Pacotes recebidos: {c.stats.packets_recv}")

    def run_multi_client(self, n: int = 5):
        print(f"\n── Teste 2: {n} clientes simultâneos ───────────────────")
        clients = [TestClient(self.host, self.port, f"player_{i}") for i in range(n)]

        # Conecta todos em paralelo
        threads = []
        def connect_and_login(c):
            if c.connect():
                c.login()
        for c in clients:
            t = threading.Thread(target=connect_and_login, args=(c,))
            t.start()
            threads.append(t)
        for t in threads:
            t.join(timeout=8)

        logged = sum(1 for c in clients if c.stats.logged_in)
        self._assert(f"Todos os {n} clientes logaram",
                     logged == n, f"logados={logged}/{n}")

        # Chat cruzado — cada cliente envia uma mensagem
        for i, c in enumerate(clients):
            if c.stats.logged_in:
                c.send_chat(f"msg do player_{i}")
        time.sleep(0.5)

        # Cada cliente deve ter recebido todas as mensagens
        logged_clients = [c for c in clients if c.stats.logged_in]
        if logged_clients:
            min_chat = min(c.stats.chat_recv for c in logged_clients)
        else:
            min_chat = 0
        self._assert("Chat distribuído para todos",
                     min_chat >= max(0, n - 2),  # tolerância: pode perder até 2
                     f"min_chat_recv={min_chat} logados={len(logged_clients)}")

        # Movimento simultâneo
        def move_loop(c, steps=10):
            for i in range(steps):
                c.send_move(float(i), 0.0, float(i))
                time.sleep(0.05)

        move_threads = [threading.Thread(target=move_loop, args=(c,))
                        for c in clients if c.stats.logged_in]
        for t in move_threads:
            t.start()
        for t in move_threads:
            t.join()

        time.sleep(0.5)
        total_snapshots = sum(c.stats.snapshots_recv for c in clients)
        self._assert("Snapshots recebidos por múltiplos clientes",
                     total_snapshots > 0,
                     f"total_snapshots={total_snapshots}")

        for c in clients:
            c.disconnect()

        errors = [e for c in clients for e in c.stats.errors]
        self._assert("Sem erros críticos",
                     len(errors) == 0,
                     f"erros: {errors[:3]}")

    def run_stress(self, duration_s: int = 10):
        print(f"\n── Teste 3: Stress {duration_s}s / 50 clientes ─────────────")
        n = 50
        clients = [TestClient(self.host, self.port, f"stress_{i}") for i in range(n)]

        connected = sum(1 for c in clients if c.connect())
        self._assert(f"50 conexões simultâneas aceitas",
                     connected >= 45,  # tolerância 10%
                     f"conectados={connected}")

        logged = 0
        login_threads = []
        for c in clients:
            if c.stats.connected:
                t = threading.Thread(target=lambda c=c: c.login())
                t.start()
                login_threads.append(t)
        for t in login_threads:
            t.join(timeout=10)
        logged = sum(1 for c in clients if c.stats.logged_in)
        self._assert("50 logins processados",
                     logged >= 45,
                     f"logados={logged}")

        # Envia movimento por duration_s segundos
        stop_evt = threading.Event()
        def spam_move(c):
            while not stop_evt.is_set():
                c.send_move(random.uniform(-100, 100),
                            0.0,
                            random.uniform(-100, 100))
                time.sleep(0.05)

        move_threads = [threading.Thread(target=spam_move, args=(c,), daemon=True)
                        for c in clients if c.stats.logged_in]
        for t in move_threads:
            t.start()

        time.sleep(duration_s)
        stop_evt.set()

        total_sent = sum(c.stats.packets_sent for c in clients)
        total_recv = sum(c.stats.packets_recv for c in clients)
        total_snap = sum(c.stats.snapshots_recv for c in clients)

        print(f"  Pacotes enviados (total): {total_sent}")
        print(f"  Pacotes recebidos (total): {total_recv}")
        print(f"  Snapshots recebidos (total): {total_snap}")
        self._assert("Servidor sobreviveu ao stress",
                     total_snap > 0, f"snapshots={total_snap}")

        for c in clients:
            c.disconnect()

    def summary(self):
        total = self.passed + self.failed
        print(f"\n{'='*50}")
        print(f"Resultado: {self.passed}/{total} testes passaram")
        if self.failed == 0:
            print("STATUS: TUDO OK ✓")
        else:
            print(f"STATUS: {self.failed} FALHA(S) ✗")
        print('='*50)
        return self.failed == 0

# ── Entrypoint ────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="REVO-ENGINE test client")
    parser.add_argument("--host",  default="127.0.0.1")
    parser.add_argument("--port",  type=int, default=7777)
    parser.add_argument("--users", type=int, default=5)
    parser.add_argument("--stress", action="store_true",
                        help="Roda teste de stress com 50 clientes por 10s")
    args = parser.parse_args()

    print(f"REVO-ENGINE Test Client")
    print(f"Servidor: {args.host}:{args.port}")
    print(f"Certifique-se que o servidor esta rodando antes de executar.")

    suite = TestSuite(args.host, args.port)
    suite.run_single_client()
    suite.run_multi_client(args.users)

    if args.stress:
        suite.run_stress(duration_s=10)

    ok = suite.summary()
    sys.exit(0 if ok else 1)
