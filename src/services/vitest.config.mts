import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";

import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    // Coverage is informational: reported and published, never gated. There is
    // deliberately no `thresholds` key here -- see the rebuild-test-suite
    // change.
    coverage: {
      provider: "v8",
      // Relative to the workspace vitest runs in (feedback-intake), so this
      // lands in the repository's gitignored build/ directory.
      reportsDirectory: "../../../build/coverage/services",
      reporter: ["text-summary", "json-summary", "lcov"],
      // Relative to the workspace root too. Getting this wrong is not a loud
      // failure -- it silently reports only the files a test imported, so
      // docker-server.ts vanished from the report entirely instead of showing
      // as 0%. That flatters the figure exactly the way omitting files from a
      // build target does.
      include: ["src/**/*.ts"],
      exclude: ["src/html.d.ts"],
      all: true,
    },
  },
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
