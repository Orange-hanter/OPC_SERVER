import { useMemo, useState, type ReactNode } from 'react'
import type { Device, Endpoint, PollGroup, Project, Tag } from './domain'
import { useI18n } from './i18n'

type Section = 'overview' | 'opcua' | 'endpoints' | 'devices' | 'polling' | 'tree' | 'raw'

interface Props {
  project: Project | null
  content: string
  onProjectChange(project: Project): void
  onContentChange(content: string): void
}

const Field = ({ label, children }: { label: string; children: ReactNode }) => (
  <label className="field">
    <span>{label}</span>
    {children}
  </label>
)

const TextInput = ({ value, onChange, type = 'text' }: {
  value: string | number
  onChange(value: string): void
  type?: 'text' | 'number' | 'url'
}) => <input type={type} value={value} onChange={(event) => onChange(event.target.value)} />

export function ProjectEditor({ project, content, onProjectChange, onContentChange }: Props) {
  const { t } = useI18n()
  const [section, setSection] = useState<Section>('overview')
  const sections: Array<[Section, string]> = [
    ['overview', t('overview')], ['opcua', t('opcua')], ['endpoints', t('endpoints')],
    ['devices', t('devices')], ['polling', t('polling')], ['tree', t('tree')], ['raw', t('raw')],
  ]

  return (
    <div className="workspace">
      <nav className="section-tabs" aria-label={t('editor')}>
        {sections.map(([id, label]) => (
          <button key={id} className={section === id ? 'active' : ''} onClick={() => setSection(id)}>{label}</button>
        ))}
      </nav>
      <div className="editor-scroll">
        {section === 'raw'
          ? <RawEditor content={content} onChange={onContentChange} />
          : project
            ? <SectionContent section={section} project={project} onChange={onProjectChange} />
            : <div className="empty-state"><strong>{t('jsonError')}</strong><p>{t('raw')}</p></div>}
      </div>
    </div>
  )
}

function SectionContent({ section, project, onChange }: { section: Exclude<Section, 'raw'>; project: Project; onChange(project: Project): void }) {
  switch (section) {
    case 'overview': return <Overview project={project} onChange={onChange} />
    case 'opcua': return <OpcUa project={project} onChange={onChange} />
    case 'endpoints': return <Endpoints project={project} onChange={onChange} />
    case 'devices': return <Devices project={project} onChange={onChange} />
    case 'polling': return <Polling project={project} onChange={onChange} />
    case 'tree': return <TreePreview project={project} />
  }
}

function Overview({ project, onChange }: { project: Project; onChange(project: Project): void }) {
  const { t } = useI18n()
  return <section className="form-page" aria-labelledby="overview-title">
    <header><p className="eyebrow">PROJECT</p><h2 id="overview-title">{t('overview')}</h2></header>
    <div className="form-grid">
      <Field label={t('name')}><TextInput value={project.name} onChange={(name) => onChange({ ...project, name })} /></Field>
      <Field label={t('schemaVersion')}><TextInput type="number" value={project.schemaVersion} onChange={(value) => onChange({ ...project, schemaVersion: Number(value) })} /></Field>
      <Field label={t('addressBase')}><select value={project.addressBase ?? 0} onChange={(event) => onChange({ ...project, addressBase: Number(event.target.value) as 0 | 1 })}><option value="0">0</option><option value="1">1</option></select></Field>
      <Field label={t('description')}><textarea value={project.description ?? ''} onChange={(event) => onChange({ ...project, description: event.target.value })} /></Field>
    </div>
  </section>
}

function OpcUa({ project, onChange }: { project: Project; onChange(project: Project): void }) {
  const settings = project.opcua ?? {}
  const set = (patch: Partial<typeof settings>) => onChange({ ...project, opcua: { ...settings, ...patch } })
  return <section className="form-page"><header><p className="eyebrow">NORTHBOUND</p><h2>OPC UA</h2></header><div className="form-grid">
    <Field label="Endpoint URL"><TextInput type="url" value={settings.endpointUrl ?? ''} onChange={(endpointUrl) => set({ endpointUrl })} /></Field>
    <Field label="Application name"><TextInput value={settings.applicationName ?? ''} onChange={(applicationName) => set({ applicationName })} /></Field>
    <Field label="Namespace URI"><TextInput value={settings.namespaceUri ?? ''} onChange={(namespaceUri) => set({ namespaceUri })} /></Field>
    <Field label="Security policy"><select value={settings.securityPolicy ?? 'None'} onChange={(e) => set({ securityPolicy: e.target.value as 'None' | 'Basic256Sha256' })}><option>None</option><option>Basic256Sha256</option></select></Field>
    <Field label="Security mode"><select value={settings.securityMode ?? 'None'} onChange={(e) => set({ securityMode: e.target.value as 'None' | 'Sign' | 'SignAndEncrypt' })}><option>None</option><option>Sign</option><option>SignAndEncrypt</option></select></Field>
  </div></section>
}

function Endpoints({ project, onChange }: { project: Project; onChange(project: Project): void }) {
  const { t } = useI18n()
  const update = (index: number, patch: Partial<Endpoint>) => onChange({ ...project, endpoints: project.endpoints.map((item, i) => i === index ? { ...item, ...patch } : item) })
  const remove = (index: number) => onChange({ ...project, endpoints: project.endpoints.filter((_, i) => i !== index) })
  return <section className="form-page"><PageHeader eyebrow="MODBUS" title={t('endpoints')} onAdd={() => onChange({ ...project, endpoints: [...project.endpoints, { id: `endpoint-${project.endpoints.length + 1}`, host: '127.0.0.1', port: 502, transport: 'tcp' }] })} />
    <div className="card-list">{project.endpoints.map((endpoint, index) => <article className="data-card" key={`${endpoint.id}-${index}`}><div className="card-title"><strong>{endpoint.id || `#${index + 1}`}</strong><button className="danger ghost" onClick={() => remove(index)}>{t('remove')}</button></div><div className="compact-grid">
      <Field label="ID"><TextInput value={endpoint.id} onChange={(id) => update(index, { id })} /></Field>
      <Field label="Host"><TextInput value={endpoint.host} onChange={(host) => update(index, { host })} /></Field>
      <Field label="Port"><TextInput type="number" value={endpoint.port} onChange={(port) => update(index, { port: Number(port) })} /></Field>
      <Field label="Transport"><select value={endpoint.transport} onChange={(e) => update(index, { transport: e.target.value as 'tcp' | 'udp' })}><option>tcp</option><option>udp</option></select></Field>
      <Field label="Response timeout, ms"><TextInput type="number" value={endpoint.responseTimeoutMs ?? 1000} onChange={(value) => update(index, { responseTimeoutMs: Number(value) })} /></Field>
      <Field label="Reconnect delay, ms"><TextInput type="number" value={endpoint.reconnectDelayMs ?? 2000} onChange={(value) => update(index, { reconnectDelayMs: Number(value) })} /></Field>
    </div></article>)}</div>
  </section>
}

function Devices({ project, onChange }: { project: Project; onChange(project: Project): void }) {
  const { t } = useI18n()
  const [expanded, setExpanded] = useState(0)
  const updateDevice = (index: number, patch: Partial<Device>) => onChange({ ...project, devices: project.devices.map((item, i) => i === index ? { ...item, ...patch } : item) })
  const updateTag = (deviceIndex: number, tagIndex: number, patch: Partial<Tag>) => {
    const device = project.devices[deviceIndex]
    const tags = (device.tags ?? []).map((tag, i) => i === tagIndex ? { ...tag, ...patch } : tag)
    updateDevice(deviceIndex, { tags })
  }
  return <section className="form-page"><PageHeader eyebrow="MODBUS" title={t('devices')} onAdd={() => onChange({ ...project, devices: [...project.devices, { id: `device-${project.devices.length + 1}`, endpointId: project.endpoints[0]?.id ?? '', unitId: 1, tags: [] }] })} />
    <div className="card-list">{project.devices.map((device, deviceIndex) => <article className="data-card" key={`${device.id}-${deviceIndex}`}>
      <button className="device-heading" onClick={() => setExpanded(expanded === deviceIndex ? -1 : deviceIndex)} aria-expanded={expanded === deviceIndex}><span><strong>{device.id}</strong><small>{device.tags?.length ?? 0} tags · Unit {device.unitId}</small></span><span>{expanded === deviceIndex ? '−' : '+'}</span></button>
      {expanded === deviceIndex && <div className="device-body"><div className="compact-grid">
        <Field label="ID"><TextInput value={device.id} onChange={(id) => updateDevice(deviceIndex, { id })} /></Field>
        <Field label="Endpoint"><select value={device.endpointId} onChange={(e) => updateDevice(deviceIndex, { endpointId: e.target.value })}>{project.endpoints.map((endpoint) => <option key={endpoint.id}>{endpoint.id}</option>)}</select></Field>
        <Field label="Unit ID"><TextInput type="number" value={device.unitId} onChange={(unitId) => updateDevice(deviceIndex, { unitId: Number(unitId) })} /></Field>
        <Field label={t('description')}><TextInput value={device.description ?? ''} onChange={(description) => updateDevice(deviceIndex, { description })} /></Field>
      </div><div className="subheading"><h3>Tags</h3><button onClick={() => updateDevice(deviceIndex, { tags: [...(device.tags ?? []), { name: `Tag${(device.tags?.length ?? 0) + 1}`, area: 'holding', address: 0, type: 'uint16', writable: false }] })}>+ {t('add')}</button></div>
      <div className="tag-table-wrap"><table><thead><tr><th>Name</th><th>Node path</th><th>Area</th><th>Address</th><th>Type</th><th>Unit</th><th>R/W</th><th /></tr></thead><tbody>{(device.tags ?? []).map((tag, tagIndex) => <tr key={`${tag.name}-${tagIndex}`}>
        <td><input aria-label="Tag name" value={tag.name} onChange={(e) => updateTag(deviceIndex, tagIndex, { name: e.target.value })} /></td>
        <td><input aria-label="Node path" value={tag.nodePath ?? ''} onChange={(e) => updateTag(deviceIndex, tagIndex, { nodePath: e.target.value })} /></td>
        <td><select aria-label="Area" value={tag.area} onChange={(e) => updateTag(deviceIndex, tagIndex, { area: e.target.value as Tag['area'] })}><option>holding</option><option>input</option><option>coil</option><option>discrete</option></select></td>
        <td><input aria-label="Address" type="number" value={tag.address} onChange={(e) => updateTag(deviceIndex, tagIndex, { address: Number(e.target.value) })} /></td>
        <td><select aria-label="Type" value={tag.type} onChange={(e) => updateTag(deviceIndex, tagIndex, { type: e.target.value as Tag['type'] })}>{['bool', 'uint16', 'int16', 'uint32', 'int32', 'float32', 'float64'].map((type) => <option key={type}>{type}</option>)}</select></td>
        <td><input aria-label="Unit" value={tag.unit ?? ''} onChange={(e) => updateTag(deviceIndex, tagIndex, { unit: e.target.value })} /></td>
        <td><input aria-label="Writable" type="checkbox" checked={tag.writable ?? false} onChange={(e) => updateTag(deviceIndex, tagIndex, { writable: e.target.checked })} /></td>
        <td><button className="icon danger ghost" aria-label={t('remove')} onClick={() => updateDevice(deviceIndex, { tags: (device.tags ?? []).filter((_, i) => i !== tagIndex) })}>×</button></td>
      </tr>)}</tbody></table></div></div>}
    </article>)}</div>
  </section>
}

function Polling({ project, onChange }: { project: Project; onChange(project: Project): void }) {
  const { t } = useI18n()
  const update = (index: number, patch: Partial<PollGroup>) => onChange({ ...project, pollGroups: project.pollGroups.map((item, i) => i === index ? { ...item, ...patch } : item) })
  return <section className="form-page"><PageHeader eyebrow="SCHEDULING" title={t('polling')} onAdd={() => onChange({ ...project, pollGroups: [...project.pollGroups, { id: `group-${project.pollGroups.length + 1}`, deviceId: project.devices[0]?.id ?? '', periodMs: 1000, priority: 'normal', blocks: [] }] })} />
    <div className="card-list">{project.pollGroups.map((group, index) => <article className="data-card" key={`${group.id}-${index}`}><div className="card-title"><strong>{group.id}</strong><button className="danger ghost" onClick={() => onChange({ ...project, pollGroups: project.pollGroups.filter((_, i) => i !== index) })}>{t('remove')}</button></div><div className="compact-grid">
      <Field label="ID"><TextInput value={group.id} onChange={(id) => update(index, { id })} /></Field>
      <Field label="Device"><select value={group.deviceId} onChange={(e) => update(index, { deviceId: e.target.value })}>{project.devices.map((device) => <option key={device.id}>{device.id}</option>)}</select></Field>
      <Field label="Period, ms"><TextInput type="number" value={group.periodMs} onChange={(periodMs) => update(index, { periodMs: Number(periodMs) })} /></Field>
      <Field label="Priority"><select value={group.priority} onChange={(e) => update(index, { priority: e.target.value as PollGroup['priority'] })}><option>fast</option><option>normal</option><option>slow</option></select></Field>
      <Field label="Tag names (comma separated)"><TextInput value={group.tagNames?.join(', ') ?? ''} onChange={(value) => update(index, { tagNames: value.split(',').map((item) => item.trim()).filter(Boolean), blocks: undefined })} /></Field>
    </div></article>)}</div>
  </section>
}

function TreePreview({ project }: { project: Project }) {
  const tree = useMemo(() => {
    const root: Record<string, Record<string, unknown>> = {}
    project.devices.flatMap((device) => device.tags ?? []).forEach((tag) => {
      const segments = (tag.nodePath || tag.name).split('/').filter(Boolean)
      let current = root
      segments.forEach((segment) => {
        current[segment] ??= {}
        current = current[segment] as Record<string, Record<string, unknown>>
      })
    })
    return root
  }, [project])
  const render = (node: Record<string, unknown>): ReactNode => <ul>{Object.entries(node).map(([name, children]) => <li key={name}><span className={Object.keys(children as object).length ? 'folder' : 'variable'}>{name}</span>{Object.keys(children as object).length > 0 && render(children as Record<string, unknown>)}</li>)}</ul>
  return <section className="form-page"><header><p className="eyebrow">OBJECTS</p><h2>OPC UA tree</h2></header><div className="tree-panel"><strong>Root / Objects</strong>{render(tree)}</div></section>
}

function RawEditor({ content, onChange }: { content: string; onChange(value: string): void }) {
  return <section className="raw-page"><div className="code-header"><span>project.modbusproj.json</span><span>{content.split('\n').length} lines</span></div><textarea aria-label="Project JSON" spellCheck={false} value={content} onChange={(event) => onChange(event.target.value)} /></section>
}

function PageHeader({ eyebrow, title, onAdd }: { eyebrow: string; title: string; onAdd(): void }) {
  const { t } = useI18n()
  return <header className="page-header"><div><p className="eyebrow">{eyebrow}</p><h2>{title}</h2></div><button className="primary" onClick={onAdd}>+ {t('add')}</button></header>
}
