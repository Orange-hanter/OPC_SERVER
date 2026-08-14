import { useEffect, useMemo, useState } from 'react'
import type { ConnectionProfile, LiveValue, MonitorEvent, OpcUaMonitor, UaNode } from './domain'
import { useI18n } from './i18n'

const MAX_EVENTS = 500

interface Props {
  monitor: OpcUaMonitor
  defaultEndpoint?: string
}

export function Monitor({ monitor, defaultEndpoint }: Props) {
  const { t } = useI18n()
  const [profile, setProfile] = useState<ConnectionProfile>({
    endpointUrl: defaultEndpoint ?? 'opc.tcp://127.0.0.1:4840',
    securityPolicy: 'None',
    securityMode: 'None',
  })
  const [status, setStatus] = useState<'disconnected' | 'connecting' | 'connected'>('disconnected')
  const [nodes, setNodes] = useState<UaNode[]>([])
  const [selected, setSelected] = useState<Set<string>>(new Set())
  const [values, setValues] = useState<Map<string, LiveValue>>(new Map())
  const [events, setEvents] = useState<MonitorEvent[]>([])
  const [paused, setPaused] = useState(false)
  const [filter, setFilter] = useState('')

  useEffect(() => monitor.onEvent((event) => {
    if (event.type === 'status') setStatus(event.status)
    if (event.type === 'browse') setNodes(event.nodes)
    if (!paused && event.type === 'value') {
      setValues((current) => new Map(current).set(event.value.nodeId, event.value))
    }
    setEvents((current) => [...current, event].slice(-MAX_EVENTS))
  }), [monitor, paused])

  useEffect(() => {
    if (defaultEndpoint) setProfile((current) => ({ ...current, endpointUrl: defaultEndpoint }))
  }, [defaultEndpoint])

  const diagnostics = events.filter((event): event is Extract<MonitorEvent, { type: 'diagnostic' }> => event.type === 'diagnostic')
  const filteredValues = [...values.values()].filter((value) =>
    `${value.browseName} ${value.nodeId} ${value.quality}`.toLowerCase().includes(filter.toLowerCase()),
  )

  const run = (operation: () => Promise<void>) => void operation().catch((error: unknown) => {
    const diagnostic: MonitorEvent = {
      type: 'diagnostic',
      level: 'error',
      message: error instanceof Error ? error.message : String(error),
      timestamp: new Date().toISOString(),
    }
    setEvents((current) => [...current, diagnostic].slice(-MAX_EVENTS))
  })

  const exportEvents = () => {
    const data = events.map((event) => JSON.stringify(event)).join('\n')
    const url = URL.createObjectURL(new Blob([data], { type: 'application/x-ndjson' }))
    const anchor = document.createElement('a')
    anchor.href = url
    anchor.download = `opc-monitor-${new Date().toISOString().replaceAll(':', '-')}.jsonl`
    anchor.click()
    URL.revokeObjectURL(url)
  }

  return <div className="monitor-layout">
    <aside className="connection-panel">
      <div className="panel-heading"><p className="eyebrow">SESSION</p><h2>Connection</h2></div>
      <label className="field"><span>Endpoint URL</span><input value={profile.endpointUrl} onChange={(e) => setProfile({ ...profile, endpointUrl: e.target.value })} /></label>
      <label className="field"><span>Security policy</span><select value={profile.securityPolicy} onChange={(e) => setProfile({ ...profile, securityPolicy: e.target.value as ConnectionProfile['securityPolicy'] })}><option>None</option><option>Basic256Sha256</option></select></label>
      <label className="field"><span>Security mode</span><select value={profile.securityMode} onChange={(e) => setProfile({ ...profile, securityMode: e.target.value as ConnectionProfile['securityMode'] })}><option>None</option><option>Sign</option><option>SignAndEncrypt</option></select></label>
      <label className="field"><span>Client certificate</span><input placeholder="optional DER/PEM path" value={profile.certificatePath ?? ''} onChange={(e) => setProfile({ ...profile, certificatePath: e.target.value })} /></label>
      <label className="field"><span>Private key</span><input placeholder="optional key path" value={profile.privateKeyPath ?? ''} onChange={(e) => setProfile({ ...profile, privateKeyPath: e.target.value })} /></label>
      <label className="field"><span>User certificate</span><input placeholder="X509IdentityToken cert path" value={profile.userCertificatePath ?? ''} onChange={(e) => setProfile({ ...profile, userCertificatePath: e.target.value })} /></label>
      <label className="field"><span>User private key</span><input placeholder="X509IdentityToken key path" value={profile.userPrivateKeyPath ?? ''} onChange={(e) => setProfile({ ...profile, userPrivateKeyPath: e.target.value })} /></label>
      <label className="field"><span>Username</span><input autoComplete="username" value={profile.username ?? ''} onChange={(e) => setProfile({ ...profile, username: e.target.value })} /></label>
      <label className="field"><span>Password</span><input type="password" autoComplete="current-password" value={profile.password ?? ''} onChange={(e) => setProfile({ ...profile, password: e.target.value })} /></label>
      <div className={`status-card ${status}`}><span className="status-dot" /><div><small>STATUS</small><strong>{t(status)}</strong></div></div>
      {status === 'disconnected'
        ? <button className="primary wide" onClick={() => run(() => monitor.connect(profile))}>{t('connect')}</button>
        : <button className="wide" onClick={() => run(() => monitor.disconnect())}>{t('disconnect')}</button>}
    </aside>

    <main className="monitor-main">
      <section className="browse-panel">
        <div className="panel-toolbar"><div><p className="eyebrow">ADDRESS SPACE</p><h2>Browse</h2></div><div className="toolbar-actions"><span className="selection-count">{selected.size} {t('selected')}</span><button onClick={() => run(() => monitor.browse())}>{t('browse')}</button><button className="primary" disabled={!selected.size} onClick={() => run(() => monitor.subscribe([...selected]))}>{t('subscribe')}</button></div></div>
        <div className="browse-tree">{nodes.length ? nodes.map((node) => <BrowseNode key={node.nodeId} node={node} selected={selected} onBrowse={(nodeId) => run(() => monitor.browse(nodeId))} onToggle={(nodeId) => setSelected((current) => {
          const next = new Set(current)
          if (next.has(nodeId)) next.delete(nodeId)
          else next.add(nodeId)
          return next
        })} />) : <div className="empty-inline">Objects <span>→ {t('browse')}</span></div>}</div>
      </section>

      <section className="live-panel">
        <div className="panel-toolbar"><div><p className="eyebrow">SUBSCRIPTION</p><h2>{t('liveValues')}</h2></div><div className="toolbar-actions"><input className="filter-input" aria-label={t('filter')} placeholder={`${t('filter')}…`} value={filter} onChange={(e) => setFilter(e.target.value)} /><button aria-pressed={paused} onClick={() => setPaused(!paused)}>{paused ? t('resume') : t('pause')}</button></div></div>
        <div className="live-table-wrap"><table><thead><tr><th>Variable</th><th>Value</th><th>Quality</th><th>Source timestamp</th><th>Server timestamp</th></tr></thead><tbody>
          {filteredValues.map((value) => <tr key={value.nodeId}><td><strong>{value.browseName}</strong><small>{value.nodeId}</small></td><td className="value-cell">{String(value.value)}</td><td><span className={`quality ${value.quality.toLowerCase()}`}>{value.quality}</span></td><td>{formatTimestamp(value.sourceTimestamp)}</td><td>{formatTimestamp(value.serverTimestamp)}</td></tr>)}
        </tbody></table>{!filteredValues.length && <div className="empty-state">{t('noValues')}</div>}</div>
      </section>

      <section className="diagnostics-panel">
        <div className="panel-toolbar"><div><p className="eyebrow">EVENT BUFFER · {events.length}/{MAX_EVENTS}</p><h2>{t('diagnostics')}</h2></div><button onClick={exportEvents}>{t('export')} JSONL</button></div>
        <div className="diagnostic-list">{diagnostics.slice(-8).reverse().map((event, index) => <div className={`diagnostic ${event.level}`} key={`${event.timestamp}-${index}`}><time>{formatTimestamp(event.timestamp)}</time><span>{event.message}</span></div>)}{!diagnostics.length && <div className="empty-inline">No diagnostic events</div>}</div>
      </section>
    </main>
  </div>
}

function BrowseNode({ node, selected, onToggle, onBrowse }: { node: UaNode; selected: Set<string>; onToggle(nodeId: string): void; onBrowse(nodeId: string): void }) {
  const [expanded, setExpanded] = useState(true)
  const variableIds = useMemo(() => flattenVariables(node).map((item) => item.nodeId), [node])
  const checked = node.nodeClass === 'Variable' ? selected.has(node.nodeId) : variableIds.length > 0 && variableIds.every((id) => selected.has(id))
  const toggle = () => {
    if (node.nodeClass === 'Variable') onToggle(node.nodeId)
    else variableIds.forEach((id) => {
      if (checked === selected.has(id)) onToggle(id)
    })
  }
  return <div className="browse-node">
    <div className="browse-row">
      <button className="tree-toggle" aria-label="Toggle children" disabled={!node.children?.length} onClick={() => setExpanded(!expanded)}>{node.children?.length ? (expanded ? '⌄' : '›') : '·'}</button>
      <input type="checkbox" aria-label={`Select ${node.browseName}`} checked={checked} onChange={toggle} />
      <span className={node.nodeClass === 'Variable' ? 'variable' : 'folder'}>{node.browseName}</span>
      <small>{node.dataType ?? node.nodeClass}</small>
      {node.nodeClass !== 'Variable' && <button className="tree-browse" onClick={() => onBrowse(node.nodeId)}>›</button>}
    </div>
    {expanded && node.children && <div className="browse-children">{node.children.map((child) => <BrowseNode key={child.nodeId} node={child} selected={selected} onToggle={onToggle} onBrowse={onBrowse} />)}</div>}
  </div>
}

const flattenVariables = (node: UaNode): UaNode[] =>
  node.nodeClass === 'Variable' ? [node] : (node.children ?? []).flatMap(flattenVariables)

const formatTimestamp = (value: string) => {
  const date = new Date(value)
  return Number.isNaN(date.valueOf()) ? value : date.toLocaleTimeString([], { hour12: false, fractionalSecondDigits: 3 })
}

export { MAX_EVENTS }
