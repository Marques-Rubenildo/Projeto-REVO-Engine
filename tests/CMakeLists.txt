FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0)
FetchContent_MakeAvailable(googletest)

# Testes do ECS
add_executable(mmo_tests_ecs unit/test_ecs.cpp)
target_compile_features(mmo_tests_ecs PRIVATE cxx_std_20)
target_include_directories(mmo_tests_ecs PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR})
target_link_libraries(mmo_tests_ecs PRIVATE mmo_engine GTest::gtest_main)

# Testes de scripting Python
add_executable(mmo_tests_scripting unit/test_scripting.cpp)
target_compile_features(mmo_tests_scripting PRIVATE cxx_std_20)
target_include_directories(mmo_tests_scripting PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR})
target_link_libraries(mmo_tests_scripting PRIVATE mmo_engine GTest::gtest_main)

# Testes de rede (protocolo + dispatcher)
add_executable(mmo_tests_network unit/test_network.cpp)
target_compile_features(mmo_tests_network PRIVATE cxx_std_20)
target_include_directories(mmo_tests_network PRIVATE
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR})
target_link_libraries(mmo_tests_network PRIVATE mmo_engine GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(mmo_tests_ecs)
gtest_discover_tests(mmo_tests_scripting)
gtest_discover_tests(mmo_tests_network)
