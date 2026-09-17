// Each simulation mode should run to completion and report a non-error
// status. Grids and step counts are kept small purely for test speed; the
// physics themselves are already covered by ../../../tests.cpp.
const { test, expect } = require('@playwright/test');
const { runSimulation } = require('./helpers');

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
  });
}
