export const FULL_EDITOR_LIMIT_BYTES = 10 * 1024 * 1024
export const MAX_SUPPORTED_BYTES = 50 * 1024 * 1024
export const PREVIEW_LIMIT_BYTES = 256 * 1024

export type LoadStrategy = 'full' | 'progressive' | 'unsupported'

export interface DocumentMetrics {
  byteSize: number
  lineCount: number
  longestLineBytes: number
  preflightMilliseconds: number
  readMilliseconds?: number
  editorMilliseconds?: number
}

export interface DocumentInspection extends DocumentMetrics {
  path: string
  strategy: LoadStrategy
}

export interface LoadedDocument extends DocumentInspection {
  content: string
}

export function classifyDocument(byteSize: number): LoadStrategy {
  if (byteSize <= FULL_EDITOR_LIMIT_BYTES) return 'full'
  if (byteSize <= MAX_SUPPORTED_BYTES) return 'progressive'
  return 'unsupported'
}

export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`
  return `${(bytes / 1024 / 1024).toFixed(2)} MiB`
}
