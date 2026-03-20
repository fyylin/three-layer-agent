# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| v19.x   | ✓ |
| < v18   | ✗ |

## API Key Safety

- Never commit `config/agent_config.json` if it contains your API key
- Use the `ANTHROPIC_API_KEY` environment variable instead
- The file is in `.gitignore` by default

## Reporting a Vulnerability

Please **do not** open a public GitHub issue for security vulnerabilities.

Instead, open a private security advisory on GitHub, or contact the maintainers directly.

We will respond within 72 hours and aim to release a patch within 14 days.

## Prompt Injection

This system processes untrusted file content (via `read_file`, `run_command`, etc.).
The `base.md` safety rules provide some protection, but no Agent system is immune to
indirect prompt injection. Do not grant the agent access to untrusted file systems
with elevated privileges.
