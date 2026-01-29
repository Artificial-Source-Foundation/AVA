# Test Optimization Plan

> Reduce test count from 870 individual tests to ~400 optimal tests

## Current State

| Metric | Value |
|--------|-------|
| Total `it()` blocks | 870 |
| Total test files | 36 |
| Vitest reported tests | ~1137 (includes parameterized) |
| Largest file | `cli/commands.test.ts` (766 lines, 36 tests) |

## Target State

| Metric | Target |
|--------|--------|
| Total `it()` blocks | ~400 |
| Reduction | ~54% |
| Coverage | Maintain 100% |
| Execution time | -40% estimated |

---

## Optimization Patterns

### Pattern 1: Consolidate Existence Checks

**Before (25 tests in hints.test.ts):**
```typescript
it('has background task hints', () => {
  expect(hints.noTasks).toBeDefined()
  expect(hints.noRunningTasks).toBeDefined()
  expect(hints.tasksAllComplete).toBeDefined()
})

it('has mission hints', () => {
  expect(hints.noMission).toBeDefined()
  expect(hints.missionComplete).toBeDefined()
  // ... 5 more
})

// ... 5 more describe blocks
```

**After (1 test):**
```typescript
it('should have all required hint properties', () => {
  const requiredHints = [
    'noTasks', 'noRunningTasks', 'tasksAllComplete',
    'noMission', 'missionComplete', 'missionBlocked',
    // ... all others
  ]

  for (const hint of requiredHints) {
    expect(hints[hint]).toBeDefined()
  }
})
```

**Savings: ~25 tests → 1 test**

---

### Pattern 2: Use `it.each()` for Data-Driven Tests

**Before (8 tests in categories.test.ts):**
```typescript
it('should detect planning category', () => {
  const matches = detectCategory('Design the architecture...')
  expect(matches[0].category).toBe('planning')
})

it('should detect coding category', () => {
  const matches = detectCategory('Implement a function...')
  expect(matches[0].category).toBe('coding')
})

// ... 6 more identical patterns
```

**After (1 parameterized test):**
```typescript
it.each([
  ['Design the architecture for a microservices system', 'planning'],
  ['Implement a function to parse JSON data', 'coding'],
  ['Write unit tests for the user service', 'testing'],
  ['Update the README with installation instructions', 'documentation'],
  ['Research best practices for React state management', 'research'],
  ['Create a responsive button component with Tailwind', 'ui'],
  ['Refactor the authentication module', 'refactoring'],
  ['Fix the login bug that causes crashes', 'bugfix'],
])('should detect %s category from: %s', (input, expected) => {
  const matches = detectCategory(input)
  expect(matches[0].category).toBe(expected)
})
```

**Savings: 8 tests → 1 parameterized test**

---

### Pattern 3: Consolidate Schema Validation

**Before (5 tests in model-fallback.test.ts):**
```typescript
it('should contain all major models', () => {
  expect(MODEL_REGISTRY['anthropic/claude-opus-4-5']).toBeDefined()
  expect(MODEL_REGISTRY['anthropic/claude-sonnet-4-5']).toBeDefined()
  // ... more
})

it('should have valid tier assignments', () => { /* ... */ })
it('should have valid cost values', () => { /* ... */ })
it('should have valid context windows', () => { /* ... */ })
it('should have capabilities arrays', () => { /* ... */ })
```

**After (1 comprehensive test):**
```typescript
it('should have valid model registry schema', () => {
  const requiredModels = [
    'anthropic/claude-opus-4-5',
    'anthropic/claude-sonnet-4-5',
    'anthropic/claude-haiku-4',
    'openai/gpt-4o',
    'google/gemini-2.0-pro',
  ]

  for (const modelId of requiredModels) {
    const model = MODEL_REGISTRY[modelId]
    expect(model).toBeDefined()
    expect(model.tier).toMatch(/^(premium|standard|economy)$/)
    expect(model.costPer1M).toBeGreaterThan(0)
    expect(model.contextWindow).toBeGreaterThan(0)
    expect(model.capabilities.length).toBeGreaterThan(0)
  }
})
```

**Savings: 5 tests → 1 test**

---

### Pattern 4: Remove TypeScript-Covered Checks

**Before:**
```typescript
it('should have capabilities arrays', () => {
  for (const model of Object.values(MODEL_REGISTRY)) {
    expect(Array.isArray(model.capabilities)).toBe(true)
  }
})
```

**After: Remove entirely** - TypeScript already enforces this via type definitions.

**Savings: Delete redundant type-checking tests**

---

### Pattern 5: Merge forEach into it.each

**Before (11 + 9 = 20 tests in operator-guard.test.ts):**
```typescript
const operatorAgents = ['operator', 'Operator', 'operator_complex', ...]
operatorAgents.forEach((agent) => {
  it(`should identify "${agent}" as operator`, () => {
    expect(isOperatorAgent(agent)).toBe(true)
  })
})

const nonOperatorAgents = ['commander', 'validator', 'scout', ...]
nonOperatorAgents.forEach((agent) => {
  it(`should NOT identify "${agent}" as operator`, () => {
    expect(isOperatorAgent(agent)).toBe(false)
  })
})
```

**After (2 parameterized tests):**
```typescript
it.each([
  ['operator', true],
  ['Operator', true],
  ['operator_complex', true],
  ['commander', false],
  ['validator', false],
  ['scout', false],
])('isOperatorAgent("%s") should return %s', (agent, expected) => {
  expect(isOperatorAgent(agent)).toBe(expected)
})
```

**Savings: 20 tests → 1 test (Vitest still runs all cases)**

---

## File-by-File Optimization

### High Impact (>10 tests saved each)

| File | Current | Target | Savings |
|------|---------|--------|---------|
| `knowledge/semantic.test.ts` | 47 | 20 | 27 |
| `templates/templates.test.ts` | 43 | 15 | 28 |
| `lib/model-fallback.test.ts` | 43 | 18 | 25 |
| `routing/categories.test.ts` | 41 | 15 | 26 |
| `lib/hints.test.ts` | 38 | 10 | 28 |
| `locks/store.test.ts` | 36 | 15 | 21 |
| `hooks/edit-error-recovery.test.ts` | 36 | 12 | 24 |
| `agents/support.test.ts` | 33 | 12 | 21 |
| **Subtotal** | 317 | 117 | **200** |

### Medium Impact (5-10 tests saved each)

| File | Current | Target | Savings |
|------|---------|--------|---------|
| `skills/injection.test.ts` | 32 | 15 | 17 |
| `guardrails/commander-discipline.test.ts` | 30 | 15 | 15 |
| `lib/notifications.test.ts` | 29 | 15 | 14 |
| `guardrails/three-strike.test.ts` | 28 | 15 | 13 |
| `metrics/collector.test.ts` | 26 | 12 | 14 |
| `lib/errors.test.ts` | 26 | 12 | 14 |
| **Subtotal** | 171 | 84 | **87** |

### Low Impact (<5 tests saved each)

Keep as-is - not worth the refactoring effort.

---

## Implementation Order

### Sprint 1: Quick Wins (2 hours)

1. **hints.test.ts** - Consolidate existence checks
2. **categories.test.ts** - Convert to it.each
3. **model-fallback.test.ts** - Merge schema validation

**Expected savings: ~80 tests**

### Sprint 2: Major Consolidation (3 hours)

4. **semantic.test.ts** - Parameterize vector operations
5. **templates.test.ts** - Merge template tests
6. **locks/store.test.ts** - Consolidate store operations

**Expected savings: ~75 tests**

### Sprint 3: Cleanup (2 hours)

7. **operator-guard.test.ts** - Convert forEach to it.each
8. **edit-error-recovery.test.ts** - Parameterize error patterns
9. **support.test.ts** - Consolidate agent tests

**Expected savings: ~65 tests**

---

## Verification

After each sprint:

```bash
npm test                           # All tests pass
npm run test -- --coverage         # Coverage ≥ current
npm run test -- --reporter=json    # Count reduction verified
```

---

## Principles

1. **Maintain coverage** - Don't delete tests, consolidate them
2. **Keep readability** - `it.each` descriptions should be clear
3. **Preserve error messages** - Individual failures should still be identifiable
4. **Validate refactoring** - Run tests before/after each file change

---

## Expected Results

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| `it()` count | 870 | ~400 | -54% |
| Test files | 36 | 36 | 0% |
| Coverage | 100% | 100% | 0% |
| Execution time | ~15s | ~9s | -40% |
| Maintainability | Medium | High | +50% |
