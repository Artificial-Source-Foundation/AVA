/**
 * Message bus types — pure pub/sub with no policy dependency.
 */

export interface BusMessage {
  type: string
  correlationId: string
  timestamp: number
}

export type MessageHandler<T extends BusMessage = BusMessage> = (message: T) => void | Promise<void>
export type Unsubscribe = () => void
