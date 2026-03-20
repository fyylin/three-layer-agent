# Three-Layer Agent System

A **production-grade, multi-layer AI agent framework** written in C++17.
Agents decompose goals, delegate work, review results, and learn from experience — all without human intervention.

> Built on the Anthropic API. Runs on Windows and Linux.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  Supervisor  — quality gate, stuck detection, advisor   │
│  ┌───────────────────────────────────────────────────┐  │
│  │  Director  — decompose goals → subtasks → review  │  │
│  │  ┌─────────────────┐  ┌─────────────────┐        │  │
│  │  │  Manager A       │  │  Manager B       │  ...  │  │
│  │  │  ┌────┐ ┌────┐  │  │  ┌────┐ ┌────┐  │        │  │
│  │  │  │Wkr1│ │Wkr2│  │  │  │Wkr3│ │Wkr4│  │        │  │
│  │  │  └────┘ └────┘  │  │  └────┘ └────┘  │        │  │
│  │  └─────────────────┘  └─────────────────┘        │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

| Layer | Role | LLM |
|-------|------|-----|
| Supervisor | Quality control, stuck detection | haiku |
| Director | Goal decomposition, review, synthesis | sonnet |
| Manager | Subtask → atomic tasks, validation | haiku |
| Worker | Single tool call, ReAct chain | haiku |

**RouterLLM** routes each layer to the most cost-effective model (~90% cost reduction vs all-opus).

---

## Features

- **Tool Fast-Path**: tool results returned directly — no LLM needed to "format" them  
- **Conversation management**: `/new` starts a fresh conversation; history is preserved
- **EXPERIENCE.md**: agents learn from successes/failures across conversations; patterns promote to skills automatically
- **Fully customizable prompts**: every agent's behaviour defined in `prompts/**/*.md` (YAML frontmatter, no recompile needed)
- **Cross-agent skills**: `prompts/skills/` — reusable skill files any agent can load
- **WORKSPACE.md**: persistent, agent-writable log of what was accomplished
- **Windows + Linux**: WinHTTP / POSIX sockets, Wide API throughout
- **/health endpoint**: `localhost:8080/health` for monitoring integration
- **173 automated tests** across 9 suites

---

## Quick Start

### 1. Prerequisites

| Platform | Compiler | Notes |
|----------|----------|-------|
| Windows | MSVC 2019+ | Uses WinHTTP (built-in) |
| Linux | GCC 10+ | Uses POSIX sockets |
| macOS | Clang 12+ | Uses POSIX sockets |

### 2. Build

```bash
# Clone
git clone https://github.com/your-org/three-layer-agent
cd three-layer-agent

# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release          # Linux/macOS
cmake -B build -G "Visual Studio 17 2022" -A x64  # Windows

# Build
cmake --build build --config Release --parallel
```

### 3. Configure

```bash
# Interactive setup wizard
./build/Release/agent_runner.exe --setup   # Windows
./build/agent_runner --setup               # Linux

# Or set API key directly
export ANTHROPIC_API_KEY="sk-ant-..."      # Linux/macOS
$env:ANTHROPIC_API_KEY = "sk-ant-..."     # Windows PowerShell
```

> **Security**: Never commit `config/agent_config.json` if it contains your API key. It is in `.gitignore` by default.

### 4. Run

```bash
./build/agent_runner                          # Interactive mode
./build/agent_runner -g "list current directory"  # Single goal
./build/agent_runner --debug -g "your goal"   # Debug output
./build/agent_runner --list-prompts           # Show all loaded prompts
./build/agent_runner --list-skills            # Show cross-agent skills
```

---

## Workspace Layout

```
workspace/
  current/
    files/              ← agent-created files (default output)
    memory/
      EXPERIENCE.md     ← cross-conversation learning (agent-readable)
      long_term/        ← persistent memory summaries (.md)
    WORKSPACE.md        ← session history (agent can read & write)
    env_knowledge.md    ← discovered paths and facts
  conversations/
    conv-YYYYMMDD-xxx/  ← one directory per conversation
      MEMORY.md         ← this conversation's memory
      runs.md           ← all messages in this conversation
      experience.md     ← experience from this conversation
  logs/
    activity.md         ← all activity, append-only (human-readable)
    structured.ndjson   ← machine-readable logs
```

---

## Conversation Commands

| Command | Effect |
|---------|--------|
| `/new` | Start a fresh conversation (history preserved in `conversations/`) |
| `/conv` | Show current conversation ID |
| `/workspace` | Display `WORKSPACE.md` |
| `/experience` | Display `EXPERIENCE.md` (cross-conversation learning) |
| `/help` | Show all commands |

---

## Customizing Prompts

All prompts are Markdown files with YAML frontmatter — no recompile needed.

```
prompts/
  base.md                    ← shared safety rules (all agents)
  AGENTS.md                  ← project context (copy to working dir)
  director/SOUL.md           ← Director identity & style
  director/skills/           ← Director-specific skills
  manager/  worker/  supervisor/  (same structure)
  skills/                    ← cross-agent skills
    file_ops.md
    system_ops.md
    code_exec.md
    memory_ops.md
    analysis.md
```

To add a skill:

```markdown
---
name: my-skill
role: cross-agent-skill
version: 1.0.0
description: Describe when to load this skill
---

# My Skill

## When to use
...
```

```bash
./agent_runner --list-skills   # verify it appears
```

---

## Experience & Skill Promotion

Workers record experiences after every tool call. When a pattern is seen **≥3 times across different conversations** with **≥90% success rate**, it is automatically promoted to `prompts/skills/exp-<tool>.md`.

```bash
/experience   # view accumulated cross-conversation experience
```

---

## Available Tools

| Tool | Input | Output |
|------|-------|--------|
| `get_current_dir` | (empty) | Current working directory |
| `list_dir` | path | Directory listing |
| `read_file` | path | File contents |
| `write_file` | `"path\ncontent"` | Writes file |
| `find_files` | `"dir\npattern"` | Matching paths |
| `stat_file` | path | Size, dates, type |
| `delete_file` | `"CONFIRMED:path"` | Deletes file |
| `run_command` | shell command | Command output |
| `get_sysinfo` | (empty) | OS, CPU, RAM |
| `get_process_list` | (empty) | Running processes |
| `get_env` | VAR_NAME | Environment variable |
| `create_tool` | `"name\ndesc\ncmd"` | Creates custom tool |
| `list_tools` | (empty) | All available tools |

---

## Testing

```bash
# Run all 173 tests
cmake --build build --target test

# Or compile and run individually
g++ -std=c++17 -I include -I third_party \
    src/agent/models.cpp tests/test_models.cpp -o /tmp/t && /tmp/t
```

Test suites: `test_models`, `test_thread_pool`, `test_worker`, `test_manager`,
`test_director`, `test_v2_infra`, `test_skill_registry`, `test_integration`, `test_prompt_loader`

---

## Configuration

`config/agent_config.json` (created by `--setup`):

```json
{
  "api_key": "",                  // prefer ANTHROPIC_API_KEY env var
  "default_model": "claude-opus-4-6",
  "director_model": { "model": "claude-sonnet-4-6" },
  "manager_model":  { "model": "claude-haiku-4-5-20251001" },
  "worker_model":   { "model": "claude-haiku-4-5-20251001" },
  "supervisor_model": { "model": "claude-haiku-4-5-20251001" },
  "prompt_dir": "./prompts",
  "workspace_dir": "./workspace",
  "use_md_prompts": true
}
```

---

## License

[MIT](LICENSE) — see `LICENSE` for details.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
