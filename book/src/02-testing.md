# Testing

## Overview
This section covers the testing framework and methodologies used in the NanoCodeAgent project.

## Test Setup
The tests utilize a temporary workspace to avoid collisions during execution. The workspace is generated using a random device to ensure uniqueness, and the corresponding directory is created before use. Different tests may use different prefixes (for example, `nano_e2e_` or `nano_limits_`):

```cpp
std::random_device rd;
const std::string workspace_prefix = /* e.g. "nano_e2e_" or "nano_limits_" */ "nano_e2e_";
std::filesystem::path test_workspace =
    std::filesystem::temp_directory_path() /
    (workspace_prefix + std::to_string(rd()) + std::to_string(rd()));
std::filesystem::create_directories(test_workspace);
```

## Test Cases
The test cases include various scenarios to validate the functionality of the agent, including limits on agent loops and context management.