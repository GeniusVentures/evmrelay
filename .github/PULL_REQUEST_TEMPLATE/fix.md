## Fix PR

**Fixes #NNN**

### What Was Broken
<!-- Describe the bug: symptoms, reproduction, impact -->

### What the Fix Does
<!-- Describe the root cause and how the fix addresses it -->

### Root Cause
<!-- Detailed root cause analysis -->

### Verification Method
<!-- How was this fix verified? -->
- [ ] Bug reproduced before fix
- [ ] Regression test added
- [ ] Existing tests still pass
- [ ] Manual reproduction steps no longer trigger the bug

### Platforms Tested
- [ ] macOS
- [ ] Linux (x86_64)
- [ ] Windows
- [ ] Android
- [ ] iOS

### Runtimes Tested
- [ ] Debug
- [ ] Release
- [ ] RelWithDebInfo

### Regression Test
<!-- If no regression test was added, explain why -->
- [ ] Regression test added: `test/...`
- [ ] No regression test because: ...

### Scope Confirmation
- [ ] This PR contains only the fix described above
- [ ] No unrelated refactors, formatting changes, or dependency bumps
- [ ] All changes conform to CLAUDE.md coding standards
- [ ] No `std::this_thread::sleep_for` in tests

### Breaking Changes
None (by definition — fixes should not break behavior).
