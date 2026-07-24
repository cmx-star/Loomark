import { describe, expect, it, vi } from 'vitest'
import { diagnosticDocumentName, diagnosticFailureReason, logDiagnostic, recentDiagnostics } from '@/diagnostics'

describe('diagnostic logging', () => {
  it('keeps only metadata and never requires a document path as a diagnostic value', () => {
    const log = vi.spyOn(console, 'info').mockImplementation(() => undefined)
    const document = diagnosticDocumentName('/Users/example/Documents/private-notes.md')
    logDiagnostic('info', 'document.load.started', { document, byteSize: 1024 })

    expect(recentDiagnostics().at(-1)).toMatchObject({
      event: 'document.load.started',
      fields: { document: 'private-notes.md', byteSize: 1024 },
      level: 'info',
    })
    log.mockRestore()
  })

  it('redacts absolute paths from failure details', () => {
    expect(diagnosticFailureReason(new Error('Could not read /Users/example/Documents/private-notes.md')))
      .toBe('Could not read [path]')
  })
})
