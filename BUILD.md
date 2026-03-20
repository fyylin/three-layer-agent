# Three-Layer Agent System  --  Build Guide

## Requirements

| Platform | Compiler   | Dependencies          |
|----------|------------|-----------------------|
| Windows  | MSVC 2019+ | WinHTTP (built-in)    |
| Linux    | GCC 10+    | OpenSSL, pthread      |
| macOS    | Clang 12+  | OpenSSL, pthread      |

## Windows (Visual Studio 2022)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

## Linux / macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## First-Time Setup

```bash
# Interactive configuration wizard (9 steps)
./build/Release/agent_runner.exe --setup       # Windows
./build/agent_runner              --setup       # Linux

# Or set API key directly
$env:ANTHROPIC_API_KEY = "sk-ant-..."           # Windows
export ANTHROPIC_API_KEY="sk-ant-..."           # Linux
```

## Usage

```bash
# Interactive conversation loop (default)
agent_runner

# Single goal
agent_runner -g "查看桌面上有哪些文件"

# Custom config and workspace
agent_runner -c my_config.json -w ./my_workspace -g "your goal"

# Debug mode (shows raw LLM responses)
agent_runner --debug -g "your goal"

# Lightweight mode (no workspace/logging)
agent_runner --no-workspace -g "your goal"
```

## Architecture (v2)

```
User Input
  └-- SupervisorAgent (Layer 0)
        - Monitor thread: polls state.json, listens to MessageBus
        - On failure: consults AdvisorAgent for root-cause analysis
        - Injects corrections via MessageBus to Workers/Managers
        - Stuck agent detection with escalation (correction → cancel)
        └-- DirectorAgent (Layer 1)
              - Decomposes goal into parallel SubTasks
              - Dispatches ManagerAgents, reviews results, synthesises answer
              - Writes workspace/director/subtasks.json
              └-- ManagerAgent × N (Layer 2, parallel)
                    - Decomposes SubTask into AtomicTasks
                    - Dispatches Workers in parallel
                    - Validates results
                    - Caches to shared session memory
                    └-- WorkerAgent × M (Layer 3, parallel)
                          - Executes one AtomicTask
                          - Calls one tool per task
                          - Reads corrections from MessageBus
                          - Reports progress to Manager
```

## Built-in Tools (12)

| Category   | Tool              | Input                        | Output                           |
|------------|-------------------|------------------------------|----------------------------------|
| Filesystem | list_dir          | "Desktop" or path            | [DIR]/[FILE] listing with sizes  |
| Filesystem | stat_file         | path                         | JSON metadata                    |
| Filesystem | find_files        | "dir\npattern"               | Matching paths                   |
| Filesystem | read_file         | path                         | File text (up to 64KB)           |
| Filesystem | write_file        | "path\ncontent"              | Confirmation                     |
| Filesystem | delete_file       | path                         | Confirmation                     |
| System     | get_env           | VAR_NAME                     | Value or "(not set)"             |
| System     | get_sysinfo       | "" (ignored)                 | JSON: OS, hostname, CPU, memory  |
| System     | get_process_list  | "" or filter substring       | "PID  name" per line             |
| System     | get_current_dir   | "" (ignored)                 | Current working directory        |
| Shell      | run_command       | Shell command string         | stdout+stderr + exit code        |
| Utility    | echo              | Any text                     | Same text                        |

**Note:** `run_command` blocks: rm -rf, del /f/s, format, mkfs, shutdown, reboot, and similar.

## Workspace Layout

```
workspace/
└-- run-001/
    ├-- global.log          NDJSON structured log (all agents)
    ├-- state.json          All agent states (Supervisor reads this)
    ├-- result.json         FinalResult after completion
    ├-- shared/             Cross-agent resource exchange
    ├-- memory/
    │   ├-- session.json    Session KV memory
    │   └-- long_term/      LLM-generated summaries (if enabled)
    ├-- director/
    │   ├-- agent.log
    │   └-- subtasks.json
    ├-- mgr-N/
    │   └-- agent.log
    └-- wkr-N/
        ├-- agent.log
        └-- artifacts/      Files written by write_file (sandboxed)
```

## Configuration

Key fields in `config/agent_config.json`:

```json
{
  "provider": "anthropic|openai|azure|ollama|custom",
  "api_key":  "sk-...",
  "default_model": "claude-opus-4-5-20251101",
  "workspace_dir": "./workspace",
  "memory_session_enabled": true,
  "memory_long_term_enabled": false,
  "supervisor_advisor_enabled": true,
  "supervisor_max_retries": 2,
  "supervisor_stuck_timeout_ms": 300000
}
```

Run `agent_runner --setup` for the interactive 9-step wizard.

## Environment Variables

| Variable          | Description                              |
|-------------------|------------------------------------------|
| ANTHROPIC_API_KEY | Overrides api_key in config              |
| AGENT_DUMP_AUDIT  | Set to "1" to print full audit trail     |

## Prompt Customization

Customize agent behavior by editing files in `prompts/`:

| File | Purpose |
|------|---------|
| `prompts/base.md` | Shared safety rules for all agents |
| `prompts/<agent>/SOUL.md` | Agent identity and communication style |
| `prompts/<agent>/skills/*.md` | Agent-specific capabilities |
| `prompts/skills/*.md` | Cross-agent skills (file_ops, system_ops, etc.) |
| `prompts/AGENTS.md` | Project context (copy to your working directory) |

```bash
# List all available prompts
./agent_runner --list-prompts

# List cross-agent skills
./agent_runner --list-skills
```

To add a new skill, create a `.md` file in `prompts/skills/` with YAML frontmatter:

```markdown
---
name: my-skill
role: cross-agent-skill
version: 1.0.0
description: What this skill does and when to use it
---

# My Skill

## When to load this skill
...
```
