// @ts-check
const { defineConfig, devices } = require('@playwright/test');

// The suite is driven against a server that run_ui_tests.sh (or CI) starts
// separately - this config never launches the app itself, since building and
// starting it needs the C++ binary compiled first.
module.exports = defineConfig({
  testDir: './tests',
  timeout: 30_000,
  expect: { timeout: 15_000 },
  // server.py is explicitly single-writer (one DB file, one latest_heatmap.json,
  // guarded on the write side by SIM_LOCK - see CLAUDE.md). A GET made after this
  // suite's own POST can still race a *different* worker's POST clearing the DB
  // between them, so tests run serially against the one server instance rather
  // than in parallel workers.
  workers: 1,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 1 : 0,
  reporter: process.env.CI ? [['list'], ['html', { open: 'never' }]] : 'list',
  use: {
    baseURL: process.env.UI_BASE_URL || 'http://127.0.0.1:5000',
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
  },
  projects: [
    { name: 'chromium', use: { ...devices['Desktop Chrome'] } },
  ],
});
