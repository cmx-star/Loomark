import { defineConfig } from 'vite'

// Keep the CommonJS main-process bundle distinct from this package's ESM files.
export default defineConfig({
  build: {
    rollupOptions: {
      output: {
        entryFileNames: '[name].cjs',
      },
    },
  },
})
