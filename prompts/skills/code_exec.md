---
name: code-exec
role: cross-agent-skill
version: 1.0.0
description: Execute code files, run builds, compile projects. Load for build/test/run tasks.
---

# Code Execution Skill

## When to use this skill
Load when: running scripts, building projects, executing tests, compiling code.

## Common Patterns

### Run a Python script
run_command: "python script.py"  or  "python3 script.py"

### Run Node.js
run_command: "node app.js"

### CMake build
run_command: "cmake -B build && cmake --build build --config Release"

### npm
run_command: "npm install && npm test"

### cargo (Rust)
run_command: "cargo build --release"

## Output Handling
- Large outputs (>4KB) are truncated — use write_file to capture full output if needed
- Exit codes: non-zero = error, check error message in output

## Working Directory
Commands run in CWD. Use get_current_dir to confirm if unsure.
