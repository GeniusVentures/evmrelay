## Feature PR

**Closes #NNN**

### Feature Summary
<!-- Brief description of the feature being delivered -->

### New Files
| File | Purpose |
|------|---------|
| `path/to/file.hpp` | Description |

### Modified Files
| File | Change Summary |
|------|----------------|
| `path/to/file.cpp` | Description |

### Implementation Notes
<!-- Key design decisions, trade-offs, patterns used -->

### Spec Compliance Checklist
<!-- Acceptance criteria from the linked issue — check off as completed -->
- [ ] Criterion 1
- [ ] Criterion 2

### Test Coverage
<!-- Describe tests added: unit, integration, fuzz, live -->
- [ ] Unit tests pass
- [ ] Integration tests pass
- [ ] Coverage ≥ 80% on new code

### Platforms Tested
<!-- Check all platforms where this was built and tested -->
- [ ] macOS
- [ ] Linux (x86_64)
- [ ] Windows
- [ ] Android
- [ ] iOS

### Runtimes Tested
<!-- Build types tested -->
- [ ] Debug
- [ ] Release
- [ ] RelWithDebInfo

### Scope Confirmation
- [ ] This PR contains only the feature described above
- [ ] No unrelated refactors, formatting changes, or dependency bumps
- [ ] All changes conform to CLAUDE.md coding standards (Allman braces, const-correct, Doxygen headers)
- [ ] No exceptions in hot paths (outcome::result used for error propagation)
- [ ] No `std::this_thread::sleep_for` in tests

### Breaking Changes
<!-- Describe any breaking changes or mark "None" -->
None.
