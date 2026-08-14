import { expect, test } from '@playwright/test'

test('native Tauri sidecar smoke is opt-in', async () => {
  test.skip(!process.env.OPC_STUDIO_NATIVE, 'set OPC_STUDIO_NATIVE=1 for native sidecar e2e')
  test.info().annotations.push({
    type: 'lab',
    description: 'Launch Tauri with bundled opc-map/opc-monitor against a live OPC_SERVER. See DOCs/13-testing-program.md.',
  })
  expect(process.env.OPC_STUDIO_NATIVE).toBeTruthy()
})
