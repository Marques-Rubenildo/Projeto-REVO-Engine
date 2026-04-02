#include "engine/core/engine.hpp"
#include <spdlog/spdlog.h>
#include <windows.h>
#include <dbghelp.h>
#include <fstream>

// Captura crashes nao tratados no Windows e salva em crash.log
static LONG WINAPI crash_handler(EXCEPTION_POINTERS* ep) {
    auto code = ep->ExceptionRecord->ExceptionCode;
    
    std::ofstream crash("logs\\crash.log", std::ios::app);
    crash << "=== CRASH ===\n";
    crash << "Codigo: 0x" << std::hex << code << "\n";
    crash << "Endereco: 0x" << std::hex 
          << (uintptr_t)ep->ExceptionRecord->ExceptionAddress << "\n";
    crash.close();

    spdlog::critical("=== CRASH NAO TRATADO === Codigo: 0x{:X}", code);
    spdlog::critical("Endereco: 0x{:X}", 
                     (uintptr_t)ep->ExceptionRecord->ExceptionAddress);
    
    // Casos comuns:
    // 0xC0000005 = Access Violation
    // 0xC0000094 = Integer divide by zero  
    // 0x40010005 = Ctrl+C (nao e crash, e sinal)
    
    if (code == 0x40010005 || code == 0x40010004) {
        // Ctrl+C ou Ctrl+Break — nao e crash, encerra limpo
        return EXCEPTION_CONTINUE_SEARCH;
    }
    
    return EXCEPTION_EXECUTE_HANDLER;
}

int main() {
    // Instala handler de crash ANTES de qualquer outra coisa
    SetUnhandledExceptionFilter(crash_handler);
    
    spdlog::set_level(spdlog::level::debug);

    EngineConfig config;
    config.server_host = "0.0.0.0";
    config.server_port = 7777;
    config.max_players = 1000;
    config.tick_rate   = 20;
    config.scripts_dir = "scripts";

    Engine engine(config);
    try {
        engine.initialize();
        engine.run();
    } catch (const std::exception& e) {
        spdlog::critical("Erro fatal: {}", e.what());
        std::ofstream crash("logs\\crash.log", std::ios::app);
        crash << "Exception: " << e.what() << "\n";
        return 1;
    } catch (...) {
        spdlog::critical("Erro fatal desconhecido.");
        return 1;
    }
    return 0;
}
