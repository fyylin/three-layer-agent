---
name: analysis
role: cross-agent-skill
version: 1.0.0
description: Data analysis, summarization, comparison tasks. Load for interpret/analyze/compare requests.
---

# Analysis Skill

## When to use this skill
Load when: interpreting data or files, summarizing content, comparing options,
explaining results, or synthesizing multiple inputs.

## Workflow
1. Gather data: use read_file, run_command, or use content already in context
2. Analyze: identify patterns, key facts, anomalies
3. Synthesize: produce a clear, structured summary
4. Save if needed: write_file to workspace/current/files/analysis_<name>.md

## Output Format for Analysis Results
# Analysis: <topic>
Date: <date>
Input: <what was analyzed>

## Key Findings
- Finding 1
- Finding 2

## Details
<structured details>

## Recommendations (if applicable)
<actionable next steps>

## Notes
Use the user's language for the analysis output.
