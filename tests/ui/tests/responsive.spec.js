// Two fixes from the "move the timeline under the plate" commit were
// specifically about narrow screens: the canvas stage sizing itself from the
// viewport rather than its own (padded) column, and history/validation
// tables. Checked on load, after a run with a wide non-square grid, and on
// every tab.
const { test, expect } = require('@playwright/test');
const { runSimulation } = require('./helpers');

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
  await page.goto('/');
  await expectNoHorizontalOverflow(page, 'initial load');

  await runSimulation(page, { mode: 'fdm', rows: 24, cols: 160 }); // wide, non-square
  await expectNoHorizontalOverflow(page, 'after a wide non-square run');

  await page.locator('.tab-btn[data-tab="history-tab"]').click();
  await expectNoHorizontalOverflow(page, 'history tab');

  await page.locator('.tab-btn[data-tab="validation-tab"]').click();
  await expectNoHorizontalOverflow(page, 'validation tab');
});
