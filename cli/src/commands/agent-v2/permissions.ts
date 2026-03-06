import * as readline from 'node:readline'
import type { BusMessage, MessageBus } from '@ava/core-v2/bus'
import type { AgentV2Options } from './types.js'

export function setupPermissionPrompts(
  bus: MessageBus,
  options: AgentV2Options
): { close(): void; unsubscribe(): void } {
  const alwaysApproved = new Set<string>()
  let rlInterface: readline.Interface | undefined

  if (options.yolo || options.json) {
    return { close: () => {}, unsubscribe: () => {} }
  }

  const unsubscribe = bus.subscribe('permission:request', async (msg: BusMessage) => {
    const data = msg as BusMessage & {
      toolName: string
      args: Record<string, unknown>
      risk: string
    }

    if (alwaysApproved.has(data.toolName)) {
      bus.publish({
        type: 'permission:response',
        correlationId: msg.correlationId,
        timestamp: Date.now(),
        approved: true,
      } as BusMessage & { approved: boolean })
      return
    }

    const argsPreview = JSON.stringify(data.args).slice(0, 120)
    const riskLabel =
      data.risk === 'high'
        ? '\x1b[31m[HIGH]\x1b[0m'
        : data.risk === 'medium'
          ? '\x1b[33m[MED]\x1b[0m'
          : ''

    if (!rlInterface) {
      rlInterface = readline.createInterface({ input: process.stdin, output: process.stderr })
    }

    const answer = await new Promise<string>((resolve) => {
      rlInterface!.question(
        `${riskLabel} Allow \x1b[1m${data.toolName}\x1b[0m(${argsPreview})? [y/N/a(lways)] `,
        (ans) => resolve(ans.trim().toLowerCase())
      )
    })

    const approved = answer === 'y' || answer === 'yes' || answer === 'a' || answer === 'always'
    if (answer === 'a' || answer === 'always') {
      alwaysApproved.add(data.toolName)
    }

    bus.publish({
      type: 'permission:response',
      correlationId: msg.correlationId,
      timestamp: Date.now(),
      approved,
      reason: approved ? undefined : 'Denied by user',
    } as BusMessage & { approved: boolean; reason?: string })
  })

  return {
    close: () => rlInterface?.close(),
    unsubscribe,
  }
}
