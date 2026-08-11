import { describe, expect, it, vi } from 'vitest'
import { BrowserProjectValidator, MockOpcUaMonitor } from './adapters'
import { createEmptyProject, type MonitorEvent } from './domain'

describe('BrowserProjectValidator', () => {
  it('accepts a project conforming to the documentation schema', async () => {
    const issues = await new BrowserProjectValidator().validate(createEmptyProject())
    expect(issues).toEqual([])
  })

  it('reports schema paths and all missing required fields', async () => {
    const issues = await new BrowserProjectValidator().validate({ schemaVersion: 1, name: 'broken' })
    expect(issues.map((issue) => issue.message)).toEqual(expect.arrayContaining([
      "must have required property 'endpoints'",
      "must have required property 'devices'",
      "must have required property 'pollGroups'",
    ]))
    expect(issues.every((issue) => issue.source === 'schema')).toBe(true)
  })
})

describe('MockOpcUaMonitor', () => {
  it('emits browsed nodes and subscribed live values', async () => {
    vi.useFakeTimers()
    const monitor = new MockOpcUaMonitor()
    const events: MonitorEvent[] = []
    monitor.onEvent((event) => events.push(event))

    const connecting = monitor.connect({
      endpointUrl: 'opc.tcp://127.0.0.1:4840',
      securityPolicy: 'None',
      securityMode: 'None',
    })
    await vi.advanceTimersByTimeAsync(180)
    await connecting
    await monitor.browse()
    await monitor.subscribe(['ns=2;s=Plant/Tank1/Level'])
    await vi.advanceTimersByTimeAsync(700)

    expect(events).toContainEqual(expect.objectContaining({ type: 'status', status: 'connected' }))
    expect(events).toContainEqual(expect.objectContaining({ type: 'browse' }))
    expect(events).toContainEqual(expect.objectContaining({
      type: 'value',
      value: expect.objectContaining({ nodeId: 'ns=2;s=Plant/Tank1/Level' }),
    }))
    await monitor.disconnect()
    vi.useRealTimers()
  })
})
