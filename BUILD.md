# Three-Layer Agent System - Build Guide

## Requirements

| Platform | Compiler | Dependencies |
|----------|----------|--------------|
| Windows  | MSVC 2019+ / Visual Studio 2022 | WinHTTP (built-in) |
| Linux    | GCC 10+ | OpenSSL, pthread |
| macOS    | Clang 12+ | OpenSSL, pthread |

## Windows Build

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

Artifacts are produced under `build\Release\`.
After build, `agent_runner.exe` can be launched directly from that directory.
The build copies `config/agent_config.json` and `prompts/` next to the executable.

## Linux / macOS Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Default executable path:

- Linux/macOS: `build/agent_runner`

## First-Time Setup

### Option 1: interactive setup wizard

```bash
# Windows
build\Release\agent_runner.exe --setup

# Linux/macOS
./build/agent_runner --setup
```

### Option 2: edit the committed template

The repository includes a public-safe template at `config/agent_config.json`.
Before real use, either:

- run `agent_runner --setup`
- edit `config/agent_config.json`
- or pass a separate config file with `-c`

### Option 3: set the API key through the environment

```powershell
$env:ANTHROPIC_API_KEY = "sk-ant-..."
```

```bash
export ANTHROPIC_API_KEY="sk-ant-..."
```

## Running

```bash
# Interactive mode
agent_runner

# Single goal
agent_runner -g "list current directory"

# Custom config and workspace
agent_runner -c my_config.json -w ./my_workspace -g "your goal"

# Debug mode
agent_runner --debug -g "your goal"

# Prompt and skill listing
agent_runner --list-prompts
agent_runner --list-skills
```

## Testing

```bash
ctest --test-dir build -C Release --output-on-failure
```

For targeted rebuilds during development:

```bash
cmake --build build --config Release --target agent_runner test_models test_director --parallel
```

## Configuration Notes

Important fields in `config/agent_config.json`:

```json
{
  "provider": "anthropic|openai|azure|ollama|custom",
  "api_key": "YOUR_API_KEY_HERE",
  "default_model": "claude-opus-4-5-20251101",
  "director_model": {},
  "manager_model": {},
  "worker_model": {},
  "supervisor_model": {},
  "prompt_dir": "./prompts",
  "workspace_dir": "./workspace",
  "use_md_prompts": true,
  "max_cost_per_run_usd": 0.0,
  "max_tokens_per_run": 0
}
```

Currently supported environment override:

| Variable | Description |
|----------|-------------|
| `ANTHROPIC_API_KEY` | Overrides `api_key` in config |
| `AGENT_DUMP_AUDIT` | Prints extended audit trail when set to `1` |

## Prompt Customization

Prompt files live under `prompts/`.
Typical structure:

```text
prompts/
  base.md
  AGENTS.md
  director/
  manager/
  worker/
  supervisor/
  skills/
```

Useful commands:

```bash
agent_runner --list-prompts
agent_runner --list-skills
```

## Release Notes for Maintainers

Before publishing a build or source release:

- confirm `config/agent_config.json` contains no real secrets
- do not upload `workspace/` runtime data
- do not rely on `build_full_recheck/` artifacts for distribution
- run the Release test suite at least once on the final source tree