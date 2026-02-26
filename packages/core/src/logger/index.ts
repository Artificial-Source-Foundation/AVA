/**
 * Logger Module
 * Barrel exports for the logging infrastructure
 */

export { AvaLogger, createLogger, getLogger, resetLogger, setLogger } from './logger.js'
export type { LogEntry, Logger, LoggerConfig, LogLevel } from './types.js'
export { DEFAULT_LOGGER_CONFIG, LOG_LEVEL_PRIORITY } from './types.js'
