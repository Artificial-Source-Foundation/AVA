#!/usr/bin/env node
const fs = require('node:fs')

let _context = null
const hooks = ['session.start', 'session.end', 'tool.before', 'tool.after']
const toolTimings = {}
const toolStarts = {}
let totalTools = 0
let blockedTools = 0

const BLOCKED_PATTERNS = [
  'rm -rf /',
  'rm -rf ~',
  'sudo rm',
  'mkfs',
  '> /dev/sda',
  ':(){ :|:& };:',
  'dd if=/dev/zero',
]

function sendMessage(msg) {
  const json = JSON.stringify(msg)
  const header = `Content-Length: ${Buffer.byteLength(json)}\r\n\r\n`
  fs.writeSync(1, header + json)
}

function handleMessage(msg) {
  if (msg.method === 'initialize') {
    _context = msg.params || {}
    process.stderr.write('[tool-timer] Plugin initialized\n')
    sendMessage({ jsonrpc: '2.0', id: msg.id, result: { hooks } })
    return
  }

  if (msg.method === 'shutdown') {
    process.stderr.write('[tool-timer] Shutting down\n')
    process.exit(0)
  }

  if (msg.method === 'hook/session.start') {
    process.stderr.write(`[tool-timer] Session started\n`)
    if (msg.id != null) sendMessage({ jsonrpc: '2.0', id: msg.id, result: {} })
    return
  }

  if (msg.method === 'hook/session.end') {
    // Print summary
    const entries = Object.entries(toolTimings)
    if (entries.length > 0) {
      process.stderr.write('\n[tool-timer] === Session Summary ===\n')
      process.stderr.write(`[tool-timer] Total tool calls: ${totalTools}\n`)
      process.stderr.write(`[tool-timer] Blocked calls: ${blockedTools}\n`)
      for (const [name, times] of entries) {
        const avg = (times.reduce((a, b) => a + b, 0) / times.length).toFixed(0)
        process.stderr.write(`[tool-timer] ${name}: ${times.length}x, avg ${avg}ms\n`)
      }
      process.stderr.write('[tool-timer] ======================\n\n')
    }
    if (msg.id != null) sendMessage({ jsonrpc: '2.0', id: msg.id, result: {} })
    return
  }

  if (msg.method === 'hook/tool.before') {
    const tool = msg.params?.tool || 'unknown'
    const args = msg.params?.args || {}
    const callId = msg.params?.call_id || '?'

    totalTools++
    toolStarts[callId] = Date.now()

    // Check for dangerous bash commands
    if (tool === 'bash' && args.command) {
      const cmd = args.command.toLowerCase()
      for (const pattern of BLOCKED_PATTERNS) {
        if (cmd.includes(pattern)) {
          blockedTools++
          process.stderr.write(`[tool-timer] BLOCKED dangerous command: ${args.command}\n`)
          if (msg.id != null) {
            sendMessage({
              jsonrpc: '2.0',
              id: msg.id,
              error: {
                code: -32000,
                message: `Plugin blocked: dangerous command "${pattern}" detected`,
              },
            })
          }
          return
        }
      }
    }

    process.stderr.write(`[tool-timer] -> ${tool}(${callId})\n`)
    if (msg.id != null) sendMessage({ jsonrpc: '2.0', id: msg.id, result: { args } })
    return
  }

  if (msg.method === 'hook/tool.after') {
    const tool = msg.params?.tool || 'unknown'
    const callId = msg.params?.call_id || '?'
    const startTime = toolStarts[callId]
    const elapsed = startTime ? Date.now() - startTime : 0
    delete toolStarts[callId]

    if (!toolTimings[tool]) toolTimings[tool] = []
    toolTimings[tool].push(elapsed)

    process.stderr.write(`[tool-timer] <- ${tool}(${callId}) ${elapsed}ms\n`)
    if (msg.id != null) sendMessage({ jsonrpc: '2.0', id: msg.id, result: {} })
    return
  }

  // Unknown method — respond OK
  if (msg.id != null) sendMessage({ jsonrpc: '2.0', id: msg.id, result: {} })
}

let buffer = ''
process.stdin.setEncoding('utf-8')
process.stdin.on('data', (chunk) => {
  buffer += chunk
  while (true) {
    const headerEnd = buffer.indexOf('\r\n\r\n')
    if (headerEnd === -1) break
    const header = buffer.substring(0, headerEnd)
    const match = header.match(/Content-Length:\s*(\d+)/)
    if (!match) {
      buffer = buffer.substring(headerEnd + 4)
      continue
    }
    const len = parseInt(match[1], 10)
    const bodyStart = headerEnd + 4
    if (buffer.length < bodyStart + len) break
    const body = buffer.substring(bodyStart, bodyStart + len)
    buffer = buffer.substring(bodyStart + len)
    try {
      handleMessage(JSON.parse(body))
    } catch (e) {
      process.stderr.write(`[tool-timer] Error: ${e}\n`)
    }
  }
})
