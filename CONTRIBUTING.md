# Contributing to Three-Layer Agent System

Thank you for your interest in contributing!

## Quick Start

```bash
git clone <repo>
cd three_layer_agent
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/agent_runner --setup
```

## Running Tests

```bash
# Build and run all 9 test suites (173 tests)
cmake --build build --target test
# or manually:
g++ -std=c++17 -I include -I third_party tests/test_models.cpp src/agent/models.cpp ... -o /tmp/t && /tmp/t
```

## What to Contribute

| Area | Examples |
|------|---------|
| **New tools** | Add to `src/utils/tool_set.cpp` + `include/utils/tool_set.hpp` |
| **New skills** | Add a `.md` file to `prompts/skills/` |
| **New agents** | Follow the `IWorker` / `IManager` interface pattern |
| **Bug fixes** | All bugs welcome, add a test if possible |
| **Prompts** | Improve `prompts/<agent>/SOUL.md` or skill files |

## Code Style

- C++17, no external dependencies beyond `nlohmann/json` (bundled)
- Windows (`WinHTTP`, Wide API) and Linux (`POSIX sockets`) must both compile
- Every new feature needs at least one test in the appropriate `tests/test_*.cpp`
- Use `#ifdef _WIN32` / `#else` guards for platform-specific code

## Pull Request Checklist

- [ ] All 173 tests pass (`GRAND TOTAL: 173/173`)
- [ ] No new `.txt` files in `prompts/` (use `.md` with YAML frontmatter)
- [ ] No hardcoded API keys or paths
- [ ] Windows and Linux compile without errors
- [ ] Update `CHANGELOG.md` with your change

## Prompt Contributions

Skills in `prompts/skills/` are especially welcome. Format:

```markdown
---
name: my-skill
role: cross-agent-skill
version: 1.0.0
description: One line describing when to load this skill
---

# My Skill

## When to use
...

## Steps
...
```

## Questions?

Open an issue with the `question` label.
