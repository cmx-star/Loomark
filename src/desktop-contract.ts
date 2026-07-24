import type { DirectoryListing } from '@/domain/directory'
import type { DocumentInspection, DocumentMetrics, LoadedDocument } from '@/domain/document'
import type { EditorMode, ThemeName } from '@/domain/workspace'

export const nativeMenuActions = [
  'open',
  'open-log-directory',
  'save',
  'print',
  'close-tab',
  'mode-source',
  'mode-reading',
  'mode-split',
  'toggle-navigator',
  'theme-paper',
  'theme-night',
  'locale-zh-CN',
  'locale-en-US',
] as const

export type NativeMenuAction = typeof nativeMenuActions[number]
export type DesktopLocale = 'en-US' | 'zh-CN'

export interface NativeMenuLabels {
  appearance: string
  closeTab: string
  copy: string
  cut: string
  edit: string
  file: string
  fullscreen: string
  language: string
  open: string
  openLogDirectory: string
  paste: string
  print: string
  redo: string
  save: string
  selectAll: string
  source: string
  reading: string
  split: string
  toggleNavigator: string
  undo: string
  view: string
  window: string
  night: string
  paper: string
  zhCN: string
  enUS: string
}

export interface NativeMenuState {
  activeDocument: boolean
  dirty: boolean
  labels: NativeMenuLabels
  locale: DesktopLocale
  mode: EditorMode
  navigatorCollapsed: boolean
  richViewsAvailable: boolean
  theme: ThemeName
}

export interface LoomarkDesktopApi {
  browseMarkdownDirectory(path: string): Promise<DirectoryListing>
  inspectDocument(path: string): Promise<DocumentInspection>
  listMarkdownSiblings(path: string): Promise<DirectoryListing>
  log(level: 'error' | 'info' | 'warn', message: string, fields: Record<string, string | undefined>): Promise<void>
  measureDocument(path: string): Promise<DocumentMetrics>
  onDocumentChanged(listener: (path: string) => void): () => void
  onNativeMenuAction(listener: (action: NativeMenuAction) => void): () => void
  openLogDirectory(): Promise<void>
  readDocument(path: string): Promise<LoadedDocument>
  readDocumentPreview(path: string): Promise<string>
  saveDocument(path: string, content: string): Promise<void>
  selectMarkdownFile(): Promise<string | null>
  updateNativeMenu(state: NativeMenuState): Promise<void>
  watchDocuments(paths: string[]): Promise<void>
}
