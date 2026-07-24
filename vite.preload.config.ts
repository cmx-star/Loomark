import { defineConfig } from 'vite'

// Forge keeps preload bundles CommonJS; use .cjs because the package defaults .js to ESM.
export default defineConfig({
  build: {
    rollupOptions: {
      output: {
        entryFileNames: '[name].cjs',
      },
    },
  },
})
