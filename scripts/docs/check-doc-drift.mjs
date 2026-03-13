#!/usr/bin/env node

import fs from 'node:fs'

const targets = [
  'docs/development/roadmap.md',
  'docs/development/backlog.md',
  'docs/development/epics.md',
  'docs/development/v3-plan.md',
  'docs/archives/ROADMAP.md',
  'docs/archives/completed-sprints/backlog.md',
  'docs/archives/completed-sprints/integration-backlog.md',
  'docs/archives/completed-sprints/2026-S2.3-plugin-ux-wiring.md',
  'docs/archives/completed-epics/plugin-ecosystem-ux-integration.md',
  'docs/archives/completed-epics/sprint-1.6-testing-hardening.md',
]

const checks = [
  {
    name: 'Streaming jitter stabilization',
    expected: 'done',
    match: /jitter|streaming polish/i,
  },
  {
    name: 'Manual OAuth runtime matrix',
    expected: 'in_progress',
    match: /manual\s+oauth.*matrix|oauth\s+runtime\s+matrix/i,
  },
  {
    name: 'Plugin lifecycle wiring',
    expected: 'in_progress',
    match: /lifecycle wiring|lifecycle actions|settings manager actions|INT-001|INT-002|INT-003/i,
  },
]

function classifyStatus(line) {
  const lower = line.toLowerCase()

  if (/\[x\]|\bdone\b|\bcompleted\b|\bcomplete\b/.test(lower)) {
    return 'done'
  }

  if (/\[ \]|\bpending\b|\btodo\b|\bremaining\b/.test(lower)) {
    return 'pending'
  }

  if (/\bin progress\b|\bin_progress\b|\bactive\b/.test(lower)) {
    return 'in_progress'
  }

  return 'unknown'
}

function readLines(path) {
  if (!fs.existsSync(path)) {
    return []
  }

  return fs
    .readFileSync(path, 'utf8')
    .split('\n')
    .map((line, index) => ({
      path,
      line: index + 1,
      text: line,
    }))
}

const lines = targets.flatMap(readLines)
const warnings = []

for (const check of checks) {
  const matches = lines.filter((entry) => check.match.test(entry.text))
  const statuses = matches
    .map((entry) => ({ ...entry, status: classifyStatus(entry.text) }))
    .filter((entry) => entry.status !== 'unknown')

  const stateSet = new Set(statuses.map((entry) => entry.status))

  if (statuses.length === 0) {
    warnings.push(`[warn] ${check.name}: no status-bearing lines found in tracked docs`)
    continue
  }

  if (check.expected === 'done') {
    if (stateSet.has('pending') || stateSet.has('in_progress')) {
      warnings.push(
        `[warn] ${check.name}: expected done, found conflicting pending/in-progress references`
      )
    }
  }

  if (check.expected === 'in_progress') {
    if (!stateSet.has('in_progress') && !stateSet.has('pending')) {
      warnings.push(
        `[warn] ${check.name}: expected in-progress signal, found only done/unknown references`
      )
    }
  }
}

if (warnings.length === 0) {
  console.log('[docs:drift] No contradictions detected for tracked items.')
} else {
  console.log('[docs:drift] Advisory warnings:')
  for (const warning of warnings) {
    console.log(`- ${warning}`)
  }
  console.log('[docs:drift] Warning-only mode: exiting with code 0.')
}

process.exit(0)
