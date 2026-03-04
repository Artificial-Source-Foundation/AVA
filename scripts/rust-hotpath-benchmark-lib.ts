import { spawnSync } from 'node:child_process'
import { mkdir, mkdtemp, readdir, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import * as path from 'node:path'
import { fileURLToPath } from 'node:url'
import {
  type BenchmarkSummary,
  summarizeSamples,
} from '../packages/core-v2/src/tools/benchmark-stats.js'
import { replace } from '../packages/core-v2/src/tools/edit-replacers.js'
import {
  isBinaryExtension,
  matchesGlob,
  shouldSkipDirectory,
} from '../packages/core-v2/src/tools/utils.js'

interface CliOptions {
  iterations: number
  warmup: number
  keepFixture: boolean
}

interface FixturePaths {
  root: string
  repo: string
  fuzzyContentPath: string
  fuzzyOldPath: string
  fuzzyNewPath: string
}

interface RustBenchmarkOutput {
  summary: BenchmarkSummary
}

const MAX_RESULTS = 100
const BENCH_PATTERN = 'BENCH_TARGET_TOKEN_[0-9]+'

export function parseCliOptions(argv: string[]): CliOptions {
  const options: CliOptions = { iterations: 35, warmup: 8, keepFixture: false }

  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i]
    if (arg === '--iterations') {
      options.iterations = Number(argv[i + 1] ?? '0')
      i += 1
    } else if (arg === '--warmup') {
      options.warmup = Number(argv[i + 1] ?? '0')
      i += 1
    } else if (arg === '--keep-fixture') {
      options.keepFixture = true
    }
  }

  if (!Number.isFinite(options.iterations) || options.iterations <= 0) {
    throw new Error(`Invalid --iterations value: ${options.iterations}`)
  }
  if (!Number.isFinite(options.warmup) || options.warmup < 0) {
    throw new Error(`Invalid --warmup value: ${options.warmup}`)
  }

  return options
}

function nowMs(): number {
  return Number(process.hrtime.bigint()) / 1_000_000
}

async function createFixture(): Promise<FixturePaths> {
  const root = await mkdtemp(path.join(tmpdir(), 'ava-rust-hotpath-bench-'))
  const repo = path.join(root, 'repo')
  await mkdir(path.join(repo, 'src'), { recursive: true })

  for (let d = 0; d < 14; d += 1) {
    const dir = path.join(repo, 'src', `module-${String(d).padStart(2, '0')}`)
    await mkdir(dir, { recursive: true })

    for (let f = 0; f < 22; f += 1) {
      const lines: string[] = [`// synthetic benchmark file ${d}/${f}`]
      for (let l = 0; l < 170; l += 1) {
        const prefix = `v_${d}_${f}_${l}`
        lines.push(`const ${prefix} = ${l};`)
        if (l % 41 === 0) {
          lines.push(`const marker_${prefix} = "BENCH_TARGET_TOKEN_${l}";`)
        }
      }
      lines.push('export const done = true;')
      await writeFile(
        path.join(dir, `file-${String(f).padStart(3, '0')}.ts`),
        `${lines.join('\n')}\n`
      )
    }
  }

  const fuzzyContentPath = path.join(repo, 'fuzzy-content.ts')
  const fuzzyOldPath = path.join(repo, 'fuzzy-old.txt')
  const fuzzyNewPath = path.join(repo, 'fuzzy-new.txt')

  const fuzzyBlocks: string[] = []
  for (let i = 0; i < 450; i += 1) {
    fuzzyBlocks.push(`export function helper${i}(value: string) {`)
    fuzzyBlocks.push('  return value.trim();')
    fuzzyBlocks.push('}', '')
  }
  fuzzyBlocks.push('export function expensiveTask(alpha: string, beta: string) {')
  fuzzyBlocks.push('  const combined = alpha + beta;')
  fuzzyBlocks.push('  return combined.trim();')
  fuzzyBlocks.push('}')

  await writeFile(fuzzyContentPath, `${fuzzyBlocks.join('\n')}\n`)
  await writeFile(
    fuzzyOldPath,
    [
      'export function expensiveTask(alpha: string, beta: string) {',
      'const combined = alpha + beta;',
      'return combined.trim();',
      '}',
    ].join('\n')
  )
  await writeFile(
    fuzzyNewPath,
    [
      'export function expensiveTask(alpha: string, beta: string) {',
      '  const combined = `${alpha}:${beta}`;',
      '  return combined.trim();',
      '}',
    ].join('\n')
  )

  return { root, repo, fuzzyContentPath, fuzzyOldPath, fuzzyNewPath }
}

async function runTsGrep(pathRoot: string, include: string): Promise<void> {
  const regex = new RegExp(BENCH_PATTERN)
  let count = 0

  async function scan(dir: string, relativeDir: string): Promise<void> {
    if (count >= MAX_RESULTS) return
    const entries = await readdir(dir, { withFileTypes: true, encoding: 'utf8' })

    for (const entry of entries) {
      if (count >= MAX_RESULTS) return

      const absolute = path.join(dir, entry.name)
      const relative = relativeDir ? `${relativeDir}/${entry.name}` : entry.name

      if (entry.isDirectory()) {
        if (!shouldSkipDirectory(entry.name)) {
          await scan(absolute, relative)
        }
        continue
      }
      if (!entry.isFile()) continue

      const matchesRelative = matchesGlob(relative, include)
      const matchesName = matchesGlob(entry.name, include)
      if (!matchesRelative && !matchesName) continue
      if (isBinaryExtension(absolute)) continue

      const content = await readFile(absolute, 'utf8')
      for (const line of content.split('\n')) {
        if (regex.test(line)) {
          count += 1
          if (count >= MAX_RESULTS) return
        }
      }
    }
  }

  await scan(pathRoot, '')
}

async function benchmarkTsGrep(
  pathRoot: string,
  iterations: number,
  warmup: number
): Promise<BenchmarkSummary> {
  const samples: number[] = []
  for (let i = 0; i < warmup; i += 1) await runTsGrep(pathRoot, '**/*.ts')
  for (let i = 0; i < iterations; i += 1) {
    const started = nowMs()
    await runTsGrep(pathRoot, '**/*.ts')
    samples.push(nowMs() - started)
  }
  return summarizeSamples(samples)
}

async function benchmarkTsFuzzy(
  contentPath: string,
  oldPath: string,
  newPath: string,
  iterations: number,
  warmup: number
): Promise<BenchmarkSummary> {
  const content = await readFile(contentPath, 'utf8')
  const oldString = await readFile(oldPath, 'utf8')
  const newString = await readFile(newPath, 'utf8')
  const samples: number[] = []

  for (let i = 0; i < warmup; i += 1) replace(content, oldString, newString, false)
  for (let i = 0; i < iterations; i += 1) {
    const started = nowMs()
    const next = replace(content, oldString, newString, false)
    samples.push(nowMs() - started)
    if (!next.includes('`${alpha}:${beta}`')) {
      throw new Error('TS fuzzy benchmark replacement failed validation')
    }
  }

  return summarizeSamples(samples)
}

function runRustBenchmark(workspaceRoot: string, args: string[]): RustBenchmarkOutput {
  const manifestPath = path.join(workspaceRoot, 'src-tauri', 'Cargo.toml')
  const result = spawnSync(
    'cargo',
    [
      'run',
      '--quiet',
      '--manifest-path',
      manifestPath,
      '--bin',
      'hotpath-benchmark',
      '--',
      ...args,
    ],
    { cwd: workspaceRoot, encoding: 'utf8' }
  )

  if (result.status !== 0) {
    throw new Error(`Rust benchmark failed (${result.status}):\n${result.stderr || result.stdout}`)
  }
  return JSON.parse(result.stdout) as RustBenchmarkOutput
}

function printSummary(label: string, summary: BenchmarkSummary): void {
  console.log(
    `${label.padEnd(16)} p50=${summary.p50.toFixed(2)}ms  p95=${summary.p95.toFixed(2)}ms  mean=${summary.mean.toFixed(2)}ms  min=${summary.min.toFixed(2)}ms  max=${summary.max.toFixed(2)}ms`
  )
}

export async function runRustHotpathBenchmark(options: CliOptions): Promise<void> {
  const workspaceRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
  const fixture = await createFixture()

  try {
    console.log(`Fixture: ${fixture.repo}`)
    console.log(`Iterations: ${options.iterations}, warmup: ${options.warmup}\n`)

    const tsGrep = await benchmarkTsGrep(fixture.repo, options.iterations, options.warmup)
    const tsFuzzy = await benchmarkTsFuzzy(
      fixture.fuzzyContentPath,
      fixture.fuzzyOldPath,
      fixture.fuzzyNewPath,
      options.iterations,
      options.warmup
    )

    const rustGrep = runRustBenchmark(workspaceRoot, [
      'grep',
      fixture.repo,
      BENCH_PATTERN,
      '**/*.ts',
      String(options.iterations),
      String(options.warmup),
      String(MAX_RESULTS),
    ])
    const rustFuzzy = runRustBenchmark(workspaceRoot, [
      'fuzzy',
      fixture.fuzzyContentPath,
      fixture.fuzzyOldPath,
      fixture.fuzzyNewPath,
      String(options.iterations),
      String(options.warmup),
      '0',
    ])

    console.log('--- Hot Path Benchmark ---')
    printSummary('TS grep', tsGrep)
    printSummary('Rust grep', rustGrep.summary)
    console.log(`grep speedup (p95): ${(tsGrep.p95 / rustGrep.summary.p95).toFixed(2)}x\n`)
    printSummary('TS fuzzy', tsFuzzy)
    printSummary('Rust fuzzy', rustFuzzy.summary)
    console.log(`fuzzy speedup (p95): ${(tsFuzzy.p95 / rustFuzzy.summary.p95).toFixed(2)}x`)

    const outputPath = path.join(workspaceRoot, '.tmp', 'rust-hotpath-benchmark-latest.json')
    await mkdir(path.dirname(outputPath), { recursive: true })
    await writeFile(
      outputPath,
      `${JSON.stringify(
        {
          generatedAt: new Date().toISOString(),
          options,
          fixturePath: fixture.repo,
          ts: { grep: tsGrep, fuzzy: tsFuzzy },
          rust: { grep: rustGrep.summary, fuzzy: rustFuzzy.summary },
          speedup: {
            grepP50: tsGrep.p50 / rustGrep.summary.p50,
            grepP95: tsGrep.p95 / rustGrep.summary.p95,
            fuzzyP50: tsFuzzy.p50 / rustFuzzy.summary.p50,
            fuzzyP95: tsFuzzy.p95 / rustFuzzy.summary.p95,
          },
        },
        null,
        2
      )}\n`
    )
    console.log(`\nSaved benchmark report: ${outputPath}`)
  } finally {
    if (!options.keepFixture) {
      await rm(fixture.root, { recursive: true, force: true })
    }
  }
}
