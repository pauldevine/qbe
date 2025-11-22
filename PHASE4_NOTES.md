# Phase 4 Implementation Notes

## Feature 2: Compound Literals - DEFERRED

**Status:** ❌ Deferred due to implementation complexity
**Reason:** MiniC's architecture makes runtime temporary management complex

### Challenges Encountered

1. **Code Generation During Parsing:**
   - MiniC generates QBE IL directly during parsing
   - Compound literals need runtime temporaries
   - No clean way to create temporaries that survive the parse phase

2. **Node System Limitations:**
   - Limited node types ('N', 'V', 'S', 'F')
   - No existing pattern for runtime-allocated temporary objects
   - Would require significant refactoring

3. **Lifetime Management:**
   - Compound literals have block scope lifetime
   - Requires tracking and cleanup
   - Complex interaction with existing variable system

### Alternative: Focus on Higher Value Features

Given Phase 4's goal of 60% C11 compliance with 6 features, it's more valuable to implement features that:
- Are easier to integrate with MiniC's architecture
- Provide more immediate benefit for DOS development
- Don't require major refactoring

### Recommendation

**Skip compound literals for now, implement instead:**
1. ✅ _Static_assert (DONE)
2. ⏭️ **Designated initializers** (easier, more useful)
3. Anonymous struct/union
4. _Generic
5. _Alignof/_Alignas
6. Additional feature TBD

This gives us 5-6 working features = 60%+ C11 compliance.

### Future Work

Compound literals could be implemented in a future phase with:
- Refactored temporary management system
- Better separation of parsing and code generation
- Enhanced node system to track complex constructs

---

## Feature 2 (Revised): Designated Initializers

**Moving forward with this feature instead**
**Expected completion:** 2-3 days
**Higher value for DOS development**
