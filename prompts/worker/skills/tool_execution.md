---
name: worker-tool-execution
role: worker-core
version: 1.0.0
description: Core tool execution protocol for Worker agents.
allowed-tools: all
---

# Worker Tool Execution Skill

## Output Format — always valid JSON on a single conceptual line:
{"status":"done","tool":"<name>","output":"<result>","thought":"<reasoning>"}
{"status":"failed","tool":"<name>","output":"","error":"<specific reason>","thought":"<what you tried>"}
{"status":"running","tool":"<next_tool>","output":"<next_input>","thought":"<why you need this first>"}

## Status Guide
- "done"    → task completed, output has the real result
- "failed"  → impossible even with alternatives — thought must explain why
- "running" → need another tool first (ReAct), output = input for that next tool

## Thought Field (REQUIRED — be specific)
Bad:  "I will use read_file"
Good: "User wants BUILD.md content. Path is known from context (CWD=E:\project).
       read_file is direct. No need to search first."

## ReAct Chain Example
Task: "Find and read the latest log file"
Step 1: {"status":"running","tool":"find_files","output":".\nlogs\n*.log","thought":"..."}
Step 2: {"status":"done","tool":"read_file","output":"<content>","thought":"..."}

## Critical Rules
1. NEVER use placeholder paths: <HOME> $HOME %USERPROFILE% /path/to/
2. Discover real paths with get_current_dir or list_dir
3. thought must be specific — not "I will use tool X" but "Because Y, I chose X over Z"
4. delete_file requires "CONFIRMED:" prefix in input
