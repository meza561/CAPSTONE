// Narrow-screen layout: checked on load, after a run with a wide non-square
// grid, and on every tab.
//
// The Validation tab renders its charts asynchronously from /study, and
// study_results.json is a gitignored ./run_study.sh output that CI never
// generates - so measuring straight after the tab click only ever saw the
// "no results" placeholder, and missed .cost-row overflowing by 76px. The
// fixture is real heat_study output (minus the scaling timings validation.js
// never reads); if its shape drifts from what validation.js expects, the
// cost-row assertion below fails rather than letting the check pass empty.
const path = require('path');
const { test, expect } = require('@playwright/test');
const { runSimulation } = require('./helpers');

const STUDY_FIXTURE = path.join(__dirname, '..', 'fixtures', 'study_results.json');

test.use({ viewport: { width: 420, height: 800 } });

async function expectNoHorizontalOverflow(page, label) {
  const { scrollWidth, clientWidth } = await page.evaluate(() => ({
    scrollWidth: document.documentElement.scrollWidth,
    clientWidth: document.documentElement.clientWidth,
  }));
  expect(scrollWidth, `${label}: scrollWidth ${scrollWidth} vs clientWidth ${clientWidth}`)
    .toBeLessThanOrEqual(clientWidth + 1); // +1 for subpixel rounding
}

test('layout has no horizontal overflow at a narrow viewport', async ({ page }) => {
  await page.route('**/study', (route) => route.fulfill({ path: STUDY_FIXTURE }));

  await page.goto('/');
  await expectNoHorizontalOverflow(page, 'initial load');

  await runSimulation(page, { mode: 'fdm', rows: 24, cols: 160 }); // wide, non-square
  await expectNoHorizontalOverflow(page, 'after a wide non-square run');

  await page.locator('.tab-btn[data-tab="history-tab"]').click();
  await expectNoHorizontalOverflow(page, 'history tab');

  await page.locator('.tab-btn[data-tab="validation-tab"]').click();
  await expect(page.locator('#study-content')).toBeVisible();
  expect(await page.locator('.cost-row').count()).toBeGreaterThan(0);
  await expectNoHorizontalOverflow(page, 'validation tab');

  await page.locator('.data-details summary').click();
  await expect(page.locator('#study-tables table').first()).toBeVisible();
  await expectNoHorizontalOverflow(page, 'validation tab, underlying numbers expanded');
});
