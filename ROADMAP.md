# Code Intelligence Roadmap

## Phase 1: Basic Code Indexing (Week 1-2)

### Goal
Add code understanding capabilities without breaking existing functionality.

### Architecture
```
New modules (zero modification to existing code):
├── include/indexer/
│   ├── code_index.hpp       # Main index interface
│   └── simple_indexer.hpp   # Grep-based implementation (no dependencies)
└── src/indexer/
    ├── code_index.cpp
    └── simple_indexer.cpp

New tools (registered in tool_registry):
├── find_definition          # Find where symbol is defined
├── find_references          # Find all usages
└── explain_code             # Explain code snippet with context
```

### Implementation Strategy
1. **No external dependencies** (Phase 1)
   - Use existing `run_command` tool to call `grep`/`rg`
   - Parse output to build simple symbol table
   - Store in memory (no database yet)

2. **Incremental enhancement**
   - Phase 1: Grep-based (works immediately)
   - Phase 2: Add Tree-sitter for AST parsing
   - Phase 3: Add SQLite for persistent index

3. **Safety guarantees**
   - All new code in separate `indexer/` namespace
   - Existing tools unchanged
   - New tools opt-in (only used when explicitly called)

## Phase 2: Auto Bug Fixing (Week 3-4)

### New Tools
- `analyze_crash`      # Parse stack trace → locate bug
- `run_tests`          # Detect test framework → run tests
- `create_fix_branch`  # Git workflow automation

## Phase 3: Tech Debt Scanner (Week 5-6)

### New Tools
- `scan_complexity`    # Find complex functions
- `find_duplicates`    # Detect code duplication
- `generate_report`    # Weekly tech debt report

## Success Metrics

Phase 1 MVP:
- [ ] Can answer "where is function X defined?"
- [ ] Can answer "who calls function X?"
- [ ] Zero impact on existing functionality
- [ ] All existing tests still pass
