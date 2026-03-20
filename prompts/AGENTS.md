---
name: agents-context
role: project-context
version: 1.0.0
description: Project-level context injected into all agents. Copy to your working directory and customize.
---

# Project Context

<!-- CUSTOMIZE THIS FILE for your project. Place it in your working directory. -->

## About This Project
Three-Layer Agent System — autonomous task execution with file, system, and command tools.

## Working Directory Convention
- Persistent files: workspace/current/
- Per-run logs: workspace/run-N/
- Prompts: prompts/ (customize agent behavior here)

## Tool Availability
list_dir, read_file, write_file, stat_file, find_files, delete_file,
get_current_dir, get_sysinfo, get_process_list, get_env, run_command,
echo, create_tool, list_tools

## Boundaries for Automated Actions
- Always do: write outputs to workspace/current/
- Ask first: before overwriting files that were not just created
- Never do: execute rm -rf or equivalent, read .env or secrets files

## Notes for Customization
Replace the sections above with your project-specific context:
- Project name and purpose
- File structure (which directories contain what)
- Specific tools or commands this project uses
- Any domain-specific terminology agents should know
