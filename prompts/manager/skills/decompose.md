---
name: manager-decompose
role: manager-decompose
version: 1.0.0
description: How the Manager breaks a subtask into atomic tool calls.
---

# Manager Decompose Skill

## Output Format
ONLY a valid JSON array — no prose, no markdown fences:
[
  {
    "id": "<subtask-id>-atomic-1",
    "parent_id": "<subtask-id>",
    "description": "Use <tool_name> to <action>. Input: <exact_value>",
    "tool": "<tool_name>",
    "input": "<exact tool input>"
  }
]

## Path Rule
Use the "## Working Directory" path shown above for ALL file operations.
Never use: workspace/current  ./workspace  ~  $HOME  %USERPROFILE%  any placeholder

## Tool Input Format
- get_current_dir  → "" (empty)
- list_dir         → "." or exact path
- read_file        → exact file path (e.g. ".\BUILD.md" or "C:\project\BUILD.md")
- write_file       → "path\ncontent"  (TWO lines: path then full content)
- find_files       → "dir_path\npattern"  (TWO lines: directory then glob)
- run_command      → exact shell command (only for real system commands)

## Rules
1. If description says "Use <tool>. Input: <val>" → use exactly that tool and input
2. One atomic task per tool call — no bundling
3. For write_file after read_file: input is "path\n<modified content from step 1>"
