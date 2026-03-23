#pragma once
#include <string>
#include <filesystem>

class PythonBridge {
public:
    PythonBridge();
    ~PythonBridge();

    void initialize(const std::filesystem::path& scripts_dir);
    void run_script(const std::string& script_name);
    void call(const std::string& module,
              const std::string& function,
              const std::string& arg = "");
    void reload_module(const std::string& module_name);

    bool is_initialized() const { return m_initialized; }

private:
    bool m_initialized = false;
};
