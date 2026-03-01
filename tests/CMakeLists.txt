# Add test executable
add_executable(test_cli test_cli.cpp ../src/cli.cpp ../src/config.cpp)

# Link against gtest and spdlog
target_link_libraries(test_cli PRIVATE GTest::gtest_main spdlog::spdlog)
target_include_directories(test_cli PRIVATE ../include)

# Discover and run tests
include(GoogleTest)
gtest_discover_tests(test_cli)
