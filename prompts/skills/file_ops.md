---
name: file-ops
role: cross-agent-skill
version: 1.0.0
description: File system operations: read, write, search, list. Load when working with files.
---

# File Operations Skill

## When to use this skill
Load this skill whenever a task involves: reading files, writing files, finding files,
listing directories, checking if files exist, or managing file content.

## read_file
Input: exact file path (relative to CWD or absolute)
- Returns file content up to 64KB
- Large files truncated with "[OUTPUT TRUNCATED]" marker

## write_file
Input: "path\ncontent" — first line is path, rest is the full file content
- Creates parent directories automatically
- Overwrites existing content (ask user first if file is important)

## list_dir
Input: path ("." = current dir, ".." = parent, or absolute path)
- Returns newline-separated list of entries

## find_files
Input: "directory\npattern" — two lines
Examples:
  ".\n*.md"           find all .md in current dir
  ".\n**\n*.txt"    find all .txt recursively
  "src\n*.cpp"       find .cpp files in src/

## stat_file
Input: file path → returns size, modification time, type (file/dir)

## delete_file
Input: "CONFIRMED:path" — CONFIRMED: prefix is mandatory

## Path Discovery
If path is unknown:
  step 1: get_current_dir → "" → returns absolute CWD
  step 2: use that path for subsequent operations

## Output Location
By default, write agent-created files to: workspace/current/files/
(See WORKSPACE.md for workspace layout)
