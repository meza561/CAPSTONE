// A continuous session that switches through all six modes without
// reloading, plus touches the timeline, playback and every tab - the things
// that only break when the page has accumulated state from a previous run,
// which six isolated page loads (simulation-modes.spec.js) would not catch.
const { test, expect } = require('@playwright/test');
const { runSimulation } = require('./helpers');

test('no console errors across a full run of all six modes', async ({ page }) => {
  const errors = [];
  page.on('console', (msg) => {
    if (msg.type() === 'error') errors.push(msg.text());
  });
  page.on('pageerror', (err) => errors.push(String(err)));

  await page.goto('/');

  const runs = [
    { mode: 'fdm' },
    { mode: 'be' },
    { mode: 'cn' },
    { mode: 'pde' },
    { mode: 'fisher', rdSteps: 300 },
    { mode: 'gray-scott', rdSteps: 300 },
  ];
  for (const opts of runs) {
    const { isError, statusText } = await runSimulation(page, { rows: 20, cols: 20, ...opts });
    expect(isError, `status was: ${statusText}`).toBe(false);
  }

  // The last run (gray-scott) leaves the timeline visible with several
  // frames; exercise scrubbing and playback before checking for errors.
  const timeSlider = page.locator('#timeSlider');
  if (await timeSlider.isVisible()) {
    await timeSlider.evaluate((el) => {
      el.value = '0';
      el.dispatchEvent(new Event('input', { bubbles: true }));
    });
    await page.locator('#playBtn').click();
    await page.waitForTimeout(250);
    await page.locator('#playBtn').click();
  }

  await page.locator('.tab-btn[data-tab="history-tab"]').click();
  await page.locator('.tab-btn[data-tab="validation-tab"]').click();
  await page.locator('.tab-btn[data-tab="sim-tab"]').click();

  expect(errors).toEqual([]);
});
