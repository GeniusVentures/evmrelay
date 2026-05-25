# Contributing to evmrelay

## Issue-First Workflow

All code changes must start with an issue. This ensures traceability and approval gating:

| Change Type | Issue Template | Required Approval Label |
|---|---|---|
| New feature | Feature Request | `approved-feature` |
| Enhancement | Enhancement | `approved-enhancement` |
| Bug fix | Bug Report | `confirmed-bug` |
| Maintenance | Chore | `needs-triage` → reviewed |

**Gates:** PRs opened without a linked approved issue will be rejected. Every PR body must include `Closes #NNN` or `Fixes #NNN` referencing an approved issue.

## Submission Process

1. **Open an issue** using the appropriate template (`.github/ISSUE_TEMPLATE/`)
2. **Wait for approval** — the issue must receive the required approval label before code is written
3. **Create a branch** named `feature/issue-NNN-description` or `fix/issue-NNN-description`
4. **Open a PR** using the corresponding PR template (`.github/PULL_REQUEST_TEMPLATE/`)
5. **Pass CI** on all platforms (OSX, Linux x86_64, Linux aarch64, Windows, Android, iOS)
6. **Get review approval** from a maintainer
7. **Merge** — squash merge preferred for clean history

## Coding Standards

This project follows the **GNUS.AI C++ Coding Standards**. Key points:

- **C++17 only** — no C++20 features (no designated initializers, no `co_await`)
- **Allman/Ullman braces** — braces on their own line
- **4-space indentation**, 120 column max line length
- **PascalCase** for classes/methods, **camelCase** for variables
- **`constexpr` / `inline constexpr` named `kCamelCase`** for constants
- **Doxygen headers** on all public interfaces and functions
- **`outcome::result<T>` for error propagation** — no exceptions in hot paths
- **`noexcept` by default** — only add exception specifications when explicitly required
- **All variables must be initialized**
- **Always use braces** on `if`/`while`/`for`/`switch` even for single statements
- **`#ifndef`/`#define`/`#endif` header guards** — no `#pragma once`

See `AgentDocs/CLAUDE.md` for the full coding standards document.

## Build & Test

### Build
```bash
cd build/<Platform>/<BuildType>   # e.g. build/OSX/Debug
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=<BuildType>
ninja
```

Platforms: `OSX`, `Linux`, `Windows`, `Android`, `iOS`
Build types: `Debug`, `Release`, `RelWithDebInfo`

### Run Tests
```bash
ctest --output-on-failure
```

### Testing Rules
- Use Google Test with the project's wait-condition templates
- **NEVER** use `std::this_thread::sleep_for` in tests
- Target ≥80% coverage on new code
- Include failure/negative tests for all new features

## Design Principles

- **Data-driven architecture** — never hard-code chain names, network IDs, ports, or RPC URLs in source
- **Program to interfaces** — separate parsing, validation, transport, persistence, and orchestration
- **Favor composition over inheritance** — encapsulate what varies behind explicit configuration
- **Minimal changes** — solve the requested issue with the smallest possible number of changed lines
- **No god classes** — keep modules focused and replaceable

## Review Checklist

All PRs are reviewed for:
- Issue linked and approved
- Coding standards compliance
- Test coverage (≥80%, happy + unhappy paths)
- No sleep_for in tests
- No exceptions in hot paths
- Data-driven: no hardcoded chain-specific values
- One concern per PR
- CI passing on all platforms

## Questions?

File an issue or contact the maintainers.
