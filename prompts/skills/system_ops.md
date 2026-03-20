---
name: system-ops
role: cross-agent-skill
version: 1.0.0
description: System queries: sysinfo, processes, environment variables, shell commands.
---

# System Operations Skill

## When to use this skill
Load when: querying system state, running commands, checking environment,
listing processes, or getting platform information.

## get_sysinfo
Input: "" (empty) → OS name/version, CPU, RAM, architecture

## get_process_list
Input: "" (empty) → running processes with PID and name

## get_env
Input: VARIABLE_NAME (e.g. "PATH", "USERPROFILE", "HOME", "TEMP")
→ Returns the value of that environment variable

## get_current_dir
Input: "" (empty) → absolute path of current working directory

## run_command
Input: shell command string
- Windows: cmd.exe syntax (dir, ipconfig, tasklist, type, etc.)
- Linux/Mac: bash syntax (ls, ps, cat, df, etc.)
- Output capped at 4KB
- Use ONLY for real system commands, NOT for text editing

## Safety Rules for run_command
Never use run_command for:
- Text substitution ("把/改为\" = conversational, not a command)
- File renaming as a substitute for write_file
- Anything the user described as editing text
