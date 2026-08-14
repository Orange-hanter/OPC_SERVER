export type Transport = 'tcp' | 'udp'
export type RegisterArea = 'holding' | 'input' | 'coil' | 'discrete'
export type TagType = 'bool' | 'uint16' | 'int16' | 'uint32' | 'int32' | 'float32' | 'float64'
export type Quality = 'Good' | 'Uncertain' | 'Bad'

export interface OpcUaSettings {
  endpointUrl?: string
  applicationName?: string
  securityPolicy?: 'None' | 'Basic256Sha256'
  securityMode?: 'None' | 'Sign' | 'SignAndEncrypt'
  namespaceUri?: string
}

export interface Endpoint {
  id: string
  host: string
  port: number
  transport: Transport
  connectTimeoutMs?: number
  responseTimeoutMs?: number
  reconnectDelayMs?: number
}

export interface Tag {
  name: string
  nodePath?: string
  area: RegisterArea
  address: number
  type: TagType
  quantity?: number
  byteOrder?: 'ABCD' | 'CDAB' | 'BADC' | 'DCBA' | 'AB' | 'BA'
  scale?: number
  offset?: number
  unit?: string
  writable?: boolean
  group?: string
  description?: string
}

export interface Device {
  id: string
  endpointId: string
  unitId: number
  profileId?: string
  description?: string
  tags?: Tag[]
}

export interface RegisterBlock {
  area: RegisterArea
  start: number
  count: number
  description?: string
}

export interface PollGroup {
  id: string
  periodMs: number
  priority: 'fast' | 'normal' | 'slow'
  deviceId: string
  blocks?: RegisterBlock[]
  tagNames?: string[]
}

export interface Project {
  schemaVersion: number
  name: string
  description?: string
  addressBase?: 0 | 1
  opcua?: OpcUaSettings
  endpoints: Endpoint[]
  deviceProfiles?: Array<Record<string, unknown>>
  devices: Device[]
  pollGroups: PollGroup[]
}

export interface ProjectFile {
  path: string | null
  content: string
}

export interface ProjectFileAdapter {
  open(): Promise<ProjectFile | null>
  save(path: string | null, content: string): Promise<string | null>
  saveAs(content: string): Promise<string | null>
}

export interface ValidationIssue {
  severity: 'error' | 'warning'
  path: string
  message: string
  source: 'schema' | 'opc-map'
}

export interface ProjectValidator {
  validate(project: unknown, content: string): Promise<ValidationIssue[]>
}

export interface ConnectionProfile {
  endpointUrl: string
  securityPolicy: 'None' | 'Basic256Sha256'
  securityMode: 'None' | 'Sign' | 'SignAndEncrypt'
  username?: string
  password?: string
  certificatePath?: string
  privateKeyPath?: string
}

export interface UaNode {
  nodeId: string
  browseName: string
  nodeClass: 'Object' | 'Variable'
  dataType?: string
  children?: UaNode[]
}

export interface LiveValue {
  nodeId: string
  browseName: string
  value: unknown
  quality: Quality
  sourceTimestamp: string
  serverTimestamp: string
}

export type MonitorEvent =
  | { type: 'status'; status: 'disconnected' | 'connecting' | 'connected'; message?: string }
  | { type: 'browse'; nodes: UaNode[] }
  | { type: 'value'; value: LiveValue }
  | { type: 'diagnostic'; level: 'info' | 'warning' | 'error'; message: string; timestamp: string }

export interface OpcUaMonitor {
  connect(profile: ConnectionProfile): Promise<void>
  disconnect(): Promise<void>
  browse(nodeId?: string): Promise<void>
  subscribe(nodeIds: string[]): Promise<void>
  onEvent(listener: (event: MonitorEvent) => void): () => void
}

export const createEmptyProject = (): Project => ({
  schemaVersion: 1,
  name: 'untitled-project',
  description: '',
  addressBase: 0,
  opcua: {
    endpointUrl: 'opc.tcp://127.0.0.1:4840',
    applicationName: 'OPC_SERVER',
    securityPolicy: 'None',
    securityMode: 'None',
    namespaceUri: 'urn:opc-server:project',
  },
  endpoints: [{ id: 'endpoint-1', host: '127.0.0.1', port: 502, transport: 'tcp' }],
  devices: [{ id: 'device-1', endpointId: 'endpoint-1', unitId: 1, tags: [] }],
  pollGroups: [{ id: 'normal', periodMs: 1000, priority: 'normal', deviceId: 'device-1', blocks: [] }],
})
