export interface DirectoryEntry {
  isDirectory: boolean
  name: string
  path: string
}

export interface DirectoryListing {
  entries: DirectoryEntry[]
  parentPath: string | null
  path: string
  truncated: boolean
}
