import type { DocumentInspection, DocumentMetrics, LoadedDocument } from '@/domain/document'

export type EditorMode = 'source' | 'reading' | 'split'
export type ThemeName = 'paper' | 'night'

export interface WorkspaceDocument extends LoadedDocument {
  id: string
  originalContent: string
  dirty: boolean
  mode: EditorMode
  sourceReady: boolean
  title: string
}

export interface SessionDocument {
  path: string
  mode: EditorMode
}

export interface WorkspaceSession {
  activeDocumentId: string | null
  documents: SessionDocument[]
  navigatorCollapsed?: boolean
  theme: ThemeName
}

export function documentTitle(path: string): string {
  return path.split(/[\\/]/).filter(Boolean).at(-1) ?? path
}

export function createWorkspaceDocument(document: LoadedDocument, mode: EditorMode = 'source'): WorkspaceDocument {
  return {
    ...document,
    id: document.path,
    originalContent: document.content,
    dirty: false,
    mode,
    sourceReady: true,
    title: documentTitle(document.path),
  }
}

export function createProgressiveWorkspaceDocument(
  document: DocumentInspection,
  previewContent: string,
): WorkspaceDocument {
  return {
    ...document,
    content: previewContent,
    id: document.path,
    originalContent: previewContent,
    dirty: false,
    mode: 'source',
    sourceReady: false,
    title: documentTitle(document.path),
  }
}

export function updateWorkspaceDocument(document: WorkspaceDocument, content: string): WorkspaceDocument {
  return {
    ...document,
    content,
    dirty: content !== document.originalContent,
  }
}

export function reloadWorkspaceDocument(document: WorkspaceDocument, loaded: LoadedDocument): WorkspaceDocument {
  return {
    ...createWorkspaceDocument(loaded, document.mode),
    id: document.id,
    title: document.title,
  }
}

export function applyProgressiveMetrics(document: WorkspaceDocument, metrics: DocumentMetrics): WorkspaceDocument {
  return {
    ...document,
    byteSize: metrics.byteSize,
    lineCount: metrics.lineCount,
    longestLineBytes: metrics.longestLineBytes,
    preflightMilliseconds: metrics.preflightMilliseconds,
    readMilliseconds: metrics.readMilliseconds,
  }
}

export function supportsRichDocumentViews(document: WorkspaceDocument): boolean {
  return document.sourceReady && document.strategy === 'full'
}

export function hasExternalContentChanged(document: WorkspaceDocument, diskContent: string): boolean {
  return document.content !== diskContent
}

export function closeWorkspaceDocument(documents: WorkspaceDocument[], id: string): WorkspaceDocument[] {
  return documents.filter((document) => document.id !== id)
}

export function createSession(
  documents: WorkspaceDocument[],
  activeDocumentId: string | null,
  theme: ThemeName,
  navigatorCollapsed = false,
): WorkspaceSession {
  return {
    activeDocumentId,
    documents: documents.map(({ path, mode }) => ({ path, mode })),
    navigatorCollapsed,
    theme,
  }
}
