// Shared helpers for the UI suite. Kept black-box: nothing here re-implements
// the app's own math (colour mapping, letterboxing) except where that math is
// generic browser/CSS behaviour rather than app-specific logic - see the
// comments in legend.spec.js and hover-readout.spec.js for the two exceptions.
const { expect } = require('@playwright/test');

const RUNNING_TEXT = 'Running simulation on server…';

const DEFAULTS = {
  mode: 'fdm',
  rows: 20,
  cols: 20,
  top: 100,
  bottom: 0,
  left: 0,
  right: 0,
  alpha: 0.01,
  F: 5,
};

/** Replace the rows currently in a src-list/mat-list style list with `count`
 * fresh ones, by deleting existing rows and clicking `addSelector` to grow it
 * back - mirrors how a person would actually edit the list. */
async function resetRows(page, listSelector, addSelector, count) {
  let rows = page.locator(`${listSelector} > div`);
  // Both src-row and mat-row are div children; the "nothing yet" hint is a
  // <p>, so this selector only ever matches real rows.
  while (await rows.count() > 0) {
    await rows.first().locator('.src-del').click();
  }
  for (let i = 0; i < count; i++) {
    await page.locator(addSelector).click();
  }
}

async function setSources(page, sources) {
  const hasPS = page.locator('#hasPointSource');
  if (!sources || sources.length === 0) {
    if (await hasPS.isChecked()) await hasPS.setChecked(false);
    return;
  }
  if (!(await hasPS.isChecked())) await hasPS.setChecked(true);
  await resetRows(page, '#src-list', '#addSrcBtn', sources.length);
  const rows = page.locator('#src-list .src-row');
  for (let i = 0; i < sources.length; i++) {
    const row = rows.nth(i);
    await row.locator('.src-x').fill(String(sources[i].x));
    await row.locator('.src-y').fill(String(sources[i].y));
    await row.locator('.src-t').fill(String(sources[i].temp));
  }
}

async function setMaterials(page, materials) {
  if (!materials) return;
  await resetRows(page, '#mat-list', '#addMatBtn', materials.length);
  const rows = page.locator('#mat-list .mat-row');
  for (let i = 0; i < materials.length; i++) {
    const row = rows.nth(i);
    const m = materials[i];
    await row.locator('.mat-x0').fill(String(m.x0));
    await row.locator('.mat-y0').fill(String(m.y0));
    await row.locator('.mat-x1').fill(String(m.x1));
    await row.locator('.mat-y1').fill(String(m.y1));
    await row.locator('.mat-a').fill(String(m.alpha));
  }
}

async function setInsulated(page, insulated) {
  if (!insulated) return;
  const boxes = {
    top: page.locator('#insTop'),
    bottom: page.locator('#insBottom'),
    left: page.locator('#insLeft'),
    right: page.locator('#insRight'),
  };
  for (const [edge, box] of Object.entries(boxes)) {
    const want = !!insulated[edge];
    if ((await box.isChecked()) !== want) await box.setChecked(want);
  }
}

/**
 * Fill the simulation form from a flat options object and run it, waiting
 * for both the network round trip and the status line to settle. Returns the
 * parsed /run response body plus the final status text/error state.
 */
async function runSimulation(page, opts = {}) {
  const o = { ...DEFAULTS, ...opts };
  const isRD = o.mode === 'fisher' || o.mode === 'gray-scott';
  const isPde = o.mode === 'pde';
  const isImplicit = o.mode === 'be' || o.mode === 'cn';

  await page.selectOption('#simMode', o.mode);
  await page.fill('#rows', String(o.rows));
  await page.fill('#cols', String(o.cols));

  // alpha-params/mat-group/ps-group are hidden for pde and RD alike (the
  // analytical solver has no diffusivity, materials or sources); boundary
  // temperatures and insulated edges stay visible for pde, just not RD.
  if (!isRD) {
    await page.fill('#top', String(o.top));
    await page.fill('#bottom', String(o.bottom));
    await page.fill('#left', String(o.left));
    await page.fill('#right', String(o.right));
    await setInsulated(page, o.insulated);
  }
  if (!isRD && !isPde) {
    await page.fill('#alpha', String(o.alpha));
    await setMaterials(page, o.materials);
    await setSources(page, o.sources);
  }
  if (isImplicit) {
    await page.fill('#diffusionF', String(o.F));
  }
  if (isRD && o.rdSteps !== undefined) {
    await page.fill('#rdSteps', String(o.rdSteps));
  }
  if (o.mode === 'fisher') {
    if (o.rdD !== undefined) await page.fill('#fkD', String(o.rdD));
    if (o.rdR !== undefined) await page.fill('#fkR', String(o.rdR));
  }
  if (o.mode === 'gray-scott') {
    if (o.feed !== undefined) await page.fill('#gsFeed', String(o.feed));
    if (o.kill !== undefined) await page.fill('#gsKill', String(o.kill));
    if (o.Du !== undefined) await page.fill('#gsDu', String(o.Du));
    if (o.Dv !== undefined) await page.fill('#gsDv', String(o.Dv));
  }

  const [resp] = await Promise.all([
    page.waitForResponse(
      (r) => r.url().includes('/run') && r.request().method() === 'POST',
      { timeout: 25_000 },
    ),
    page.click('#runBtn'),
  ]);

  const statusEl = page.locator('#status');
  await expect(statusEl).not.toHaveText(RUNNING_TEXT, { timeout: 25_000 });

  const body = await resp.json();
  const statusText = (await statusEl.textContent()) || '';
  const isError = await statusEl.evaluate((el) => el.classList.contains('is-error'));
  return { body, statusText, isError };
}

/** First numeric token in a string, with thousands separators stripped -
 * covers every branch of script.js's fmtTime/fmtValue formatting (fixed,
 * exponential, or grouped). */
function parseLeadingNumber(text) {
  const cleaned = String(text).replace(/,/g, '');
  const m = cleaned.match(/-?\d+(?:\.\d+)?(?:e[-+]?\d+)?/i);
  return m ? Number(m[0]) : NaN;
}

async function fetchFrame(page, step) {
  const res = await page.request.get(`/run?time=${step}`);
  expect(res.ok()).toBeTruthy();
  return res.json();
}

async function fetchFrameSteps(page) {
  const res = await page.request.get('/frames');
  expect(res.ok()).toBeTruthy();
  const body = await res.json();
  return body.steps;
}

/** Read a single pixel from the canvas's backing bitmap (1 device pixel per
 * grid cell, independent of however CSS has scaled the element on screen). */
async function canvasPixel(page, x, y) {
  return page.evaluate(
    ([px, py]) => {
      const canvas = document.getElementById('heatCanvas');
      const ctx = canvas.getContext('2d');
      const d = ctx.getImageData(px, py, 1, 1).data;
      return [d[0], d[1], d[2]];
    },
    [x, y],
  );
}

/** Parse the legend key's CSS gradient into {percent, rgb} stops, exactly as
 * the browser will render it - this is standard CSS gradient syntax, not
 * anything specific to this app. */
async function gradientStops(page) {
  const bg = await page
    .locator('#gradientBar')
    .evaluate((el) => getComputedStyle(el).backgroundImage);
  const re = /rgba?\(([^)]+)\)\s*([\d.]+)%/g;
  const stops = [];
  let m;
  while ((m = re.exec(bg))) {
    const rgb = m[1].split(',').slice(0, 3).map((s) => parseFloat(s));
    stops.push({ percent: parseFloat(m[2]), rgb });
  }
  return stops.sort((a, b) => a.percent - b.percent);
}

/** Linearly interpolate an RGB colour between two CSS gradient stops at
 * `percent` - the same interpolation a browser performs when painting a
 * linear-gradient between the stops it was given. */
function interpolateStops(stops, percent) {
  const p = Math.min(100, Math.max(0, percent));
  for (let i = 0; i < stops.length - 1; i++) {
    const a = stops[i];
    const b = stops[i + 1];
    if (p >= a.percent && p <= b.percent) {
      const frac = b.percent === a.percent ? 0 : (p - a.percent) / (b.percent - a.percent);
      return a.rgb.map((v, idx) => v + (b.rgb[idx] - v) * frac);
    }
  }
  return stops[stops.length - 1].rgb;
}

/**
 * Where a grid is actually painted inside the canvas ELEMENT's box, given
 * `object-fit: contain` letterboxing - the same generic CSS geometry
 * paintedRect() in script.js implements, needed here only to know where to
 * move the mouse, not to duplicate any app-specific business logic.
 */
function paintedRect(box, rows, cols) {
  const aspect = cols / rows;
  let w = box.width;
  let h = box.height;
  if (w / h > aspect) w = h * aspect;
  else h = w / aspect;
  return {
    left: box.x + (box.width - w) / 2,
    top: box.y + (box.height - h) / 2,
    width: w,
    height: h,
  };
}

module.exports = {
  DEFAULTS,
  runSimulation,
  setSources,
  setMaterials,
  setInsulated,
  parseLeadingNumber,
  fetchFrame,
  fetchFrameSteps,
  canvasPixel,
  gradientStops,
  interpolateStops,
  paintedRect,
};
