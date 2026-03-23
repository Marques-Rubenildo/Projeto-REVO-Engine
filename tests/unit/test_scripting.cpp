#include <gtest/gtest.h>
#include "engine/scripting/python_bridge.hpp"
#include <filesystem>

TEST(PythonBridgeTest, InicializaSemCrash) {
    PythonBridge bridge;
    bridge.initialize(std::filesystem::current_path() / "scripts");
    EXPECT_TRUE(bridge.is_initialized());
}
