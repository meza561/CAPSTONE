// History must round-trip every setting that affects the actual simulation -
// materials and insulated edges included, since restoring only some of them
// meant pressing Run after "View" recomputed a different simulation from the
// one on screen - and the write must not throw when localStorage is close to
// its quota, since that used to make a successful run report itself as
// failed. See the "move the timeline under the plate" commit.
const { test, expect } = require('@playwright/test');
const { runSimulation, setMaterials, setInsulated, setSources } = require('./helpers');

test('history restores materials and insulated edges, not just the basics', async ({ page }) => {
  await page.goto('/');

  const original = {
    mode: 'fdm',
    rows: 15,
    cols: 18,
    top: 80,
    bottom: 10,
    left: 5,
    right: 0,
    alpha: 0.02,
    insulated: { top: true, left: true, bottom: false, right: false },
    materials: [
      { x0: 2, y0: 2, x1: 5, y1: 5, alpha: 0.002 },
      { x0: 8, y0: 1, x1: 10, y1: 4, alpha: 0.01 },
    ],
    sources: [{ x: 3, y: 3, temp: 500 }],
  };
  await runSimulation(page, original);

  // Mutate every field the restore needs to overwrite, so a pass here proves
  // the values came from history rather than being left over in the form.
  await page.selectOption('#simMode', 'cn');
  await page.fill('#rows', '40');
  await page.fill('#cols', '40');
  await page.fill('#top', '1');
  await page.fill('#bottom', '2');
  await page.fill('#left', '3');
  await page.fill('#right', '4');
  await setInsulated(page, { top: false, left: false, bottom: true, right: true });
  await setMaterials(page, [{ x0: 0, y0: 0, x1: 1, y1: 1, alpha: 0.5 }]);
  await setSources(page, []);

  await page.locator('.tab-btn[data-tab="history-tab"]').click();
  await page.locator('#historyTable .view-btn').first().click();

  await expect(page.locator('#simMode')).toHaveValue(original.mode);
  await expect(page.locator('#rows')).toHaveValue(String(original.rows));
  await expect(page.locator('#cols')).toHaveValue(String(original.cols));
  await expect(page.locator('#top')).toHaveValue(String(original.top));
  await expect(page.locator('#bottom')).toHaveValue(String(original.bottom));
  await expect(page.locator('#left')).toHaveValue(String(original.left));
  await expect(page.locator('#right')).toHaveValue(String(original.right));

  await expect(page.locator('#insTop')).toBeChecked();
  await expect(page.locator('#insLeft')).toBeChecked();
  await expect(page.locator('#insBottom')).not.toBeChecked();
  await expect(page.locator('#insRight')).not.toBeChecked();

  const matRows = page.locator('#mat-list .mat-row');
  await expect(matRows).toHaveCount(original.materials.length);
  for (let i = 0; i < original.materials.length; i++) {
    const m = original.materials[i];
    const row = matRows.nth(i);
    await expect(row.locator('.mat-x0')).toHaveValue(String(m.x0));
    await expect(row.locator('.mat-y0')).toHaveValue(String(m.y0));
    await expect(row.locator('.mat-x1')).toHaveValue(String(m.x1));
    await expect(row.locator('.mat-y1')).toHaveValue(String(m.y1));
    await expect(row.locator('.mat-a')).toHaveValue(String(m.alpha));
  }

  await expect(page.locator('#hasPointSource')).toBeChecked();
  const srcRows = page.locator('#src-list .src-row');
  await expect(srcRows).toHaveCount(original.sources.length);
  await expect(srcRows.nth(0).locator('.src-x')).toHaveValue(String(original.sources[0].x));
  await expect(srcRows.nth(0).locator('.src-y')).toHaveValue(String(original.sources[0].y));
  await expect(srcRows.nth(0).locator('.src-t')).toHaveValue(String(original.sources[0].temp));

  // Restoring history switches back to the simulation tab.
  await expect(page.locator('#sim-tab')).not.toHaveClass(/hidden/);
});

test('a run still succeeds when localStorage is nearly out of quota', async ({ page }) => {
  await page.goto('/');

  await page.evaluate(() => {
    for (let i = 0; i < 64; i++) {
      try {
        localStorage.setItem(`quota-filler-${i}`, 'x'.repeat(1024 * 1024));
      } catch (e) {
        break; // out of quota - that is the point of this test
      }
    }
  });

  const { isError, statusText } = await runSimulation(page, { mode: 'fdm', rows: 20, cols: 20 });
  expect(isError, `status was: ${statusText}`).toBe(false);
  expect(statusText).not.toMatch(/^Error:/);

  await page.evaluate(() => {
    for (let i = 0; i < 64; i++) localStorage.removeItem(`quota-filler-${i}`);
  });
});
