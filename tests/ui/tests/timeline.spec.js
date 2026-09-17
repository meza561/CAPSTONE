// The slider must index the frames /frames actually reports, not a rounded
// number of seconds, and the label it shows must be the frame it is
// actually displaying - see the "move the timeline under the plate" commit.
const { test, expect } = require('@playwright/test');
const { runSimulation, fetchFrameSteps, parseLeadingNumber } = require('./helpers');

test('time slider indexes real stored frames and its label matches them', async ({ page }) => {
  await page.goto('/');
  const { body } = await runSimulation(page, {
    mode: 'fdm', rows: 20, cols: 20, top: 100, bottom: 0, left: 0, right: 0, alpha: 0.01,
  });
  const dt = body.data.dt;

  const steps = await fetchFrameSteps(page);
  expect(steps.length).toBeGreaterThan(2); // otherwise this test proves nothing

  const slider = page.locator('#timeSlider');
  await expect(slider).toHaveAttribute('max', String(steps.length - 1));

  // The slider should already be parked on the last frame after a run.
  await expect(page.locator('#frameVal')).toHaveText(
    new RegExp(`frame ${steps.length} of ${steps.length}\\s*[—-]\\s*step ${steps[steps.length - 1]}$`),
  );

  for (const idx of [0, Math.floor(steps.length / 2), steps.length - 1]) {
    await slider.evaluate((el, v) => {
      el.value = String(v);
      el.dispatchEvent(new Event('input', { bubbles: true }));
    }, idx);

    const expectedStep = steps[idx];
    await expect(page.locator('#frameVal')).toHaveText(
      new RegExp(`frame ${idx + 1} of ${steps.length}\\s*[—-]\\s*step ${expectedStep}$`),
    );

    // The displayed time must correspond to *this* frame's real step, not
    // whatever frame was showing before the drag.
    const shown = parseLeadingNumber(await page.locator('#timeVal').textContent());
    const expectedTime = expectedStep * dt;
    expect(shown).toBeCloseTo(expectedTime, 0);
  }
});
