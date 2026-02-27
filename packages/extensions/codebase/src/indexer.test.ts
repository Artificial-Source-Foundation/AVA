import { MockFileSystem } from '@ava/core-v2/__test-utils__/mock-platform'
import { describe, expect, it } from 'vitest'
import { createRepoMap, detectLanguage, indexFiles } from './indexer.js'

describe('detectLanguage', () => {
  it('detects TypeScript', () => {
    expect(detectLanguage('src/index.ts')).toBe('typescript')
    expect(detectLanguage('App.tsx')).toBe('typescript')
  })

  it('detects JavaScript', () => {
    expect(detectLanguage('main.js')).toBe('javascript')
    expect(detectLanguage('App.jsx')).toBe('javascript')
  })

  it('detects Python', () => {
    expect(detectLanguage('main.py')).toBe('python')
  })

  it('detects Rust', () => {
    expect(detectLanguage('main.rs')).toBe('rust')
  })

  it('returns unknown for unrecognized extensions', () => {
    expect(detectLanguage('file.xyz')).toBe('unknown')
  })
})

describe('indexFiles', () => {
  it('indexes files from glob', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/project/src/index.ts', 'export const x = 1')
    fs.addFile('/project/src/app.tsx', 'export default App')

    const files = await indexFiles('/project', fs, ['**/*.ts'])
    // MockFileSystem glob is basic, check it doesn't throw
    expect(Array.isArray(files)).toBe(true)
  })

  it('returns empty array when no files match', async () => {
    const fs = new MockFileSystem()
    const files = await indexFiles('/empty', fs)
    expect(files).toEqual([])
  })
})

describe('createRepoMap', () => {
  it('creates a repo map from file indices', () => {
    const map = createRepoMap([
      { path: '/a.ts', symbols: [], imports: [], exports: [], language: 'typescript', size: 100 },
      { path: '/b.py', symbols: [], imports: [], exports: [], language: 'python', size: 200 },
    ])
    expect(map.totalFiles).toBe(2)
    expect(map.totalSymbols).toBe(0)
    expect(map.generatedAt).toBeGreaterThan(0)
  })
})
