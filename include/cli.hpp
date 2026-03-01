#pragma once

#include "config.hpp"

// Parse command line arguments and update the configuration
// Returns true if parsing was successful and execution should continue
// Returns false if parsing failed or if --help/--version was requested
bool cli_parse(int argc, char* argv[], AgentConfig& config);
