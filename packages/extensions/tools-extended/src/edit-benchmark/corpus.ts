import type { BenchmarkCase } from './types.js'

const DEFAULT_CORPUS: BenchmarkCase[] = [
  {
    id: 'simple-replace',
    description: 'single exact replacement',
    original: 'const x = 1\nconst y = 2\n',
    oldString: 'const x = 1',
    newString: 'const x = 42',
    expected: 'const x = 42\nconst y = 2\n',
  },
  {
    id: 'whitespace-change',
    description: 'replace with normalized spacing',
    original: 'function sum(a, b) {\n  return a + b\n}\n',
    oldString: 'function   sum(a,b){\nreturn a+b\n}',
    newString: 'function sum(a, b) {\n  return a - b\n}',
    expected: 'function sum(a, b) {\n  return a - b\n}\n',
  },
  {
    id: 'replace-all',
    description: 'replace all occurrences',
    original: 'let id = 1\nlet id = 2\n',
    oldString: 'let id =',
    newString: 'let value =',
    expected: 'let value = 1\nlet value = 2\n',
    replaceAll: true,
  },
  {
    id: 'indentation-flex',
    description: 'indentation differs between find and source',
    original: 'if (ok) {\n    run()\n}\n',
    oldString: 'if (ok) {\n  run()\n}',
    newString: 'if (ok) {\n    runSafe()\n}',
    expected: 'if (ok) {\n    runSafe()\n}\n',
  },
  {
    id: 'block-anchor',
    description: 'replace multiline block with same anchors',
    original: 'function x() {\n  const a = 1\n  const b = 2\n  return a + b\n}\n',
    oldString: 'function x() {\n  const a = 0\n  const b = 0\n  return a + b\n}',
    newString: 'function x() {\n  const a = 4\n  const b = 5\n  return a + b\n}',
    expected: 'function x() {\n  const a = 4\n  const b = 5\n  return a + b\n}\n',
  },
]

export function getDefaultCorpus(): BenchmarkCase[] {
  return [...DEFAULT_CORPUS]
}
