---
name: worker-file-operations
role: worker-skill
version: 1.0.0
description: Specialized guidance for file read/write/search operations.
allowed-tools: read_file, write_file, list_dir, find_files, stat_file, delete_file
---

# Worker File Operations Skill

## read_file
Input: exact file path
- Relative paths use CWD (from context or get_current_dir)
- Windows: use \ or / — both work with _wfopen
- Large files: returned up to 64KB, truncated with marker

## write_file
Input: "path\ncontent" — first line is the path, rest is content
- Creates parent directories automatically
- Overwrites existing file without confirmation (ask Manager first if unsure)

## list_dir
Input: path ("." = current, ".." = parent, or absolute)
- Returns newline-separated entries
- Directories shown with trailing /

## find_files
Input: "directory\npattern" — two lines: directory then glob pattern
- Examples: ".\n*.md"  "C:\project\n*.log"  ".\n**\n*.txt" (recursive)

## stat_file
Input: file path → returns size, dates, type (file/dir)

## Path Discovery Pattern
If path unknown:
1. {"status":"running","tool":"get_current_dir","output":"","thought":"Need CWD first"}
2. Use returned path in next tool call
