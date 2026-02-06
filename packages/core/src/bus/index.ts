/**
 * Message Bus - Public API
 * Event-driven tool/UI communication
 */

export {
  getMessageBus,
  MessageBus,
  resetMessageBus,
  setMessageBus,
} from './message-bus.js'

export {
  type AnyBusMessage,
  type AskUserRequest,
  type AskUserResponse,
  type BusMessage,
  BusMessageType,
  type PolicyUpdate,
  type ToolCallsUpdate,
  type ToolConfirmationRequest,
  type ToolConfirmationResponse,
  type ToolExecutionFailure,
  type ToolExecutionStart,
  type ToolExecutionSuccess,
  type ToolPolicyRejection,
} from './types.js'
