# Changelog

All notable changes to this project will be documented here.

## [v19.0] - 2026-03-20

### Added
- **Conversation management**: `conv_id` system — one directory per conversation, not per message
- **`/new` `/conv` `/workspace` `/experience` `/help`** conversation commands
- **ExperienceManager**: persistent cross-conversation experience with auto skill promotion
  - `workspace/current/memory/EXPERIENCE.md` — human-readable experience log
  - Strict promotion criteria: ≥3 conversations + ≥90% success rate → `prompts/skills/`
- **All formats migrated to Markdown**: `activity.md`, `MEMORY.md`, `env_knowledge.md`, `summary_N.md`
- **PromptLoader**: YAML frontmatter parsing, `list_all_meta()`, `--list-prompts` / `--list-skills` CLI flags
- 5 cross-agent skills: `file_ops`, `system_ops`, `code_exec`, `memory_ops`, `analysis`

### Changed
- `workspace/logs/agent.log` → `workspace/logs/activity.md`
- `workspace/sessions/run-N/` → `workspace/conversations/conv-YYYYMMDD-xxxxxx/`
- `long_term/summary_N.txt` → `long_term/summary_N.md`
- `env_knowledge.tsv` → `env_knowledge.md` (Markdown table)

### Removed
- Legacy `.txt` prompt files (replaced by `.md` with YAML frontmatter)

## [v18.0] - 2026-03-20

### Added
- PromptLoader file caching (mutex-protected, one disk read per file)
- `gmtime_r` → `#ifdef _WIN32 gmtime_s` guard in all timestamp code
- Setup wizard shows new prompt directory structure
- BUILD.md prompt customization guide

## [v17.0] - 2026-03-20

### Added
- Full MD prompt architecture: `base.md`, `SOUL.md`, agent skills, cross-agent `prompts/skills/`
- `PromptLoader` class (185 lines, header-only): YAML frontmatter, role-based assembly, caching
- `workspace/current/WORKSPACE.md`: auto-generated, agent-writable session history
- `workspace/logs/activity.md`: append-only across all sessions (replaces per-run logs)
- `workspace/current/files/`: default output directory for agent-created files

## [v15-v16] - 2026-03-20

### Added
- **TOOL_SUCCESS_FAST_PATH**: Worker returns tool result directly, skipping LLM formatting
  (fixes `parse_failed` on large file reads, reduces LLM calls by 1 per tool)
- Manager decompose injects real CWD (prevents `workspace/current` path confusion)
- Supervisor cancel threshold: 2 → 4 intervals (prevents premature cancellation)
- Text-edit intent detection in `assess_complexity()` (prevents accidental command execution)

## [v13-v14] - 2026-03-20

### Added
- RouterLLM: per-layer model allocation (Director→sonnet, Manager/Worker/Supervisor→haiku, ~90% cost reduction)
- SIGHUP config hot-reload (`kill -HUP <pid>`)
- `/health` HTTP endpoint (`localhost:8080/health`, POSIX + WinSock, no dependencies)
- Dynamic token pricing (haiku/sonnet/opus rates, replaces hardcoded opus price)
- `CapabilityProfile.success_rates` EMA updates after each task
- `TaskStatus::Rejected` semantic: validated but quality fails (vs `Failed` = execution error)

## [v12.0] - 2026-03-19

### Added
- TF-IDF semantic experience retrieval (`include/utils/tfidf.hpp`)
- D1 Event-driven Supervisor: `MsgType::ToolFailed/SlowWarning/GivingUp`
- C1 SharedContext: explicit predecessor output sharing between Workers
- E2 Few-Shot injection: successful decompositions stored and reused
- OTel-lite: `span_id` field in structured logs

## [v1-v11] - 2026-03-18 / 2026-03-19

### Foundation
- 4-layer architecture: Supervisor / Director / Manager / Worker
- Windows Wide API (WinHTTP, `_wfopen`, `GetCurrentDirectoryW`)
- ReAct chain, HITL user confirmation, checkpoint/resume, budget cap
- L0/L1/L3/L4 complexity routing with Chinese verb detection
- Manager fast-path, E1 rule engine, EnvKnowledgeBase
- 155 → 173 automated tests across 9 suites
