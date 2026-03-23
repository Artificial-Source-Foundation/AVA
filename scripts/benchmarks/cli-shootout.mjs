#!/usr/bin/env node

import { spawn, spawnSync } from 'node:child_process'
import rawFs from 'node:fs'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'

const DEFAULT_AVA = process.env.AVA_BENCH_BIN || 'ava'
const DEFAULT_OPENCODE = process.env.OPENCODE_BENCH_BIN || 'opencode'
const ANSI_REGEX = new RegExp(`${String.fromCharCode(27)}\\[[0-9;]*m`, 'g')

function parseArgs(argv) {
  const options = {
    avaBin: DEFAULT_AVA,
    opencodeBin: DEFAULT_OPENCODE,
    cwd: process.cwd(),
    outputDir: path.join(process.cwd(), '.tmp', 'benchmarks'),
    offlineIterations: 10,
    onlineIterations: 5,
    warmupRuns: 1,
    timeoutMs: 120000,
    online: false,
    avaFast: false,
    avaProvider: undefined,
    avaModel: undefined,
    opencodeModel: undefined,
  }

  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i]
    const next = argv[i + 1]
    switch (arg) {
      case '--ava-bin':
        options.avaBin = next
        i += 1
        break
      case '--opencode-bin':
        options.opencodeBin = next
        i += 1
        break
      case '--cwd':
        options.cwd = path.resolve(next)
        i += 1
        break
      case '--output-dir':
        options.outputDir = path.resolve(next)
        i += 1
        break
      case '--offline-iterations':
        options.offlineIterations = Number(next)
        i += 1
        break
      case '--online-iterations':
        options.onlineIterations = Number(next)
        i += 1
        break
      case '--warmup-runs':
        options.warmupRuns = Number(next)
        i += 1
        break
      case '--timeout-ms':
        options.timeoutMs = Number(next)
        i += 1
        break
      case '--online':
        options.online = true
        break
      case '--ava-fast':
        options.avaFast = true
        break
      case '--ava-provider':
        options.avaProvider = next
        i += 1
        break
      case '--ava-model':
        options.avaModel = next
        i += 1
        break
      case '--opencode-model':
        options.opencodeModel = next
        i += 1
        break
      default:
        throw new Error(`Unknown argument: ${arg}`)
    }
  }

  if (options.online) {
    if (!options.avaProvider || !options.avaModel) {
      throw new Error('--online requires --ava-provider and --ava-model')
    }
    if (!options.opencodeModel) {
      options.opencodeModel = `${options.avaProvider}/${options.avaModel}`
    }
  }

  return options
}

function commandExists(command) {
  const result = spawnSync(command, ['--help'], { stdio: 'ignore' })
  return !result.error
}

function assertBinaryAvailable(binaryPath, label) {
  const hasPathSeparator = binaryPath.includes('/')
  if (hasPathSeparator) {
    if (!rawFs.existsSync(binaryPath)) {
      throw new Error(`${label} binary not found: ${binaryPath}`)
    }
    return
  }

  const result = spawnSync('which', [binaryPath], { stdio: 'ignore' })
  if (result.status !== 0) {
    throw new Error(`${label} binary not found on PATH: ${binaryPath}`)
  }
}

function resolveBinaryPath(binaryPath) {
  if (binaryPath.includes('/')) {
    return binaryPath
  }

  const result = spawnSync('which', [binaryPath], { encoding: 'utf8' })
  if (result.status !== 0) {
    return binaryPath
  }
  return result.stdout.trim() || binaryPath
}

function runCapture(command, args, options = {}) {
  return new Promise((resolve) => {
    const start = process.hrtime.bigint()
    const child = spawn(command, args, {
      cwd: options.cwd,
      env: options.env ?? process.env,
      stdio: ['ignore', 'pipe', 'pipe'],
    })

    let stdout = ''
    let stderr = ''
    let firstChunkMs = null
    let timedOut = false

    const markFirstChunk = () => {
      if (firstChunkMs === null) {
        firstChunkMs = Number(process.hrtime.bigint() - start) / 1e6
      }
    }

    child.stdout.on('data', (chunk) => {
      markFirstChunk()
      stdout += chunk.toString()
    })
    child.stderr.on('data', (chunk) => {
      markFirstChunk()
      stderr += chunk.toString()
    })

    const timer = setTimeout(() => {
      timedOut = true
      child.kill('SIGKILL')
    }, options.timeoutMs ?? 120000)

    child.on('error', (error) => {
      clearTimeout(timer)
      resolve({
        exitCode: null,
        signal: null,
        stdout,
        stderr: `${stderr}${error.message}`,
        durationMs: Number(process.hrtime.bigint() - start) / 1e6,
        firstChunkMs,
        timedOut,
      })
    })

    child.on('close', (exitCode, signal) => {
      clearTimeout(timer)
      resolve({
        exitCode,
        signal,
        stdout,
        stderr,
        durationMs: Number(process.hrtime.bigint() - start) / 1e6,
        firstChunkMs,
        timedOut,
      })
    })
  })
}

async function runMeasuredCommand(command, args, options = {}) {
  const withTime = os.platform() === 'linux' && commandExists('/usr/bin/time')
  const result = withTime
    ? await runCapture('/usr/bin/time', ['-v', command, ...args], options)
    : await runCapture(command, args, options)

  const peakRssKb = withTime ? parsePeakRssKb(result.stderr) : null
  const stderr = withTime ? stripTimeOutput(result.stderr) : result.stderr

  return {
    ...result,
    stderr,
    peakRssKb,
  }
}

function parsePeakRssKb(stderr) {
  const match = stderr.match(/Maximum resident set size \(kbytes\):\s+(\d+)/)
  return match ? Number(match[1]) : null
}

function stripTimeOutput(stderr) {
  return stderr
    .split('\n')
    .filter((line) => !line.startsWith('\t') && !line.includes('Maximum resident set size'))
    .join('\n')
    .trim()
}

function median(values) {
  if (values.length === 0) return null
  const sorted = [...values].sort((a, b) => a - b)
  const middle = Math.floor(sorted.length / 2)
  return sorted.length % 2 === 0 ? (sorted[middle - 1] + sorted[middle]) / 2 : sorted[middle]
}

function percentile(values, p) {
  if (values.length === 0) return null
  const sorted = [...values].sort((a, b) => a - b)
  const index = Math.min(sorted.length - 1, Math.ceil((p / 100) * sorted.length) - 1)
  return sorted[index]
}

function stripAnsi(text) {
  return text.replaceAll(ANSI_REGEX, '')
}

function summarizeSamples(samples) {
  const successful = samples.filter((sample) => sample.success)
  const durations = successful.map((sample) => sample.durationMs)
  const ttfts = successful
    .map((sample) => sample.firstChunkMs)
    .filter((value) => typeof value === 'number')
  const rssValues = successful
    .map((sample) => sample.peakRssKb)
    .filter((value) => typeof value === 'number')

  return {
    runs: samples.length,
    successRate: samples.length === 0 ? 0 : successful.length / samples.length,
    durationMedianMs: median(durations),
    durationP95Ms: percentile(durations, 95),
    ttftMedianMs: median(ttfts),
    ttftP95Ms: percentile(ttfts, 95),
    peakRssMedianKb: median(rssValues),
    peakRssP95Kb: percentile(rssValues, 95),
  }
}

function makeOfflineTasks() {
  return [
    {
      name: 'help',
      description: 'Cold help output',
      avaArgs: ['--help'],
      opencodeArgs: ['--help'],
      verify: (_output, exitCode) => exitCode === 0,
    },
  ]
}

function makeOnlineTasks(options) {
  return [
    {
      name: 'echo',
      description: 'Exact short reply',
      prompt: 'Reply exactly with BENCHMARK_OK and nothing else.',
      verify: (output, exitCode) => exitCode === 0 && /BENCHMARK_OK/.test(output),
      avaArgs: [
        '--headless',
        ...(options.avaFast ? ['--fast'] : []),
        '--json',
        '--provider',
        options.avaProvider,
        '--model',
        options.avaModel,
        '--max-turns',
        '3',
        '--',
        'Reply exactly with BENCHMARK_OK and nothing else.',
      ],
      opencodeArgs: [
        'run',
        'Reply exactly with BENCHMARK_OK and nothing else.',
        '--format',
        'json',
        '--model',
        options.opencodeModel,
      ],
    },
    {
      name: 'package-name',
      description: 'Read package.json and answer',
      prompt: 'Read package.json in the current directory and reply with only the package name.',
      verify: (output, exitCode) => exitCode === 0 && /\bava\b/.test(output),
      avaArgs: [
        '--headless',
        ...(options.avaFast ? ['--fast'] : []),
        '--json',
        '--provider',
        options.avaProvider,
        '--model',
        options.avaModel,
        '--max-turns',
        '4',
        '--',
        'Read package.json in the current directory and reply with only the package name.',
      ],
      opencodeArgs: [
        'run',
        'Read package.json in the current directory and reply with only the package name.',
        '--format',
        'json',
        '--model',
        options.opencodeModel,
      ],
    },
  ]
}

function detectStructuredSuccess(cliName, result, task) {
  const cleanStdout = stripAnsi(result.stdout)
  if (cliName === 'ava') {
    // AVA can emit an explicit final success event even if the shell exit code is noisy,
    // so prefer the structured completion payload when the task output also verifies.
    return cleanStdout.includes('"success":true') && task.verify(cleanStdout, 0)
  }
  if (cliName === 'opencode') {
    // OpenCode JSON mode may still be usable when the stream ends with a stop event,
    // so accept structured success when the task output itself matches expectations.
    return cleanStdout.includes('"reason":"stop"') && task.verify(cleanStdout, 0)
  }
  return false
}

async function benchmarkCliTask(cliName, command, args, task, options, sampleIndex) {
  const result = await runMeasuredCommand(command, args, {
    cwd: options.cwd,
    timeoutMs: options.timeoutMs,
  })
  const combinedOutput = stripAnsi(`${result.stdout}\n${result.stderr}`)
  const success =
    task.verify(combinedOutput, result.exitCode) || detectStructuredSuccess(cliName, result, task)
  return {
    cli: cliName,
    task: task.name,
    sampleIndex,
    command,
    args,
    durationMs: result.durationMs,
    firstChunkMs: result.firstChunkMs,
    peakRssKb: result.peakRssKb,
    exitCode: result.exitCode,
    timedOut: result.timedOut,
    success,
    stdoutPreview: stripAnsi(result.stdout).trim().slice(0, 400),
    stderrPreview: stripAnsi(result.stderr).trim().slice(0, 400),
  }
}

async function runTaskGroup(groupName, tasks, iterations, options) {
  const results = []
  const cliMatrix = [
    { name: 'ava', bin: options.avaBin },
    { name: 'opencode', bin: options.opencodeBin },
  ]

  for (const task of tasks) {
    for (let warmup = 0; warmup < options.warmupRuns; warmup += 1) {
      for (const cli of cliMatrix) {
        const args = cli.name === 'ava' ? task.avaArgs : task.opencodeArgs
        await benchmarkCliTask(cli.name, cli.bin, args, task, options, -1)
      }
    }

    for (let sampleIndex = 0; sampleIndex < iterations; sampleIndex += 1) {
      const runOrder = sampleIndex % 2 === 0 ? cliMatrix : [...cliMatrix].reverse()
      for (const cli of runOrder) {
        const args = cli.name === 'ava' ? task.avaArgs : task.opencodeArgs
        results.push(await benchmarkCliTask(cli.name, cli.bin, args, task, options, sampleIndex))
      }
    }
  }

  return {
    name: groupName,
    tasks,
    results,
  }
}

function summarizeGroup(group) {
  const summary = {}
  for (const task of group.tasks) {
    summary[task.name] = {}
    for (const cliName of ['ava', 'opencode']) {
      const samples = group.results.filter(
        (result) => result.task === task.name && result.cli === cliName
      )
      summary[task.name][cliName] = summarizeSamples(samples)
    }
  }
  return summary
}

function failureSummaries(group, taskName, cliName) {
  return group.results
    .filter((result) => result.task === taskName && result.cli === cliName && !result.success)
    .map((result) => ({
      sampleIndex: result.sampleIndex,
      exitCode: result.exitCode,
      timedOut: result.timedOut,
      stdoutPreview: result.stdoutPreview,
      stderrPreview: result.stderrPreview,
    }))
}

function formatMs(value) {
  return value === null ? 'n/a' : `${value.toFixed(1)} ms`
}

function formatKb(value) {
  return value === null ? 'n/a' : `${(value / 1024).toFixed(1)} MB`
}

function makeMarkdownReport(metadata, groups, summaries) {
  const lines = []
  lines.push('# CLI Shootout')
  lines.push('')
  lines.push('## Environment')
  lines.push('')
  lines.push(`- Date: ${metadata.timestamp}`)
  lines.push(`- Host: ${metadata.hostname}`)
  lines.push(`- Platform: ${metadata.platform}`)
  lines.push(`- Repo commit: ${metadata.gitCommit}`)
  lines.push(`- Working directory: ${metadata.cwd}`)
  lines.push(`- AVA binary: ${metadata.avaBin}`)
  lines.push(`- OpenCode binary: ${metadata.opencodeBin}`)
  lines.push(`- AVA version: ${metadata.avaVersion}`)
  lines.push(`- OpenCode version: ${metadata.opencodeVersion}`)
  lines.push(`- AVA fast mode: ${metadata.config.avaFast ? 'enabled' : 'disabled'}`)
  lines.push(
    `- AVA binary size: ${metadata.avaSizeMb === null ? 'n/a' : `${metadata.avaSizeMb.toFixed(2)} MB`}`
  )
  lines.push(
    `- OpenCode binary size: ${metadata.opencodeSizeMb === null ? 'n/a' : `${metadata.opencodeSizeMb.toFixed(2)} MB`}`
  )
  lines.push('')
  lines.push('## Fairness Rules')
  lines.push('')
  lines.push('- Same machine, same cwd, same prompt text, same timeout, same iteration count.')
  lines.push('- Warmups are discarded; measured runs alternate AVA/OpenCode ordering per sample.')
  lines.push(
    '- Online runs are only included when explicitly enabled with the same provider/model pairing.'
  )
  lines.push(
    '- P95 becomes noisy at low sample counts; use medians first and raise iterations for stronger conclusions.'
  )
  lines.push('')

  for (const group of groups) {
    lines.push(`## ${group.name}`)
    lines.push('')
    lines.push('| Task | CLI | Success | Median | P95 | TTFT Median | Peak RSS Median |')
    lines.push('|---|---|---:|---:|---:|---:|---:|')
    for (const task of group.tasks) {
      for (const cliName of ['ava', 'opencode']) {
        const item = summaries[group.name][task.name][cliName]
        lines.push(
          `| ${task.name} | ${cliName} | ${(item.successRate * 100).toFixed(0)}% | ${formatMs(item.durationMedianMs)} | ${formatMs(item.durationP95Ms)} | ${formatMs(item.ttftMedianMs)} | ${formatKb(item.peakRssMedianKb)} |`
        )
      }
    }
    lines.push('')

    const failures = []
    for (const task of group.tasks) {
      for (const cliName of ['ava', 'opencode']) {
        const items = failureSummaries(group, task.name, cliName)
        if (items.length > 0) {
          failures.push({ task: task.name, cli: cliName, items })
        }
      }
    }

    if (failures.length > 0) {
      lines.push(`### ${group.name} failures`)
      lines.push('')
      for (const failure of failures) {
        lines.push(`- ${failure.task} / ${failure.cli}`)
        for (const item of failure.items) {
          const detail = item.stderrPreview || item.stdoutPreview || 'no preview captured'
          lines.push(
            `  - sample ${item.sampleIndex}: exit=${item.exitCode ?? 'null'} timeout=${item.timedOut} :: ${detail.replaceAll('\n', ' ').slice(0, 220)}`
          )
        }
      }
      lines.push('')
    }
  }

  return lines.join('\n')
}

function getVersion(binaryPath) {
  const result = spawnSync(binaryPath, ['--version'], { encoding: 'utf8' })
  if (result.status === 0) {
    return (result.stdout || result.stderr || '').trim()
  }
  return 'unsupported via --version'
}

function getBinarySizeMb(binaryPath) {
  try {
    const stat = rawFs.statSync(binaryPath)
    return stat.size / (1024 * 1024)
  } catch {
    return null
  }
}

function getGitCommit() {
  const result = spawnSync('git', ['rev-parse', '--short', 'HEAD'], { encoding: 'utf8' })
  return result.status === 0 ? result.stdout.trim() : 'unknown'
}

async function main() {
  const options = parseArgs(process.argv.slice(2))
  assertBinaryAvailable(options.avaBin, 'AVA')
  assertBinaryAvailable(options.opencodeBin, 'OpenCode')
  await fs.mkdir(options.outputDir, { recursive: true })

  const resolvedAvaBin = resolveBinaryPath(options.avaBin)
  const resolvedOpencodeBin = resolveBinaryPath(options.opencodeBin)

  const groups = []
  groups.push(await runTaskGroup('offline', makeOfflineTasks(), options.offlineIterations, options))
  if (options.online) {
    groups.push(
      await runTaskGroup('online', makeOnlineTasks(options), options.onlineIterations, options)
    )
  }

  const summaries = Object.fromEntries(groups.map((group) => [group.name, summarizeGroup(group)]))
  const timestamp = new Date().toISOString().replaceAll(':', '-').replaceAll('.', '-')
  const metadata = {
    timestamp: new Date().toISOString(),
    hostname: os.hostname(),
    platform: `${os.platform()} ${os.release()} ${os.arch()}`,
    gitCommit: getGitCommit(),
    cwd: options.cwd,
    avaBin: resolvedAvaBin,
    opencodeBin: resolvedOpencodeBin,
    avaVersion: getVersion(resolvedAvaBin),
    opencodeVersion: getVersion(resolvedOpencodeBin),
    avaSizeMb: getBinarySizeMb(resolvedAvaBin),
    opencodeSizeMb: getBinarySizeMb(resolvedOpencodeBin),
    config: {
      offlineIterations: options.offlineIterations,
      onlineIterations: options.onlineIterations,
      warmupRuns: options.warmupRuns,
      timeoutMs: options.timeoutMs,
      online: options.online,
      avaProvider: options.avaProvider ?? null,
      avaModel: options.avaModel ?? null,
      opencodeModel: options.opencodeModel ?? null,
      avaFast: options.avaFast,
    },
  }

  const payload = {
    metadata,
    groups: groups.map((group) => ({
      name: group.name,
      tasks: group.tasks.map(({ name, description }) => ({ name, description })),
      results: group.results,
    })),
    summaries,
  }

  const jsonPath = path.join(options.outputDir, `cli-shootout-${timestamp}.json`)
  const markdownPath = path.join(options.outputDir, `cli-shootout-${timestamp}.md`)
  await fs.writeFile(jsonPath, `${JSON.stringify(payload, null, 2)}\n`, 'utf8')
  await fs.writeFile(markdownPath, `${makeMarkdownReport(metadata, groups, summaries)}\n`, 'utf8')

  console.log(`Wrote ${jsonPath}`)
  console.log(`Wrote ${markdownPath}`)
}

main().catch((error) => {
  console.error(error instanceof Error ? (error.stack ?? error.message) : String(error))
  process.exit(1)
})
