# Changelog

All notable changes to this project will be documented here.

## [v21.0] - 2026-03-22

### Added
- Public-safe committed config template in `config/agent_config.json`
- Documentation refresh for GitHub release use, including clearer quick-start and build guidance
- Documentation index under `docs/README.md`

### Changed
- Runtime path resolution now prefers portable paths relative to the executable and project root
- Release build copies `config/` and `prompts/` next to the executable so `build/Release/agent_runner.exe` can run directly
- README and BUILD documentation now describe the actual portable build and setup flow

### Fixed
- Windows UTF-8 handling for prompt loading and file locking in Chinese-path environments
- Director file-creation workflows now compact more aggressively for sequential execution
- Supervisor review now considers tool evidence instead of only the final answer text
- `list_dir` no longer silently falls back to Desktop when the requested path is invalid
- `AgentConfig` serialization now preserves `supervisor_model`, `use_md_prompts`, and numeric budget fields correctly
- Release source tree no longer relies on a private config file being excluded from version control

## [v20.0] - 2026-03-21

### Added
- Code Indexer Tools: 3 indexer tools integrated into Worker Agent
  - `find_definition`: locate symbol definitions using grep
  - `find_references`: find all references to a symbol
  - `find_callers`: find all call sites of a function
- Windows grep encoding fix: proper UTF-8 handling for grep output on Windows

### Fixed
- Windows command encoding: skip UTF-16LE conversion for grep commands
- Indexer output parsing: handle Windows paths correctly

## [v19.0] - 2026-03-20

### Added
- Conversation management: `conv_id` system, one directory per conversation instead of per message
- `/new`, `/conv`, `/workspace`, `/experience`, and `/help` conversation commands
- ExperienceManager: persistent cross-conversation experience with auto skill promotion
  - `workspace/current/memory/EXPERIENCE.md` records human-readable experience
  - Promotion criteria: at least 3 conversations plus at least 90% success rate
- Markdown migration for `activity.md`, `MEMORY.md`, `env_knowledge.md`, and long-term summaries
- PromptLoader with YAML frontmatter parsing and `--list-prompts` / `--list-skills`
- Cross-agent skills such as `file_ops`, `system_ops`, `code_exec`, `memory_ops`, and `analysis`

### Changed
- `workspace/logs/agent.log` -> `workspace/logs/activity.md`
- `workspace/sessions/run-N/` -> `workspace/conversations/conv-YYYYMMDD-xxxxxx/`
- `long_term/summary_N.txt` -> `long_term/summary_N.md`
- `env_knowledge.tsv` -> `env_knowledge.md`

### Removed
- Legacy `.txt` prompt files in favor of Markdown prompt files with YAML frontmatter

## [v18.0] - 2026-03-20

### Added
- PromptLoader file caching
- Windows-safe timestamp guards using `gmtime_s`
- Setup wizard prompt-directory guidance
- BUILD.md prompt customization guidance

## [v17.0] - 2026-03-20

### Added
- Full Markdown prompt architecture with `base.md`, `SOUL.md`, agent skills, and cross-agent `prompts/skills/`
- `workspace/current/WORKSPACE.md` as an agent-writable session record
- `workspace/logs/activity.md` as append-only activity output
- `workspace/current/files/` as a default output directory for agent-created files

## [v15-v16] - 2026-03-20

### Added
- Tool success fast-path to reduce unnecessary LLM formatting calls
- Manager decompose injects real current working directory
- Supervisor cancel threshold raised to reduce premature cancellation
- Better text-edit intent detection in complexity assessment

## [v13-v14] - 2026-03-20

### Added
- Per-layer model routing for cost reduction
- SIGHUP config hot-reload
- `/health` HTTP endpoint
- Dynamic token pricing
- CapabilityProfile success-rate EMA tracking
- `TaskStatus::Rejected` semantics distinct from execution failure

## [v12.0] - 2026-03-19

### Added
- TF-IDF semantic experience retrieval
- Event-driven Supervisor message types
- SharedContext predecessor-output sharing between Workers