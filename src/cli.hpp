#pragma once

namespace pt {

// Parses argv, dispatches to a subcommand, returns a process exit code.
int run_cli(int argc, char** argv);

}  // namespace pt
