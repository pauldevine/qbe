# MiniC C Standard Compliance Tracker

This directory contains tests and tracking for C standard compliance (C89, C99, C11, C17).

## Current Status

### C89/ANSI C (1989) - Target: 100%

| Feature Category | Status | Passing | Total | Notes |
|------------------|--------|---------|-------|-------|
| **Types** | 🟡 Partial | 6/8 | 75% | Missing: `short`, `float`/`double` |
| **Storage Classes** | 🔴 Missing | 0/4 | 0% | Need: `static`, `extern`, `auto`, `register` |
| **Type Qualifiers** | 🔴 Missing | 0/2 | 0% | Need: `const`, `volatile` |
| **Operators** | 🟢 Complete | 30/30 | 100% | All operators implemented |
| **Control Flow** | 🟡 Partial | 7/8 | 87.5% | Missing: `goto`/labels |
| **Functions** | 🟡 Partial | 4/5 | 80% | Missing: function pointers |
| **Comments** | 🔴 Wrong | 1/2 | 50% | Have `#`, need `/* */` |
| **Preprocessor** | 🔴 Missing | 0/1 | 0% | Complete preprocessor needed |
| **Pointers** | 🟢 Complete | 3/3 | 100% | Full pointer support |
| **Arrays** | 🟢 Complete | 3/3 | 100% | With initialization |
| **Structures** | 🟢 Complete | 3/3 | 100% | Struct, union, enum |
| **Overall C89** | 🟡 Partial | ~60% | | Core features complete |

### C99 (1999) - Target: 0% → 100%

| Feature | Status | Priority | Notes |
|---------|--------|----------|-------|
| `//` comments | 🔴 TODO | High | Easy to add |
| `long long` type | 🔴 TODO | High | QBE supports `Kl` |
| `_Bool` type | 🔴 TODO | High | Map to `int` |
| Designated initializers | 🔴 TODO | High | `{.x = 10}` |
| Compound literals | 🔴 TODO | Medium | `(struct){10, 20}` |
| VLAs | 🔴 TODO | Medium | Variable-length arrays |
| `inline` functions | 🔴 TODO | Medium | |
| `restrict` qualifier | 🔴 TODO | Low | Optimization hint |
| Mixed declarations/code | 🔴 TODO | High | Declare anywhere |
| `for (int i = ...)` | 🔴 TODO | High | Loop variable declaration |
| Hex float literals | 🔴 TODO | Low | `0x1.8p3` |
| `<stdint.h>` types | 🔴 TODO | High | `int32_t`, etc. |
| Flexible array members | 🔴 TODO | Low | `int arr[]` in struct |

### C11 (2011) - Target: Future

- `_Generic`
- `_Static_assert`
- `_Alignas`, `_Alignof`
- Anonymous structs/unions
- Atomics
- Threads
- Unicode support

### C17 (2018) - Target: Future

- Bug fixes and clarifications to C11
- No major new features

## Testing Strategy

### Level 1: Unit Tests (Current)
✅ Basic feature tests in `../test/test_*.c`
- 8 tests, all passing
- Coverage: Current implemented features

### Level 2: Compliance Tests (This Directory)
🚧 Feature-specific conformance tests
- One test per C89/C99/C11 feature
- PASS/FAIL tracking
- Detailed error reporting

### Level 3: Torture Tests (Future)
📋 GCC/LLVM test suites
- Thousands of edge case tests
- Random program generators
- Real-world program compilation

## Running Tests

```bash
# Run all compliance tests
./run_compliance_tests.sh

# Run specific standard tests
./run_compliance_tests.sh c89
./run_compliance_tests.sh c99

# Generate compliance report
./generate_report.sh
```

## Adding New Tests

1. Create test file in appropriate directory: `c89-compliance/`, `c99-compliance/`, etc.
2. Name format: `feature_name.c` (e.g., `short_type.c`, `goto_labels.c`)
3. Test should print "PASS" or "FAIL"
4. Update this README with test count

## C89 Implementation Priority

**Phase 1: High Priority (Next 2 weeks)**
1. ✅ Character literals - DONE
2. ✅ Hex/octal literals - DONE
3. ✅ Compound assignments - DONE
4. ✅ Ternary operator - DONE
5. 🔴 `short` type
6. 🔴 `const` qualifier
7. 🔴 C-style `/* */` comments
8. 🔴 Comma operator
9. 🔴 `goto` and labels

**Phase 2: Medium Priority (Next 4 weeks)**
10. 🔴 `static` storage class
11. 🔴 `extern` storage class
12. 🔴 `volatile` qualifier
13. 🔴 Function pointers
14. 🔴 `auto`, `register` (mostly no-ops)

**Phase 3: Lower Priority (Future)**
15. 🔴 `float` and `double` (requires QBE FP support)
16. 🔴 Preprocessor (large undertaking)

## References

- [C89 Standard (ANSI X3.159-1989)](https://port70.net/~nsz/c/c89/c89-draft.html)
- [C99 Standard (ISO/IEC 9899:1999)](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf)
- [C11 Standard (ISO/IEC 9899:2011)](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)
- [C17 Standard (ISO/IEC 9899:2018)](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2176.pdf)

## Contributing

When implementing a new feature:
1. Add test to appropriate compliance directory
2. Update this README with status
3. Run full test suite
4. Update compliance percentage
