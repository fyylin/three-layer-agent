# Three-Layer Agent System

A multi-layer AI agent framework written in C++17.
It decomposes user goals, delegates work across Director, Manager, and Worker agents, and uses a Supervisor layer for quality control, retries, and stuck-agent intervention.

## Highlights

- Multi-layer execution pipeline: Supervisor -> Director -> Manager -> Worker
- Built-in filesystem, system, and shell tools
- Prompt-driven behavior under `prompts/`, with Markdown skill files and no recompile required for prompt changes
- Windows-focused UTF-8 path handling, including Chinese path support in key runtime and tooling paths
- Portable runtime layout: the built executable can load `config/` and `prompts/` from the build output directory
- Public-safe committed config template in `config/agent_config.json`
- Full Release test suite currently passes with `ctest`

## Architecture

```text
User Input
  -> SupervisorAgent
       - quality gate
       - stuck detection
       - advisor-assisted retries
       - correction injection
    -> DirectorAgent
         - classify complexity
         - decompose goal into subtasks
         - dispatch managers
         - review and synthesise final answer
      -> ManagerAgent x N
           - decompose subtask into atomic tasks
           - validate outputs
           - coordinate workers
        -> WorkerAgent x M
             - execute one atomic task
             - use tools directly when possible
```

## Core Capabilities

- Goal decomposition and hierarchical execution
- Prompt-based routing and behavior control
- Shared workspace, memory, and structured logs
- Session memory and optional long-term memory
- Configurable model/provider settings per layer
- Optional code indexing components and supporting utilities
- Supervisor review tuned for practical, evidence-based acceptance

## Repository Layout

```text
config/        Runtime config template
include/       Public headers
prompts/       Agent prompts and skills
src/           Implementation
tests/         Automated tests
docs/          Design notes and implementation writeups
build/         Generated build output (not for source control)
workspace/     Runtime state and artifacts
```

## Quick Start

### Requirements

| Platform | Compiler | Notes |
|----------|----------|-------|
| Windows  | MSVC 2019+ / VS 2022 | Uses WinHTTP and wide-character Win32 APIs |
| Linux    | GCC 10+  | Uses OpenSSL and pthread |
| macOS    | Clang 12+ | Uses OpenSSL and pthread |

### Build

Windows:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

Linux / macOS:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

After build, start the executable from the build output directory:

- Windows: `build\Release\agent_runner.exe`
- Linux/macOS: `build/agent_runner`

The build copies `config/agent_config.json` and `prompts/` next to the executable so the Release output can run directly.

### First-Time Setup

Option 1: run the setup wizard

```bash
# Windows
build\Release\agent_runner.exe --setup

# Linux/macOS
./build/agent_runner --setup
```

Option 2: edit the committed template in `config/agent_config.json`

Option 3: override the API key with an environment variable

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

# Prompt and skill discovery
agent_runner --list-prompts
agent_runner --list-skills
```

## Configuration

The repository includes a public-safe config template at `config/agent_config.json`.
It is intended to be committed without secrets.

Important fields include:

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

Recommended practice:

- Keep the committed template clean
- Put personal overrides in a separate file such as `config/dev.local.json`
- Pass that file explicitly with `-c`

## Workspace Layout

A typical runtime workspace looks like this:

```text
workspace/
  run-001/
    global.log
    state.json
    result.json
    shared/
    memory/
    director/
    mgr-N/
    wkr-N/
```

## Prompts and Skills

Prompts are loaded from `prompts/`.
The system supports shared prompt rules, agent identity files, and per-role skills.

Useful files:

- `prompts/base.md`
- `prompts/<agent>/SOUL.md`
- `prompts/<agent>/skills/*.md`
- `prompts/skills/*.md`
- `prompts/AGENTS.md`

This makes behavior iteration possible without recompiling the C++ code.

## Built-in Tooling

Main built-in tool groups include:

- Filesystem: `list_dir`, `read_file`, `write_file`, `find_files`, `stat_file`, `delete_file`
- System: `get_env`, `get_sysinfo`, `get_process_list`, `get_current_dir`
- Shell: `run_command`
- Utility: `echo`

`run_command` includes blocking for clearly destructive commands.

## Testing

Run the Release suite after building:

```bash
ctest --test-dir build -C Release --output-on-failure
```

The current Release build in this repository passes the full registered test suite.

## Documentation

Start with these files:

- [BUILD.md](BUILD.md) for build and setup
- [CHANGELOG.md](CHANGELOG.md) for release history
- [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines
- [docs/README.md](docs/README.md) for design-note navigation

## Security Notes

- Do not commit real API keys or private endpoints
- Review `config/agent_config.json` before publishing
- Prefer a separate local config file for personal settings
- Check generated `build/` and `workspace/` output before uploading archives or tags

## License

MIT. See [LICENSE](LICENSE).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).