import Ajv2020, { type ErrorObject } from 'ajv/dist/2020.js'
import schema from '../../../../DOCs/schemas/modbus-project.schema.json'
import type {
  ConnectionProfile,
  LiveValue,
  MonitorEvent,
  OpcUaMonitor,
  ProjectFile,
  ProjectFileAdapter,
  ProjectValidator,
  UaNode,
  ValidationIssue,
} from './domain'

type WritableHandle = {
  name: string
  getFile(): Promise<File>
  createWritable(): Promise<{ write(content: string): Promise<void>; close(): Promise<void> }>
}

type FilePickerWindow = Window & {
  showOpenFilePicker?: (options: unknown) => Promise<WritableHandle[]>
  showSaveFilePicker?: (options: unknown) => Promise<WritableHandle>
  __TAURI_INTERNALS__?: unknown
}

const pickerOptions = {
  types: [{
    description: 'Modbus project',
    accept: { 'application/json': ['.modbusproj.json', '.json'] },
  }],
  excludeAcceptAllOption: false,
}

const isTauri = () => Boolean((window as FilePickerWindow).__TAURI_INTERNALS__)

class BrowserProjectFileAdapter implements ProjectFileAdapter {
  private handle: WritableHandle | null = null

  async open(): Promise<ProjectFile | null> {
    const browser = window as FilePickerWindow
    if (browser.showOpenFilePicker) {
      const [handle] = await browser.showOpenFilePicker(pickerOptions)
      this.handle = handle
      const file = await handle.getFile()
      return { path: handle.name, content: await file.text() }
    }

    return new Promise((resolve) => {
      const input = document.createElement('input')
      input.type = 'file'
      input.accept = '.json,.modbusproj.json,application/json'
      input.onchange = async () => {
        const file = input.files?.[0]
        resolve(file ? { path: file.name, content: await file.text() } : null)
      }
      input.oncancel = () => resolve(null)
      input.click()
    })
  }

  async save(path: string | null, content: string): Promise<string | null> {
    if (!path || !this.handle) return this.saveAs(content)
    const writer = await this.handle.createWritable()
    await writer.write(content)
    await writer.close()
    return path
  }

  async saveAs(content: string): Promise<string | null> {
    const browser = window as FilePickerWindow
    if (browser.showSaveFilePicker) {
      const handle = await browser.showSaveFilePicker({
        ...pickerOptions,
        suggestedName: this.handle?.name ?? 'project.modbusproj.json',
      })
      const writer = await handle.createWritable()
      await writer.write(content)
      await writer.close()
      this.handle = handle
      return handle.name
    }

    const blobUrl = URL.createObjectURL(new Blob([content], { type: 'application/json' }))
    const anchor = document.createElement('a')
    anchor.href = blobUrl
    anchor.download = 'project.modbusproj.json'
    anchor.click()
    URL.revokeObjectURL(blobUrl)
    return anchor.download
  }
}

class TauriProjectFileAdapter implements ProjectFileAdapter {
  async open(): Promise<ProjectFile | null> {
    const [{ open }, { invoke }] = await Promise.all([
      import('@tauri-apps/plugin-dialog'),
      import('@tauri-apps/api/core'),
    ])
    const path = await open({
      multiple: false,
      filters: [{ name: 'Modbus project', extensions: ['modbusproj.json', 'json'] }],
    })
    return path ? invoke<ProjectFile>('read_project', { path }) : null
  }

  async save(path: string | null, content: string): Promise<string | null> {
    if (!path) return this.saveAs(content)
    const { invoke } = await import('@tauri-apps/api/core')
    return invoke<string>('write_project', { path, content })
  }

  async saveAs(content: string): Promise<string | null> {
    const [{ save }, { invoke }] = await Promise.all([
      import('@tauri-apps/plugin-dialog'),
      import('@tauri-apps/api/core'),
    ])
    const path = await save({
      defaultPath: 'project.modbusproj.json',
      filters: [{ name: 'Modbus project', extensions: ['modbusproj.json', 'json'] }],
    })
    return path ? invoke<string>('write_project', { path, content }) : null
  }
}

const ajv = new Ajv2020({ allErrors: true, strict: false })
const validateSchema = ajv.compile(schema)

function schemaIssues(project: unknown): ValidationIssue[] {
  validateSchema(project)
  return (validateSchema.errors ?? []).map((error: ErrorObject) => ({
    severity: 'error',
    path: error.instancePath || '/',
    message: error.message ?? 'Schema validation failed',
    source: 'schema',
  }))
}

class BrowserProjectValidator implements ProjectValidator {
  async validate(project: unknown): Promise<ValidationIssue[]> {
    return schemaIssues(project)
  }
}

class TauriProjectValidator implements ProjectValidator {
  async validate(project: unknown, content: string): Promise<ValidationIssue[]> {
    const issues = schemaIssues(project)
    if (issues.length) return issues
    const { invoke } = await import('@tauri-apps/api/core')
    const native = await invoke<ValidationIssue[]>('validate_project', { content })
    return [...issues, ...native]
  }
}

const mockTree: UaNode[] = [{
  nodeId: 'ns=2;s=Plant',
  browseName: 'Plant',
  nodeClass: 'Object',
  children: [
    { nodeId: 'ns=2;s=Plant/Tank1/Level', browseName: 'Level', nodeClass: 'Variable', dataType: 'Float' },
    { nodeId: 'ns=2;s=Plant/Tank1/Temperature', browseName: 'Temperature', nodeClass: 'Variable', dataType: 'Float' },
    { nodeId: 'ns=2;s=Plant/Pump1/Running', browseName: 'Running', nodeClass: 'Variable', dataType: 'Boolean' },
  ],
}]

class MockOpcUaMonitor implements OpcUaMonitor {
  private listeners = new Set<(event: MonitorEvent) => void>()
  private timer: number | undefined
  private subscriptions: string[] = []

  private emit(event: MonitorEvent) {
    this.listeners.forEach((listener) => listener(event))
  }

  async connect(profile: ConnectionProfile) {
    this.emit({ type: 'status', status: 'connecting' })
    await new Promise((resolve) => window.setTimeout(resolve, 180))
    this.emit({ type: 'status', status: 'connected', message: profile.endpointUrl })
    this.emit({ type: 'diagnostic', level: 'info', message: `Mock session: ${profile.endpointUrl}`, timestamp: new Date().toISOString() })
    this.timer = window.setInterval(() => {
      this.subscriptions.forEach((nodeId, index) => {
        const now = new Date().toISOString()
        const value: LiveValue = {
          nodeId,
          browseName: nodeId.split('/').at(-1) ?? nodeId,
          value: nodeId.endsWith('Running') ? Math.random() > 0.5 : Number((20 + Math.sin(Date.now() / 1500 + index) * 8).toFixed(2)),
          quality: Math.random() > 0.04 ? 'Good' : 'Uncertain',
          sourceTimestamp: now,
          serverTimestamp: now,
        }
        this.emit({ type: 'value', value })
      })
    }, 700)
  }

  async disconnect() {
    if (this.timer) window.clearInterval(this.timer)
    this.emit({ type: 'status', status: 'disconnected' })
  }

  async browse() {
    this.emit({ type: 'browse', nodes: mockTree })
  }

  async subscribe(nodeIds: string[]) {
    this.subscriptions = nodeIds
    this.emit({ type: 'diagnostic', level: 'info', message: `Subscribed: ${nodeIds.length}`, timestamp: new Date().toISOString() })
  }

  onEvent(listener: (event: MonitorEvent) => void) {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }
}

class TauriOpcUaMonitor implements OpcUaMonitor {
  private listeners = new Set<(event: MonitorEvent) => void>()
  private unlisten: (() => void) | null = null

  private async ensureListener() {
    if (this.unlisten) return
    const { listen } = await import('@tauri-apps/api/event')
    this.unlisten = await listen<MonitorEvent>('opc-monitor://event', ({ payload }) => {
      this.listeners.forEach((listener) => listener(payload))
    })
  }

  async connect(profile: ConnectionProfile) {
    await this.ensureListener()
    const { invoke } = await import('@tauri-apps/api/core')
    await invoke('monitor_connect', { profile })
  }

  async disconnect() {
    const { invoke } = await import('@tauri-apps/api/core')
    await invoke('monitor_disconnect')
  }

  async browse(nodeId?: string) {
    const { invoke } = await import('@tauri-apps/api/core')
    await invoke('monitor_browse', { nodeId: nodeId ?? null })
  }

  async subscribe(nodeIds: string[]) {
    const { invoke } = await import('@tauri-apps/api/core')
    await invoke('monitor_subscribe', { nodeIds })
  }

  onEvent(listener: (event: MonitorEvent) => void) {
    this.listeners.add(listener)
    void this.ensureListener()
    return () => this.listeners.delete(listener)
  }
}

export const createProjectFileAdapter = (): ProjectFileAdapter =>
  isTauri() ? new TauriProjectFileAdapter() : new BrowserProjectFileAdapter()

export const createProjectValidator = (): ProjectValidator =>
  isTauri() ? new TauriProjectValidator() : new BrowserProjectValidator()

export const createOpcUaMonitor = (): OpcUaMonitor =>
  isTauri() ? new TauriOpcUaMonitor() : new MockOpcUaMonitor()

export { BrowserProjectValidator, MockOpcUaMonitor }
