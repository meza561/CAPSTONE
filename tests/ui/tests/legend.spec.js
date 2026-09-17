// The legend key must be generated from the same colour function the canvas
// paints with. Rather than re-implementing that function in the test (which
// is exactly how the old CSS gradient drifted from the canvas in the first
// place), this samples the canvas's own painted pixels and the legend's own
// rendered CSS gradient, and checks the two agree - a black-box comparison
// of the two outputs against each other, not against a second copy of the
// formula. See the "move the timeline under the plate" commit.
const { test, expect } = require('@playwright/test');
const {
  runSimulation, fetchFrame, canvasPixel, gradientStops, interpolateStops, parseLeadingNumber,
} = require('./helpers');

function expectClose(actual, expected, tolerance, label) {
  for (let c = 0; c < 3; c++) {
    expect(Math.abs(actual[c] - expected[c]), `${label} channel ${c}: ${actual} vs ${expected}`)
      .toBeLessThanOrEqual(tolerance);
  }
}

test('legend gradient matches the colours the canvas actually paints', async ({ page }) => {
  await page.goto('/');
  const N = 21;
  const { body } = await runSimulation(page, {
    mode: 'fdm', rows: N, cols: N, top: 100, bottom: 0, left: 0, right: 0, alpha: 0.05,
  });
  const finalStep = body.data.step;

  const legendMin = parseLeadingNumber(await page.locator('#legendMin').textContent());
  const legendMax = parseLeadingNumber(await page.locator('#legendMax').textContent());
  expect(legendMax - legendMin).toBeGreaterThan(50); // sanity: a real, non-degenerate range

  const frame = await fetchFrame(page, finalStep);
  const stops = await gradientStops(page);
  expect(stops.length).toBeGreaterThanOrEqual(2);

  const span = legendMax - legendMin;
  const tAt = (v) => (100 * (v - legendMin)) / span;

  // Exact endpoints: the top-middle cell is pinned at exactly `top`, the
  // bottom-middle cell at exactly `bottom` - no PDE uncertainty here, so
  // these should match the gradient's 0%/100% stops tightly.
  const mid = Math.floor(N / 2);
  const hotVal = frame.data[0][mid];
  const coldVal = frame.data[N - 1][mid];
  expectClose(await canvasPixel(page, mid, 0), interpolateStops(stops, tAt(hotVal)), 3, 'hot edge');
  expectClose(await canvasPixel(page, mid, N - 1), interpolateStops(stops, tAt(coldVal)), 3, 'cold edge');

  // An interior cell at an arbitrary mid-range value: the canvas pixel must
  // sit on the same gradient the key displays, not merely agree at the
  // extremes.
  const ri = Math.floor(N / 3);
  const rj = Math.floor((2 * N) / 3);
  const interiorVal = frame.data[ri][rj];
  expectClose(
    await canvasPixel(page, rj, ri),
    interpolateStops(stops, tAt(interiorVal)),
    4,
    'interior cell',
  );
});

test('switching colour maps keeps the canvas and legend in sync at the extremes', async ({ page }) => {
  await page.goto('/');
  const N = 15;
  const { body } = await runSimulation(page, {
    mode: 'fdm', rows: N, cols: N, top: 100, bottom: 0, left: 0, right: 0, alpha: 0.05,
  });
  const finalStep = body.data.step;
  const frame = await fetchFrame(page, finalStep);
  const mid = Math.floor(N / 2);

  await page.selectOption('#colorMap', 'blackbody');

  const legendMin = parseLeadingNumber(await page.locator('#legendMin').textContent());
  const legendMax = parseLeadingNumber(await page.locator('#legendMax').textContent());
  const stops = await gradientStops(page);

  const tAt = (v) => (100 * (v - legendMin)) / (legendMax - legendMin);
  expectClose(
    await canvasPixel(page, mid, 0),
    interpolateStops(stops, tAt(frame.data[0][mid])),
    3,
    'blackbody hot edge',
  );
  expectClose(
    await canvasPixel(page, mid, N - 1),
    interpolateStops(stops, tAt(frame.data[N - 1][mid])),
    3,
    'blackbody cold edge',
  );
});
