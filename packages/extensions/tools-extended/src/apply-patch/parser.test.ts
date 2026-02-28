import { describe, expect, it } from 'vitest'
import { parsePatch, validatePatch } from './parser.js'

describe('parsePatch', () => {
  it('parses add file operation', () => {
    const patch = `*** Begin Patch
*** Add File: src/new.ts
+export function hello() {
+  return 'hello'
+}
*** End Patch`

    const result = parsePatch(patch)
    expect(result.errors).toHaveLength(0)
    expect(result.files).toHaveLength(1)
    expect(result.files[0].operation).toBe('add')
    expect(result.files[0].path).toBe('src/new.ts')
    expect(result.files[0].chunks[0].lines).toHaveLength(3)
    expect(result.files[0].chunks[0].lines[0].type).toBe('add')
  })

  it('parses update file operation', () => {
    const patch = `*** Begin Patch
*** Update File: src/index.ts
@@ export function main @@
-  return 'old'
+  return 'new'
*** End Patch`

    const result = parsePatch(patch)
    expect(result.errors).toHaveLength(0)
    expect(result.files).toHaveLength(1)
    expect(result.files[0].operation).toBe('update')
    expect(result.files[0].chunks).toHaveLength(1)
    expect(result.files[0].chunks[0].contextLine).toBe('export function main')
  })

  it('parses delete file operation', () => {
    const patch = `*** Begin Patch
*** Delete File: src/old.ts
*** End Patch`

    const result = parsePatch(patch)
    expect(result.errors).toHaveLength(0)
    expect(result.files).toHaveLength(1)
    expect(result.files[0].operation).toBe('delete')
    expect(result.files[0].path).toBe('src/old.ts')
  })

  it('parses move file operation', () => {
    const patch = `*** Begin Patch
*** Move File: old/path.ts -> new/path.ts
*** End Patch`

    const result = parsePatch(patch)
    expect(result.errors).toHaveLength(0)
    expect(result.files).toHaveLength(1)
    expect(result.files[0].operation).toBe('move')
    expect(result.files[0].path).toBe('old/path.ts')
    expect(result.files[0].newPath).toBe('new/path.ts')
  })

  it('parses multiple operations', () => {
    const patch = `*** Begin Patch
*** Add File: src/a.ts
+content a
*** Delete File: src/b.ts
*** Update File: src/c.ts
@@ line @@
-old
+new
*** End Patch`

    const result = parsePatch(patch)
    expect(result.errors).toHaveLength(0)
    expect(result.files).toHaveLength(3)
  })

  it('handles unclosed patch', () => {
    const patch = `*** Begin Patch
*** Add File: src/a.ts
+content`

    const result = parsePatch(patch)
    expect(result.errors).toHaveLength(1)
    expect(result.errors[0]).toContain('not properly closed')
    // Still parses the files
    expect(result.files).toHaveLength(1)
  })

  it('handles context lines', () => {
    const patch = `*** Begin Patch
*** Update File: src/index.ts
@@ function main @@
 context before
-old line
+new line
 context after
*** End Patch`

    const result = parsePatch(patch)
    const lines = result.files[0].chunks[0].lines
    expect(lines[0].type).toBe('context')
    expect(lines[1].type).toBe('delete')
    expect(lines[2].type).toBe('add')
    expect(lines[3].type).toBe('context')
  })
})

describe('validatePatch', () => {
  it('passes valid patch', () => {
    const patch = parsePatch(`*** Begin Patch
*** Update File: src/a.ts
@@ line @@
-old
+new
*** End Patch`)
    expect(validatePatch(patch)).toHaveLength(0)
  })

  it('catches move without destination', () => {
    const parsed = {
      files: [{ operation: 'move' as const, path: 'a.ts', chunks: [] }],
      errors: [],
    }
    const errors = validatePatch(parsed)
    expect(errors.some((e) => e.includes('destination'))).toBe(true)
  })

  it('catches update without chunks', () => {
    const parsed = {
      files: [{ operation: 'update' as const, path: 'a.ts', chunks: [] }],
      errors: [],
    }
    const errors = validatePatch(parsed)
    expect(errors.some((e) => e.includes('no changes'))).toBe(true)
  })
})
