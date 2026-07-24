import { app, BrowserWindow, dialog, ipcMain, Menu, shell, type OpenDialogOptions } from 'electron'
import { watch, type FSWatcher } from 'node:fs'
import { appendFile, readFile, readdir, stat, writeFile } from 'node:fs/promises'
import { dirname, extname, join, resolve } from 'node:path'
import type { DirectoryListing } from '../src/domain/directory'
import {
  FULL_EDITOR_LIMIT_BYTES,
  MAX_SUPPORTED_BYTES,
  PREVIEW_LIMIT_BYTES,
  classifyDocument,
  type DocumentInspection,
  type DocumentMetrics,
  type LoadedDocument,
} from '../src/domain/document'
import type { NativeMenuAction, NativeMenuLabels, NativeMenuState } from '../src/desktop-contract'

declare const MAIN_WINDOW_VITE_DEV_SERVER_URL: string | undefined
declare const MAIN_WINDOW_VITE_NAME: string

app.setName('Loomark')

const MARKDOWN_EXTENSIONS = new Set(['.md', '.markdown', '.mdown', '.mkdn', '.mdtxt'])
const MAX_DIRECTORY_ENTRIES = 1_000
const nativeMenuLabelKeys: Array<keyof NativeMenuLabels> = [
  'appearance', 'closeTab', 'copy', 'cut', 'edit', 'enUS', 'file', 'fullscreen', 'language', 'night', 'open',
  'openLogDirectory', 'paper', 'paste', 'print', 'reading', 'redo', 'save', 'selectAll', 'source', 'split',
  'toggleNavigator', 'undo', 'view', 'window', 'zhCN',
]

interface FileSignature {
  byteSize: number
  modifiedAt: number
}

const defaultLabels: NativeMenuLabels = {
  appearance: '外观', closeTab: '关闭标签页', copy: '复制', cut: '剪切', edit: '编辑', enUS: 'English',
  file: '文件', fullscreen: '全屏', language: '语言', night: '夜间', open: '打开...',
  openLogDirectory: '打开日志目录', paper: '纸张', paste: '粘贴', print: '打印...', reading: '阅读', redo: '重做',
  save: '保存', selectAll: '全选', source: '源码', split: '分屏', toggleNavigator: '显示/隐藏目录', undo: '撤销',
  view: '视图', window: '窗口', zhCN: '简体中文',
}

let mainWindow: BrowserWindow | null = null
let menuState: NativeMenuState = {
  activeDocument: false,
  dirty: false,
  labels: defaultLabels,
  locale: 'zh-CN',
  mode: 'source',
  navigatorCollapsed: false,
  richViewsAvailable: false,
  theme: 'paper',
}
const directoryWatchers = new Map<string, FSWatcher>()
const watchedDocuments = new Map<string, string>()
const ownWrites = new Map<string, FileSignature>()

function isMarkdownPath(path: string): boolean {
  return MARKDOWN_EXTENSIONS.has(extname(path).toLowerCase())
}

function resolveMarkdownPath(value: unknown): string {
  if (typeof value !== 'string' || value.includes('\0')) throw new Error('A Markdown file path is required.')
  const path = resolve(value)
  if (!isMarkdownPath(path)) throw new Error('Only Markdown files can be opened.')
  return path
}

function resolveDirectoryPath(value: unknown): string {
  if (typeof value !== 'string' || value.includes('\0')) throw new Error('A directory path is required.')
  return resolve(value)
}

async function fileSignature(path: string): Promise<FileSignature | null> {
  try {
    const metadata = await stat(path)
    return metadata.isFile() ? { byteSize: metadata.size, modifiedAt: metadata.mtimeMs } : null
  } catch {
    return null
  }
}

async function inspectDocument(path: string): Promise<DocumentInspection> {
  const startedAt = performance.now()
  const metadata = await stat(path)
  if (!metadata.isFile()) throw new Error('The selected path is not a file.')
  return {
    byteSize: metadata.size,
    path,
    preflightMilliseconds: performance.now() - startedAt,
    strategy: classifyDocument(metadata.size),
  }
}

function contentMetrics(content: string): Pick<DocumentMetrics, 'lineCount' | 'longestLineBytes'> {
  if (!content) return { lineCount: 0, longestLineBytes: 0 }
  const lines = content.split(/\r?\n/)
  if (content.endsWith('\n')) lines.pop()
  return lines.reduce(
    (metrics, line) => ({
      lineCount: metrics.lineCount + 1,
      longestLineBytes: Math.max(metrics.longestLineBytes, Buffer.byteLength(line)),
    }),
    { lineCount: 0, longestLineBytes: 0 },
  )
}

async function readDocument(path: string): Promise<LoadedDocument> {
  const inspection = await inspectDocument(path)
  if (inspection.strategy === 'unsupported') throw new Error('Files above 50 MiB are outside the M0 supported range.')
  const startedAt = performance.now()
  const content = await readFile(path, 'utf8')
  return {
    ...inspection,
    ...contentMetrics(content),
    content,
    readMilliseconds: performance.now() - startedAt,
  }
}

async function measureDocument(path: string): Promise<DocumentMetrics> {
  const loaded = await readDocument(path)
  const { content: _content, path: _path, strategy: _strategy, ...metrics } = loaded
  return metrics
}

async function previewDocument(path: string): Promise<string> {
  const inspection = await inspectDocument(path)
  if (inspection.strategy === 'unsupported') throw new Error('Files above 50 MiB are outside the M0 supported range.')
  const content = await readFile(path)
  return content.subarray(0, PREVIEW_LIMIT_BYTES).toString('utf8')
}

async function listMarkdownDirectory(path: string): Promise<DirectoryListing> {
  const entries = await readdir(path, { withFileTypes: true })
  const visibleEntries = entries
    .filter((entry) => !entry.name.startsWith('.') && (entry.isDirectory() || isMarkdownPath(entry.name)))
    .sort((left, right) => Number(right.isDirectory()) - Number(left.isDirectory()) || left.name.localeCompare(right.name, undefined, { sensitivity: 'base' }))
  const truncated = visibleEntries.length > MAX_DIRECTORY_ENTRIES
  return {
    entries: visibleEntries.slice(0, MAX_DIRECTORY_ENTRIES).map((entry) => ({
      isDirectory: entry.isDirectory(),
      name: entry.name,
      path: join(path, entry.name),
    })),
    parentPath: dirname(path) === path ? null : dirname(path),
    path,
    truncated,
  }
}

function sendMenuAction(action: NativeMenuAction) {
  BrowserWindow.getFocusedWindow()?.webContents.send('native-menu-action', action)
}

function buildApplicationMenu() {
  const labels = menuState.labels
  const canUseRichViews = menuState.activeDocument && menuState.richViewsAvailable
  Menu.setApplicationMenu(Menu.buildFromTemplate([
    {
      label: 'Loomark',
      submenu: [
        { role: 'about' },
        { type: 'separator' },
        { role: 'services' },
        { type: 'separator' },
        { role: 'hide' },
        { role: 'hideOthers' },
        { role: 'unhide' },
        { type: 'separator' },
        { role: 'quit' },
      ],
    },
    {
      label: labels.file,
      submenu: [
        { label: labels.open, accelerator: 'CmdOrCtrl+O', click: () => sendMenuAction('open') },
        { label: labels.openLogDirectory, click: () => sendMenuAction('open-log-directory') },
        { type: 'separator' },
        { label: labels.save, accelerator: 'CmdOrCtrl+S', enabled: menuState.dirty, click: () => sendMenuAction('save') },
        { type: 'separator' },
        { label: labels.print, accelerator: 'CmdOrCtrl+P', enabled: canUseRichViews, click: () => sendMenuAction('print') },
        { type: 'separator' },
        { label: labels.closeTab, accelerator: 'CmdOrCtrl+W', enabled: menuState.activeDocument, click: () => sendMenuAction('close-tab') },
      ],
    },
    {
      label: labels.edit,
      submenu: [
        { role: 'undo', label: labels.undo }, { role: 'redo', label: labels.redo }, { type: 'separator' },
        { role: 'cut', label: labels.cut }, { role: 'copy', label: labels.copy }, { role: 'paste', label: labels.paste }, { role: 'selectAll', label: labels.selectAll },
      ],
    },
    {
      label: labels.view,
      submenu: [
        { label: labels.source, type: 'radio', checked: menuState.mode === 'source', enabled: menuState.activeDocument, click: () => sendMenuAction('mode-source') },
        { label: labels.reading, type: 'radio', checked: menuState.mode === 'reading', enabled: canUseRichViews, click: () => sendMenuAction('mode-reading') },
        { label: labels.split, type: 'radio', checked: menuState.mode === 'split', enabled: canUseRichViews, click: () => sendMenuAction('mode-split') },
        { type: 'separator' },
        { label: labels.toggleNavigator, accelerator: 'CmdOrCtrl+Shift+B', click: () => sendMenuAction('toggle-navigator') },
      ],
    },
    {
      label: labels.appearance,
      submenu: [
        { label: labels.paper, type: 'radio', checked: menuState.theme === 'paper', click: () => sendMenuAction('theme-paper') },
        { label: labels.night, type: 'radio', checked: menuState.theme === 'night', click: () => sendMenuAction('theme-night') },
      ],
    },
    {
      label: labels.language,
      submenu: [
        { label: labels.zhCN, type: 'radio', checked: menuState.locale === 'zh-CN', click: () => sendMenuAction('locale-zh-CN') },
        { label: labels.enUS, type: 'radio', checked: menuState.locale === 'en-US', click: () => sendMenuAction('locale-en-US') },
      ],
    },
    { label: labels.window, submenu: [{ role: 'togglefullscreen', label: labels.fullscreen }] },
  ]))
}

function isValidMenuState(value: unknown): value is NativeMenuState {
  if (!value || typeof value !== 'object') return false
  const state = value as Partial<NativeMenuState>
  return typeof state.activeDocument === 'boolean'
    && typeof state.dirty === 'boolean'
    && (state.locale === 'zh-CN' || state.locale === 'en-US')
    && (state.mode === 'source' || state.mode === 'reading' || state.mode === 'split')
    && typeof state.navigatorCollapsed === 'boolean'
    && typeof state.richViewsAvailable === 'boolean'
    && (state.theme === 'paper' || state.theme === 'night')
    && isNativeMenuLabels(state.labels)
}

function isNativeMenuLabels(value: unknown): value is NativeMenuLabels {
  if (!value || typeof value !== 'object') return false
  const labels = value as Partial<NativeMenuLabels>
  return nativeMenuLabelKeys.every((key) => typeof labels[key] === 'string')
}

async function synchronizeWatchers(paths: unknown): Promise<void> {
  if (!Array.isArray(paths) || !paths.every((path) => typeof path === 'string')) throw new Error('Document paths must be an array.')
  const normalizedDocuments = new Map<string, string>()
  for (const rawPath of paths) {
    const path = resolveMarkdownPath(rawPath)
    normalizedDocuments.set(path, rawPath)
  }
  watchedDocuments.clear()
  normalizedDocuments.forEach((originalPath, path) => watchedDocuments.set(path, originalPath))
  const desiredDirectories = new Set([...normalizedDocuments.keys()].map((path) => dirname(path)))
  for (const [directory, watcher] of directoryWatchers) {
    if (desiredDirectories.has(directory)) continue
    watcher.close()
    directoryWatchers.delete(directory)
  }
  for (const directory of desiredDirectories) {
    if (directoryWatchers.has(directory)) continue
    const watcher = watch(directory, { persistent: false }, (_eventType, filename) => {
      void emitDocumentChanges(directory, filename?.toString())
    })
    directoryWatchers.set(directory, watcher)
  }
  for (const path of ownWrites.keys()) if (!normalizedDocuments.has(path)) ownWrites.delete(path)
}

async function emitDocumentChanges(directory: string, filename?: string) {
  try {
    const candidates = filename
      ? [resolve(directory, filename)]
      : [...watchedDocuments.keys()].filter((path) => dirname(path) === directory)
    for (const path of candidates) {
      const originalPath = watchedDocuments.get(path)
      if (!originalPath) continue
      const ownWrite = ownWrites.get(path)
      const currentSignature = await fileSignature(path)
      if (ownWrite && currentSignature && ownWrite.byteSize === currentSignature.byteSize && ownWrite.modifiedAt === currentSignature.modifiedAt) continue
      mainWindow?.webContents.send('document-file-changed', originalPath)
    }
  } catch (cause) {
    void writeDiagnostic('error', 'watcher.failed', { reason: String(cause) })
  }
}

async function writeDiagnostic(level: string, message: string, fields: Record<string, string | undefined> = {}) {
  const entry = `[${new Date().toISOString()}][${level}] ${message} ${JSON.stringify(fields)}\n`
  console[level === 'error' ? 'error' : level === 'warn' ? 'warn' : 'info'](entry.trim())
  await appendFile(join(app.getPath('logs'), 'loomark.log'), entry, 'utf8')
}

function registerIpc() {
  ipcMain.handle('dialog:open-markdown', async () => {
    const options: OpenDialogOptions = {
      filters: [{ name: 'Markdown', extensions: [...MARKDOWN_EXTENSIONS].map((extension) => extension.slice(1)) }],
      properties: ['openFile'],
    }
    const result = mainWindow
      ? await dialog.showOpenDialog(mainWindow, options)
      : await dialog.showOpenDialog(options)
    return result.canceled ? null : result.filePaths[0] ?? null
  })
  ipcMain.handle('document:inspect', (_event, value) => inspectDocument(resolveMarkdownPath(value)))
  ipcMain.handle('document:read', (_event, value) => readDocument(resolveMarkdownPath(value)))
  ipcMain.handle('document:preview', (_event, value) => previewDocument(resolveMarkdownPath(value)))
  ipcMain.handle('document:measure', (_event, value) => measureDocument(resolveMarkdownPath(value)))
  ipcMain.handle('document:save', async (_event, value, content) => {
    const path = resolveMarkdownPath(value)
    if (typeof content !== 'string') throw new Error('Markdown content must be text.')
    await writeFile(path, content, 'utf8')
    const signature = await fileSignature(path)
    if (signature) ownWrites.set(path, signature)
  })
  ipcMain.handle('directory:browse', (_event, value) => listMarkdownDirectory(resolveDirectoryPath(value)))
  ipcMain.handle('directory:siblings', (_event, value) => listMarkdownDirectory(dirname(resolveMarkdownPath(value))))
  ipcMain.handle('watcher:sync', (_event, paths) => synchronizeWatchers(paths))
  ipcMain.handle('diagnostic:log', (_event, level, message, fields) => {
    if (!['error', 'info', 'warn'].includes(level) || typeof message !== 'string' || !fields || typeof fields !== 'object') throw new Error('Invalid diagnostic entry.')
    return writeDiagnostic(level, message.slice(0, 500), fields)
  })
  ipcMain.handle('diagnostic:open-log-directory', async () => {
    const error = await shell.openPath(app.getPath('logs'))
    if (error) throw new Error(error)
  })
  ipcMain.handle('menu:update', (_event, state) => {
    if (!isValidMenuState(state)) throw new Error('Invalid native menu state.')
    menuState = state
    buildApplicationMenu()
  })
}

async function createWindow() {
  mainWindow = new BrowserWindow({
    height: 720,
    minHeight: 520,
    minWidth: 760,
    title: 'Loomark',
    width: 1120,
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      preload: join(__dirname, 'preload.cjs'),
      sandbox: true,
    },
  })
  if (MAIN_WINDOW_VITE_DEV_SERVER_URL) await mainWindow.loadURL(MAIN_WINDOW_VITE_DEV_SERVER_URL)
  else await mainWindow.loadFile(join(__dirname, `../renderer/${MAIN_WINDOW_VITE_NAME}/index.html`))
}

app.whenReady().then(async () => {
  app.setAppLogsPath(join(app.getPath('userData'), 'logs'))
  registerIpc()
  buildApplicationMenu()
  await createWindow()
  await writeDiagnostic('info', 'application.started')
  app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) void createWindow() })
})

app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit() })
app.on('before-quit', () => { directoryWatchers.forEach((watcher) => watcher.close()) })
