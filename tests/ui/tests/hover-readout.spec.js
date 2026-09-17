// On a non-square grid the canvas is letterboxed (object-fit: contain), so a
// hover has to account for that to land on the right cell - see the
// "move the timeline under the plate" commit, which fixed exactly this on a
// 20x300 plate. A pinned heat source gives an exact, known value/location to
// check the readout against.
const { test, expect } = require('@playwright/test');
const { runSimulation, paintedRect, parseLeadingNumber } = require('./helpers');

test('hover readout maps to the correct cell on a non-square grid', async ({ page }) => {
  await page.goto('/');
  const rows = 24;
  const cols = 160;
  const source = { x: 140, y: 20, temp: 777 };

  await runSimulation(page, {
    mode: 'fdm', rows, cols, top: 0, bottom: 0, left: 0, right: 0, alpha: 0.05,
    sources: [source],
  });

  // Same (row, col) <-> (x, y) conversion server.py applies: row counts down
  // from the top, y counts up from the bottom.
  const row = (rows - 1) - source.y;
  const col = source.x;

  const box = await page.locator('#heatCanvas').boundingBox();
  const painted = paintedRect(box, rows, cols);
  const px = painted.left + ((col + 0.5) / cols) * painted.width;
  const py = painted.top + ((row + 0.5) / rows) * painted.height;

  await page.mouse.move(px, py);

  const tip = page.locator('.chart-tip');
  await expect(tip).toBeVisible();
  const text = await tip.textContent();

  const coordMatch = text.match(/x\s*=\s*(-?\d+),\s*y\s*=\s*(-?\d+)/);
  expect(coordMatch, `tooltip text was: ${text}`).not.toBeNull();
  expect(Number(coordMatch[1])).toBe(source.x);
  expect(Number(coordMatch[2])).toBe(source.y);

  const value = parseLeadingNumber(text);
  expect(value).toBeCloseTo(source.temp, 0);
});
