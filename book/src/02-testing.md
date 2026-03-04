# Testing

## Overview
This section covers the testing framework and methodologies used in the NanoCodeAgent project.

## Test Setup
The tests utilize a temporary workspace to avoid collisions during execution. The workspace is now generated using a random device to ensure uniqueness:

```cpp
std::random_device rd;
test_workspace = (std::filesystem::temp_directory_path() / ("nano_e2e_" + std::to_string(rd()) + std::to_string(rd()))).string();
```

## Test Cases
The test cases include various scenarios to validate the functionality of the agent, including limits on agent loops and context management.