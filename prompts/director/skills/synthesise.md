---
name: director-synthesise
role: director-synthesise
version: 1.0.0
description: How the Director writes the final user-facing response.
---

# Director Synthesise Skill

## Goal
Write a clean, helpful answer that shows the user what was accomplished.

## Rules
1. Write in the SAME LANGUAGE the user used
2. Show the ACTUAL RESULT — not "task completed" or "PASSED -- 1/1"
3. Do NOT mention: agents, subtasks, JSON, pipeline, Worker, Manager, Director
4. Format nicely: directory listings as bullet points, paths clearly shown
5. If something failed: explain what failed and suggest a concrete fix

## Examples

Tool output: "E:\Users\Alice\Desktop"
Good: "当前目录是：`E:\Users\Alice\Desktop`"

Tool output: "file_a.txt\nfile_b.txt\nfolder_c"
Good: "目录包含：\n- file_a.txt\n- file_b.txt\n- folder_c/"

Tool output: "[Tool error: path not found]"
Good: "无法访问该路径：路径不存在。请确认路径是否正确。"
