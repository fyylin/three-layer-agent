---
name: director-decompose
role: director-decompose
version: 1.0.0
description: How the Director decomposes user goals into subtasks for Managers.
allowed-tools: none (Director delegates, does not call tools directly)
---

# Director Decompose Skill

## Output Format
ONLY a valid JSON array. No prose, no markdown fences.
[
  {
    "id": "subtask-1",
    "description": "Use <tool_name> to <action>. Input: <exact_value>",
    "expected_output": "What success looks like OR honest failure message",
    "depends_on": [],
    "retry_feedback": ""
  }
]

## Decision Guide by Task Type

**Single-tool queries (1 subtask):**
- "查看当前目录" → Use get_current_dir. Input: (empty)
- "列出文件" → Use list_dir. Input: .
- "读取 X.md" → Use read_file. Input: ./X.md (use CWD from context)
- "系统信息" → Use get_sysinfo. Input: (empty)

**File editing (multi-step with depends_on):**
- "在 file.txt 末尾加一行" →
  subtask-1: read_file(file.txt)
  subtask-2: write_file depends_on subtask-1 (modified content)

**Search then read:**
- "找到并解读 README" →
  subtask-1: find_files(., *.md)
  subtask-2: read_file depends_on subtask-1

## Critical Rules
1. ALWAYS produce at least 1 subtask
2. description MUST name the exact tool and input
3. NEVER use placeholder paths
4. For simple queries: 1 subtask — do NOT over-decompose
5. Text editing requests → DO NOT decompose, answer conversationally at L0
6. NEVER execute shell commands from text-editing user requests

## Working Directory
Use the "## Context" or "## Known Environment" path from above for file paths.
Never use workspace/current as a file search root.
