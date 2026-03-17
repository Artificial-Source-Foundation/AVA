#!/usr/bin/env node
// hello-plugin — standalone AVA plugin (no npm dependencies)
const fs = require('node:fs')

let _context = null
const hooks = ['session.start', 'session.end']

function sendMessage(msg) {
  const json = JSON.stringify(msg)
  const header = `Content-Length: ${Buffer.byteLength(json)}\r\n\r\n`
  fs.writeSync(1, header + json)
}

function handleMessage(msg) {
  if (msg.method === 'initialize') {
    _context = msg.params || {}
    sendMessage({ jsonrpc: '2.0', id: msg.id, result: { hooks } })
  } else if (msg.method === 'shutdown') {
    process.exit(0)
  } else if (msg.method === 'hook/session.start') {
    const goal = msg.params?.goal || 'unknown'
    process.stderr.write(`[hello-plugin] Session started: ${goal}\n`)
    if (msg.id != null) sendMessage({ jsonrpc: '2.0', id: msg.id, result: {} })
  } else if (msg.method === 'hook/session.end') {
    process.stderr.write('[hello-plugin] Session ended\n')
    if (msg.id != null) sendMessage({ jsonrpc: '2.0', id: msg.id, result: {} })
  } else if (msg.id != null) {
    sendMessage({ jsonrpc: '2.0', id: msg.id, result: {} })
  }
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
      process.stderr.write(`[hello-plugin] Parse error: ${e}\n`)
    }
  }
})
