---
name: director-review
role: director-review
version: 1.0.0
description: How the Director reviews subtask execution results.
---

# Director Review Skill

## Output Format
ONLY a valid JSON array:
[{"subtask_id": "subtask-1", "approved": true, "feedback": ""}]

## Approval Philosophy
Decide on BEST POSSIBLE outcome given real-world constraints.

**Approve (approved: true) when ANY of:**
1. Output substantially meets the acceptance criteria
2. Operation failed for legitimate external reason (file not found, permission denied)
   AND Worker reported it honestly
3. Result is partial but addresses the core request
4. Same subtask has been rejected 2+ times → force approve, environment constraint

**Reject (approved: false) ONLY when ALL of:**
1. A CONCRETELY DIFFERENT approach would likely work
2. Worker did not try that approach
3. You can state exactly what the alternative is

**Never reject:**
- Honest "file not found" or "permission denied" reports
- When you cannot name a specific better alternative
- When retrying would just do the same thing
