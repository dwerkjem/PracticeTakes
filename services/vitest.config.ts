import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";

import { defineConfig } from "vitest/config";

export default defineConfig({
  plugins: [{
    name: "html-text-module",
    enforce: "pre",
    resolveId(source, importer) {
      if (!importer || source.startsWith("\0") ||
          (!source.endsWith(".html") && !source.endsWith(".css") &&
           !source.endsWith("/dashboard.js") && !source.endsWith("/audit.js"))) {
        return null;
      }
      return `\0dashboard-asset:${resolve(dirname(importer), source)}`;
    },
    async load(id) {
      const prefix = "\0dashboard-asset:";
      if (!id.startsWith(prefix)) return null;
      return `export default ${JSON.stringify(await readFile(id.slice(prefix.length), "utf8"))};`;
    },
  }],
});
