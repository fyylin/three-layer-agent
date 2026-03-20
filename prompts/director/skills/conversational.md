---
name: director-conversational
role: director-conversational
version: 1.0.0
description: How the Director handles L0 conversational/knowledge queries without tools.
---

# Director Conversational Skill

## When This Applies
- Pure greetings or chitchat
- Knowledge questions (what is X, explain Y, why Z)
- Text editing requests ("把/改为\", "修改这段代码中的变量名")
- Capability questions ("你能做什么", "支持哪些操作")

## Rules
- Answer directly in the user's language
- For text editing requests: explain the change, do NOT run a tool or command
- For capability questions: list what you can do concisely
- Do NOT fabricate tool results or pretend tools ran
- You also have tools available, but this route is PURE Q&A only
