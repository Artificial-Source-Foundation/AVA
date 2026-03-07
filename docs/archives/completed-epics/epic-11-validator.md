# Epic 11: Validator

> QA verification gate

---

## Goal

Add automated validation to ensure agent outputs meet quality standards before presenting to user.

---

## Prerequisites

- Epic 8 (Agent) - Single agent loop

---

## Sprints

| # | Sprint | Tasks | Est. Lines |
|---|--------|-------|------------|
| 11.1 | Validation Framework | Define validation rules and runners | ~250 |
| 11.2 | Code Validators | Syntax, types, lint checks | ~300 |
| 11.3 | Test Validators | Run tests, check coverage | ~250 |
| 11.4 | Self-Review | LLM reviews its own output | ~200 |

**Total:** ~1000 lines

---

## Validation Types

| Validator | Checks | Tools Used |
|-----------|--------|------------|
| Syntax | Code parses without errors | Language parser |
| TypeScript | Type errors | `tsc --noEmit` |
| Lint | Style violations | ESLint, Biome |
| Tests | Test pass/fail | Jest, Vitest |
| Build | Compilation succeeds | Build command |
| Self-Review | LLM checks own work | LLM call |

---

## Key Features

### Validation Pipeline
```typescript
interface ValidationResult {
  validator: string
  passed: boolean
  errors: string[]
  warnings: string[]
}

interface Validator {
  name: string
  run(files: string[], ctx: ValidationContext): Promise<ValidationResult>
}

async function validateChanges(
  files: string[],
  validators: Validator[]
): Promise<ValidationResult[]> {
  const results: ValidationResult[] = []

  for (const validator of validators) {
    const result = await validator.run(files, ctx)
    results.push(result)

    // Stop on critical failure
    if (!result.passed && validator.name === 'syntax') {
      break
    }
  }

  return results
}
```

### Self-Review Validator
```typescript
const selfReviewValidator: Validator = {
  name: 'self-review',
  async run(files, ctx) {
    const diffs = await getDiffs(files)

    const review = await llm.generate({
      system: 'Review the following code changes for issues...',
      user: diffs,
    })

    const issues = parseReviewIssues(review)

    return {
      validator: 'self-review',
      passed: issues.critical.length === 0,
      errors: issues.critical,
      warnings: issues.minor,
    }
  }
}
```

---

## Acceptance Criteria

- [ ] All code changes pass syntax validation
- [ ] TypeScript errors block completion
- [ ] Lint warnings reported but don't block
- [ ] Tests run automatically after code changes
- [ ] Self-review catches obvious issues
