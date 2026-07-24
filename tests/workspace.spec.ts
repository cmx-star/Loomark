import { describe, expect, it } from 'vitest'
import type { LoadedDocument } from '@/domain/document'
import { closeWorkspaceDocument, createSession, createWorkspaceDocument, updateWorkspaceDocument } from '@/domain/workspace'

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
})
