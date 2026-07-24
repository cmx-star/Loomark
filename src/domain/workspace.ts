import type { LoadedDocument } from '@/domain/document'

export type EditorMode = 'source' | 'reading' | 'split'
export type ThemeName = 'paper' | 'night'

export interface WorkspaceDocument extends LoadedDocument {
  id: string
  originalContent: string
  dirty: boolean
  mode: EditorMode
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
