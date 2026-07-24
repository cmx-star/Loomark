export type DiagnosticLevel = 'info' | 'warn' | 'error'
export type DiagnosticFields = Record<string, boolean | number | string | undefined>

export interface DiagnosticEvent {
  event: string
  fields: DiagnosticFields
  level: DiagnosticLevel
  timestamp: string
}

const MAX_RECENT_EVENTS = 100
const recentEvents: DiagnosticEvent[] = []

function formatFields(fields: DiagnosticFields): Record<string, string | undefined> {
  return Object.fromEntries(
    Object.entries(fields).map(([key, value]) => [key, value === undefined ? undefined : String(value)]),
  )
}

function writeToConsole(level: DiagnosticLevel, message: string, fields: DiagnosticFields) {
  const details = Object.keys(fields).length > 0 ? fields : undefined
  if (level === 'error') console.error(message, details)
  else if (level === 'warn') console.warn(message, details)
  else console.info(message, details)
}

function writeToDesktopLog(level: DiagnosticLevel, message: string, fields: DiagnosticFields) {
  if (!window.loomark) return
  void window.loomark.log(level, message, formatFields(fields)).catch((cause) => {
    console.warn('[loomark] diagnostic log write failed', cause)
  })
}

export function logDiagnostic(level: DiagnosticLevel, event: string, fields: DiagnosticFields = {}) {
  const entry: DiagnosticEvent = {
    event,
    fields,
    level,
    timestamp: new Date().toISOString(),
  }
  recentEvents.push(entry)
  if (recentEvents.length > MAX_RECENT_EVENTS) recentEvents.splice(0, recentEvents.length - MAX_RECENT_EVENTS)

  const message = `[loomark] ${event}`
  writeToConsole(level, message, fields)
  writeToDesktopLog(level, message, fields)
}

export function recentDiagnostics(): readonly DiagnosticEvent[] {
  return recentEvents.slice()
}

export function diagnosticDocumentName(path: string): string {
  return path.split(/[\\/]/).filter(Boolean).at(-1) ?? path
}

export function diagnosticErrorMessage(cause: unknown): string {
  return cause instanceof Error ? cause.message : String(cause)
}

export function diagnosticFailureReason(cause: unknown): string {
  return diagnosticErrorMessage(cause)
    .replace(/(?:[A-Za-z]:)?(?:[\\/][^\\/:\s]+)+/g, '[path]')
    .slice(0, 500)
}
