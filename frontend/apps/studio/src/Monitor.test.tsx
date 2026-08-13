import { fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { I18nContext, translate } from './i18n'
import { Monitor } from './Monitor'
import type { ConnectionProfile, MonitorEvent, OpcUaMonitor, UaNode } from './domain'

class ImmediateMonitor implements OpcUaMonitor {
  private listeners = new Set<(event: MonitorEvent) => void>()

  private emit(event: MonitorEvent) {
    this.listeners.forEach((listener) => listener(event))
  }

  async connect(profile: ConnectionProfile) {
    this.emit({ type: 'status', status: 'connected', message: profile.endpointUrl })
  }

  async disconnect() {
    this.emit({ type: 'status', status: 'disconnected' })
  }

  async browse() {
    const nodes: UaNode[] = [{
      nodeId: 'ns=2;s=Plant',
      browseName: 'Plant',
      nodeClass: 'Object',
      children: [
        { nodeId: 'ns=2;s=Plant/Tank1/Level', browseName: 'Level', nodeClass: 'Variable', dataType: 'Float' },
      ],
    }]
    this.emit({ type: 'browse', nodes })
  }

  async subscribe(nodeIds: string[]) {
    this.emit({
      type: 'value',
      value: {
        nodeId: nodeIds[0],
        browseName: 'Level',
        value: 12.5,
        quality: 'Good',
        sourceTimestamp: new Date().toISOString(),
        serverTimestamp: new Date().toISOString(),
      },
    })
  }

  onEvent(listener: (event: MonitorEvent) => void) {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }
}

describe('Monitor', () => {
  it('connects, browses, and shows subscribed values', async () => {
    const monitor = new ImmediateMonitor()
    render(
      <I18nContext.Provider value={{ locale: 'en', t: (key) => translate('en', key) }}>
        <Monitor monitor={monitor} />
      </I18nContext.Provider>,
    )

    fireEvent.click(screen.getByRole('button', { name: 'Connect' }))
    expect(await screen.findByText('Connected')).toBeVisible()

    fireEvent.click(screen.getByRole('button', { name: 'Browse' }))
    expect(await screen.findByLabelText('Select Level')).toBeInTheDocument()

    fireEvent.click(screen.getByLabelText('Select Level'))
    fireEvent.click(screen.getByRole('button', { name: 'Subscribe selection' }))
    expect(await screen.findByText('12.5')).toBeInTheDocument()
    expect(screen.getByText('Good')).toBeInTheDocument()

    fireEvent.click(screen.getByRole('button', { name: 'Disconnect' }))
    expect(await screen.findByText('Disconnected')).toBeVisible()
  })
})
