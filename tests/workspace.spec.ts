import { describe, expect, it } from 'vitest'
import type { DocumentInspection, LoadedDocument } from '@/domain/document'
import {
  applyProgressiveMetrics,
  closeWorkspaceDocument,
  createSession,
  createProgressiveWorkspaceDocument,
  createWorkspaceDocument,
  hasExternalContentChanged,
  reloadWorkspaceDocument,
  supportsRichDocumentViews,
  updateWorkspaceDocument,
} from '@/domain/workspace'

const loaded: LoadedDocument = {
  path: '/notes/example.md',
  byteSize: 12,
  content: '# Example\n',
  lineCount: 1,
  longestLineBytes: 9,
  preflightMilliseconds: 1,
  readMilliseconds: 2,
  strategy: 'full',
}

const progressiveInspection: DocumentInspection = {
  path: '/notes/large.md',
  byteSize: 10 * 1024 * 1024 + 1,
  preflightMilliseconds: 1,
  strategy: 'progressive',
}

describe('workspace document state', () => {
  it('marks a changed document as unsaved and clears it after content returns', () => {
    const document = createWorkspaceDocument(loaded)
    expect(updateWorkspaceDocument(document, '# Changed\n').dirty).toBe(true)
    expect(updateWorkspaceDocument(document, loaded.content).dirty).toBe(false)
  })

  it('serializes session paths, per-document modes, and navigator state without content', () => {
    const document = createWorkspaceDocument(loaded, 'split')
    expect(createSession([document], document.id, 'night', true)).toEqual({
      activeDocumentId: '/notes/example.md',
      documents: [{ path: '/notes/example.md', mode: 'split' }],
      navigatorCollapsed: true,
      theme: 'night',
    })
  })

  it('closes only the requested document', () => {
    const first = createWorkspaceDocument(loaded)
    const second = createWorkspaceDocument({ ...loaded, path: '/notes/second.md' })
    expect(closeWorkspaceDocument([first, second], first.id)).toEqual([second])
  })

  it('reloads from disk only when requested and preserves the selected mode', () => {
    const changed = updateWorkspaceDocument(createWorkspaceDocument(loaded, 'split'), '# Local edits\n')
    const reloaded = reloadWorkspaceDocument(changed, { ...loaded, content: '# External version\n' })

    expect(reloaded).toMatchObject({
      content: '# External version\n',
      dirty: false,
      mode: 'split',
      originalContent: '# External version\n',
    })
  })

  it('keeps a progressive document as a bounded non-editable preview after metrics are measured', () => {
    const preview = createProgressiveWorkspaceDocument(progressiveInspection, '# Preview\n')
    const measured = applyProgressiveMetrics(preview, {
      byteSize: progressiveInspection.byteSize,
      lineCount: 1,
      longestLineBytes: 17,
      preflightMilliseconds: 1,
      readMilliseconds: 4,
    })

    expect(preview).toMatchObject({
      content: '# Preview\n',
      dirty: false,
      mode: 'source',
      sourceReady: false,
    })
    expect(preview).not.toHaveProperty('lineCount')
    expect(preview).not.toHaveProperty('longestLineBytes')
    expect(measured).toMatchObject({
      content: '# Preview\n',
      lineCount: 1,
      longestLineBytes: 17,
      mode: 'source',
      sourceReady: false,
    })
    expect(supportsRichDocumentViews(measured)).toBe(false)
    expect(supportsRichDocumentViews(createWorkspaceDocument(loaded))).toBe(true)
  })

  it.each([
    ['an unchanged clean document', createWorkspaceDocument(loaded), loaded.content, false],
    ['an unchanged dirty document', updateWorkspaceDocument(createWorkspaceDocument(loaded), '# Local edits\n'), '# Local edits\n', false],
    ['a changed clean document', createWorkspaceDocument(loaded), '# External version\n', true],
    ['a changed dirty document', updateWorkspaceDocument(createWorkspaceDocument(loaded), '# Local edits\n'), '# External version\n', true],
    ['a changed progressive document', createWorkspaceDocument({ ...loaded, byteSize: 10 * 1024 * 1024 + 1, strategy: 'progressive' }), '# External version\n', true],
  ])('prompts only when the external content differs for %s', (_description, document, diskContent, expected) => {
    expect(hasExternalContentChanged(document, diskContent)).toBe(expected)
  })

})
