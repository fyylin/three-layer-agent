---
name: director-classify
role: director-classify
version: 1.0.0
description: Classify the user's latest request into the routing level the Director should use.
allowed-tools: none
---

# Director Classify Skill

## Task
Classify the CURRENT user request into exactly one routing label:
- `L0`: Pure conversation only. Greeting, capability question, knowledge explanation, or text-edit advice. No tool execution, file access, directory access, command execution, or system inspection is required.
- `L1`: Exactly one obvious tool action. Simple list/read/current-dir/system-info/process-list style request.
- `L2`: One self-contained work item that still needs the Manager/Worker pipeline, but not parallel branches.
- `L3`: Multiple actionable steps or likely tool work. Use this as the default for file operations, system operations, searches, code execution, or anything uncertain.
- `L4`: Clearly complex multi-stage work with dependencies, such as refactor, migrate, build a complete project, architect a system, or long chained workflows.

## Hard Safety Rules
- If the request asks to read, list, search, check, open, inspect, run, execute, create, write, modify, delete, move, copy, install, build, test, download, or operate on any real file/system state, it is NOT `L0`.
- If the request mentions a concrete file path, directory path, command, process, environment variable, URL to fetch, or system state to inspect, it is NOT `L0`.
- Text-edit intent without real execution is still `L0`.
  Examples: "replace / with \\", "explain this code", "what can you do"
- When uncertain between labels, choose the more operational route, usually `L3`.
- Avoid `L0` unless you are confident the Director can answer directly without delegating anything.

## Examples
- `hello` -> `L0`
- `what is a vector database` -> `L0`
- `replace / with \\ in this command` -> `L0`
- `show current directory` -> `L1`
- `read README.md` -> `L1`
- `create hello.py in D:\temp` -> `L2`
- `find the newest log file and read the error lines` -> `L3`
- `list project files and check which config is wrong` -> `L3`
- `refactor this module and add tests` -> `L4`

## Output Format
Return ONLY one label: `L0`, `L1`, `L2`, `L3`, or `L4`.
No JSON. No prose. No explanation.
