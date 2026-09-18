// Each simulation mode should run to completion and report a non-error
// status. Grids and step counts are kept small purely for test speed; the
// physics themselves are already covered by ../../../tests.cpp.
const { test, expect } = require('@playwright/test');
const { runSimulation, fetchFrameSteps } = require('./helpers');

const MODES = [
  { mode: 'fdm', label: 'Explicit (forward Euler)' },
  { mode: 'be', label: 'Backward Euler' },
  { mode: 'cn', label: 'Crank-Nicolson' },
  { mode: 'pde', label: 'Analytical steady state' },
  { mode: 'fisher', label: 'Fisher-KPP', rdSteps: 300 },
  { mode: 'gray-scott', label: 'Gray-Scott', rdSteps: 300 },
];

for (const m of MODES) {
  test(`${m.label} runs and reports a non-error status`, async ({ page }) => {
    await page.goto('/');
    const { body, statusText, isError } = await runSimulation(page, {
      mode: m.mode,
      rows: 20,
      cols: 20,
      rdSteps: m.rdSteps,
    });

    expect(isError, `status was: ${statusText}`).toBe(false);
    expect(statusText).not.toMatch(/^Error:/);
    expect(body.status).toBe('success');
    // Every run owns its files; the id is how the timeline finds them again.
    expect(body.run_id).toMatch(/^[0-9a-f]{32}$/);
  });
}

test('two runs get different ids and separate frame lists', async ({ page }) => {
  await page.goto('/');

  const first = await runSimulation(page, { mode: 'fdm', rows: 20, cols: 20 });
  const firstSteps = await fetchFrameSteps(page, first.body.run_id);

  const second = await runSimulation(page, { mode: 'cn', rows: 12, cols: 12, F: 5 });
  const secondSteps = await fetchFrameSteps(page, second.body.run_id);

  expect(second.body.run_id).not.toBe(first.body.run_id);
  expect(firstSteps.length).toBeGreaterThan(0);
  expect(secondSteps.length).toBeGreaterThan(0);

  // The first run's frames survive the second: before per-run files, starting
  // a run wiped the previous one's database outright.
  expect(await fetchFrameSteps(page, first.body.run_id)).toEqual(firstSteps);
});
