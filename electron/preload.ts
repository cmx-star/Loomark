import { contextBridge, ipcRenderer } from 'electron'
import type { LoomarkDesktopApi, NativeMenuAction, NativeMenuState } from '../src/desktop-contract'

function listen<T>(channel: string, listener: (value: T) => void): () => void {
  const wrapped = (_event: Electron.IpcRendererEvent, value: T) => listener(value)
  ipcRenderer.on(channel, wrapped)
  return () => ipcRenderer.removeListener(channel, wrapped)
}

const loomark: LoomarkDesktopApi = {
  browseMarkdownDirectory: (path) => ipcRenderer.invoke('directory:browse', path),
  inspectDocument: (path) => ipcRenderer.invoke('document:inspect', path),
  listMarkdownSiblings: (path) => ipcRenderer.invoke('directory:siblings', path),
  log: (level, message, fields) => ipcRenderer.invoke('diagnostic:log', level, message, fields),
  measureDocument: (path) => ipcRenderer.invoke('document:measure', path),
  onDocumentChanged: (listener) => listen<string>('document-file-changed', listener),
  onNativeMenuAction: (listener) => listen<NativeMenuAction>('native-menu-action', listener),
  openLogDirectory: () => ipcRenderer.invoke('diagnostic:open-log-directory'),
  readDocument: (path) => ipcRenderer.invoke('document:read', path),
  readDocumentPreview: (path) => ipcRenderer.invoke('document:preview', path),
  saveDocument: (path, content) => ipcRenderer.invoke('document:save', path, content),
  selectMarkdownFile: () => ipcRenderer.invoke('dialog:open-markdown'),
  updateNativeMenu: (state: NativeMenuState) => ipcRenderer.invoke('menu:update', state),
  watchDocuments: (paths) => ipcRenderer.invoke('watcher:sync', paths),
}

contextBridge.exposeInMainWorld('loomark', loomark)
