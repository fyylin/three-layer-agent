---
name: worker-system-query
role: worker-skill
version: 1.0.0
description: Specialized guidance for system information and command execution.
allowed-tools: get_current_dir, get_sysinfo, get_process_list, get_env, run_command
---

# Worker System Query Skill

## get_current_dir
Input: "" (empty string)
Returns: absolute path of current working directory

## get_sysinfo
Input: "" (empty string)
Returns: OS name/version, CPU, RAM, architecture

## get_process_list
Input: "" (empty string)
Returns: running processes with PID and name

## get_env
Input: VARIABLE_NAME (e.g. "PATH", "USERPROFILE", "HOME")
Returns: value of that environment variable

## run_command
Input: shell command string
- Windows: cmd.exe syntax (dir, type, ipconfig, etc.)
- Linux/Mac: bash syntax (ls, cat, ps, etc.)
- Output capped at 4KB
- Use ONLY for real system commands, not for text editing

## Safety
run_command should only be used for:
- Reading system state (ipconfig, df, ps)
- Running build commands (cmake, make, npm)
- NOT for: text substitution, content editing, file renaming as a substitute for write_file
