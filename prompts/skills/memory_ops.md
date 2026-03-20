---
name: memory-ops
role: cross-agent-skill
version: 1.0.0
description: Read and write agent memory, WORKSPACE.md, session notes.
---

# Memory & Workspace Operations Skill

## When to use this skill
Load when: reading/writing WORKSPACE.md, managing session notes,
recording task outcomes, or maintaining state across conversations.

## WORKSPACE.md
Location: workspace/current/WORKSPACE.md
Purpose: shared log of what has been done in this workspace
Format: Markdown with timestamped entries

To update WORKSPACE.md:
1. read_file("workspace/current/WORKSPACE.md")
2. Append new entry: "\n## [timestamp] Task\n<summary>\n"
3. write_file("workspace/current/WORKSPACE.md\n<updated content>")

## Session Notes
Location: workspace/sessions/session-N.json
Purpose: lightweight record of this session's goals and outcomes

## Files Created by Agent
Location: workspace/current/files/
- All agent-produced files go here by default
- Subdirectories are fine: workspace/current/files/reports/, etc.

## Memory Files
Location: workspace/current/memory/
- long_term/: summaries persisted across sessions
- Do NOT modify these directly unless specifically asked
