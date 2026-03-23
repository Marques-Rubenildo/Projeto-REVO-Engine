# REVO Engine

🚧 **Projeto em desenvolvimento** — ainda não está pronto para produção.

Engine de MMO RPG moderna com arquitetura híbrida, utilizando **C++20** para desempenho crítico e **Python 3.11+** para lógica de jogo flexível.

---

## 🚀 Visão Geral

A **REVO Engine** é uma engine voltada para jogos MMO, projetada para:

* Alta performance no servidor
* Escalabilidade para múltiplos jogadores
* Flexibilidade na criação de sistemas de jogo
* Separação clara entre núcleo e lógica

A engine segue uma arquitetura em camadas, onde o C++ gerencia sistemas críticos e o Python controla comportamento e regras do jogo.

---

## 🧱 Arquitetura

### 🔹 Core (C++)

Responsável por:

* Sistema de entidades (ECS)
* Rede e sincronização
* Gerenciamento de memória
* Execução multithread

### 🔹 Script Layer (Python)

Responsável por:

* IA de NPCs
* Sistema de quests
* Habilidades e combate
* Eventos do mundo

### 🔹 Integração

* Comunicação via **pybind11**
* APIs expostas do C++ para Python
* Execução baseada em eventos (evita overhead por frame)

---

## ⚙️ Stack Tecnológica

| Camada         | Tecnologia                 |
| -------------- | -------------------------- |
| Core Engine    | C++20                      |
| ECS            | EnTT                       |
| Rede           | Asio (Boost ou standalone) |
| Bindings       | pybind11                   |
| Scripts        | Python 3.11+               |
| Banco de Dados | PostgreSQL                 |
| Cache          | Redis                      |
| Build          | CMake + Conan              |
| Serialização   | FlatBuffers                |

---

## 📦 Pré-requisitos (Windows)

* Visual Studio 2022 (com suporte a C++)
* CMake 3.20+
* Python 3.11+
* Git

---

## 🔨 Build

```bash
build.bat
```

Ou manualmente:

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

---

## 📁 Estrutura do Projeto

```
/engine        → Núcleo em C++
/scripts       → Lógica em Python
/network       → Sistemas de rede
/tools         → Ferramentas auxiliares
/build         → Arquivos de build
```

---

## 🎯 Objetivo do Projeto

Construir uma engine MMO capaz de:

* Suportar múltiplos jogadores simultaneamente
* Operar com baixa latência
* Permitir hot-reload de scripts
* Escalar horizontalmente (shards/zones)

---

## 🧠 Filosofia de Design

* **Performance onde importa** → C++
* **Flexibilidade onde precisa** → Python
* **Eventos ao invés de loops pesados**
* **Separação de responsabilidades clara**

---

## 🌐 Networking (Visão Inicial)

* UDP para movimentação (com confiabilidade customizada)
* TCP para dados críticos
* Sistema de snapshot + delta compression

---

## 📌 Status do Projeto

| Módulo          | Status          |
| --------------- | --------------- |
| Core Engine     | 🟡 Em progresso |
| Sistema de Rede | 🔴 Planejado    |
| Scripts Python  | 🔴 Planejado    |
| Persistência    | 🔴 Planejado    |

---

## 🛠️ Roadmap

* [ ] Implementar ECS base
* [ ] Sistema de rede inicial
* [ ] Integração com Python
* [ ] Sistema de combate
* [ ] Persistência com banco de dados
* [ ] Ferramentas de desenvolvimento

---

## 🤝 Contribuição

Contribuições são bem-vindas!

1. Fork o projeto
2. Crie uma branch (`feature/nova-feature`)
3. Commit suas mudanças
4. Abra um Pull Request

---

## 📜 Licença

Definir (MIT recomendado)

---

## 👤 Autor

**Rubenildo Marques**

---

## 💡 Observação

Este projeto tem foco educacional e experimental, visando aprendizado avançado em:

* Engenharia de software
* Arquitetura de sistemas
* Desenvolvimento de engines
* Sistemas distribuídos

---
