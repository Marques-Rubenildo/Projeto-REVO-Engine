#include "python_bridge.hpp"
#include <pybind11/embed.h>
#include <spdlog/spdlog.h>

namespace py = pybind11;

PythonBridge::PythonBridge()  = default;
PythonBridge::~PythonBridge() = default;

void PythonBridge::initialize(const std::filesystem::path& scripts_dir) {
    try {
        py::initialize_interpreter();
        py::module_::import("sys")
            .attr("path")
            .attr("append")(scripts_dir.string());
        m_initialized = true;
        spdlog::info("Python bridge inicializado. Scripts em: {}", scripts_dir.string());
    } catch (const py::error_already_set& e) {
        spdlog::error("Falha ao inicializar Python: {}", e.what());
    }
}

void PythonBridge::run_script(const std::string& script_name) {
    if (!m_initialized) return;
    try {
        py::eval_file(script_name);
    } catch (const py::error_already_set& e) {
        spdlog::error("Erro em {}: {}", script_name, e.what());
    }
}

void PythonBridge::call(const std::string& module,
                        const std::string& function,
                        const std::string& arg) {
    if (!m_initialized) return;
    try {
        auto mod = py::module_::import(module.c_str());
        auto fn  = mod.attr(function.c_str());
        if (arg.empty()) fn();
        else             fn(arg);
    } catch (const py::error_already_set& e) {
        spdlog::error("Erro ao chamar {}.{}: {}", module, function, e.what());
    }
}

void PythonBridge::reload_module(const std::string& module_name) {
    if (!m_initialized) return;
    try {
        auto importlib = py::module_::import("importlib");
        auto mod       = py::module_::import(module_name.c_str());
        importlib.attr("reload")(mod);
        spdlog::info("Modulo '{}' recarregado.", module_name);
    } catch (const py::error_already_set& e) {
        spdlog::error("Falha ao recarregar {}: {}", module_name, e.what());
    }
}
