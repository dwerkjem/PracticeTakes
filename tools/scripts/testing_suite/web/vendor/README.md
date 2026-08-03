# Vendored web dependencies

## Chart.js 4.5.1 (`chart.umd.js`, MIT)

The history graphs. Committed rather than fetched, for two reasons:

- **The hub has to work offline.** It is a local development tool that already
  runs with no network — a CDN `<script>` would make the graphs the one part
  that breaks on a train.
- **There is no build step for these assets, and adding one would cost more
  than the dependency saves.** The page is plain HTML, CSS, and JavaScript
  served by a standard-library HTTP server; a bundler and a `node_modules` for
  four charts would be more moving parts than the thing they draw.

Upgrading is `npm pack chart.js`, then copy `package/dist/chart.umd.js` and
`package/LICENSE.md` here and note the new version in this file.

Nothing here is part of the shipped application: the testing suite is a separate
tool, and none of this reaches the Practice Takes binary.
