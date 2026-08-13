import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { createOpcUaMonitor, createProjectFileAdapter, createProjectValidator } from './adapters'
import { createEmptyProject, type Project, type ValidationIssue } from './domain'
import { I18nContext, translate, type Locale, type MessageKey } from './i18n'
import { Monitor } from './Monitor'
import { ProjectEditor } from './ProjectEditor'

const serialize = (project: Project) => `${JSON.stringify(project, null, 2)}\n`
const initialContent = serialize(createEmptyProject())

export default function App() {
  const [locale, setLocale] = useState<Locale>(() => (localStorage.getItem('studio-locale') as Locale) || 'en')
  const [theme, setTheme] = useState<'light' | 'dark'>(() =>
    (localStorage.getItem('studio-theme') as 'light' | 'dark') ||
    (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'),
  )
  const [mode, setMode] = useState<'editor' | 'monitor'>('editor')
  const [content, setContent] = useState(initialContent)
  const [savedContent, setSavedContent] = useState(initialContent)
  const [path, setPath] = useState<string | null>(null)
  const [issues, setIssues] = useState<ValidationIssue[]>([])
  const [notice, setNotice] = useState<string | null>(null)
  const history = useRef([initialContent])
  const [historyIndex, setHistoryIndex] = useState(0)
  const fileAdapter = useMemo(createProjectFileAdapter, [])
  const validator = useMemo(createProjectValidator, [])
  const monitor = useMemo(createOpcUaMonitor, [])
  const t = useCallback((key: MessageKey) => translate(locale, key), [locale])

  const project = useMemo(() => {
    try { return JSON.parse(content) as Project } catch { return null }
  }, [content])
  const dirty = content !== savedContent

  const applyContent = useCallback((next: string) => {
    setContent(next)
    setIssues([])
    setHistoryIndex((currentIndex) => {
      const nextHistory = history.current.slice(0, currentIndex + 1)
      nextHistory.push(next)
      history.current = nextHistory.slice(-100)
      return history.current.length - 1
    })
  }, [])

  const resetDocument = useCallback((next: string, nextPath: string | null) => {
    setContent(next)
    setSavedContent(next)
    setPath(nextPath)
    setIssues([])
    history.current = [next]
    setHistoryIndex(0)
  }, [])

  const reportError = useCallback((error: unknown) => setNotice(error instanceof Error ? error.message : String(error)), [])
  const confirmDiscard = useCallback(() => !dirty || window.confirm(t('unsaved')), [dirty, t])
  const newProject = useCallback(() => {
    if (confirmDiscard()) resetDocument(serialize(createEmptyProject()), null)
  }, [confirmDiscard, resetDocument])

  const openProject = useCallback(async () => {
    if (!confirmDiscard()) return
    try {
      const file = await fileAdapter.open()
      if (file) resetDocument(file.content, file.path)
    } catch (error) { reportError(error) }
  }, [confirmDiscard, fileAdapter, reportError, resetDocument])

  const saveProject = useCallback(async (saveAs = false) => {
    try {
      const savedPath = saveAs ? await fileAdapter.saveAs(content) : await fileAdapter.save(path, content)
      if (savedPath) {
        setPath(savedPath)
        setSavedContent(content)
        setNotice(t('save'))
      }
    } catch (error) { reportError(error) }
  }, [content, fileAdapter, path, reportError, t])

  const validate = useCallback(async () => {
    if (!project) {
      setIssues([{ severity: 'error', path: '/', message: t('jsonError'), source: 'schema' }])
      return
    }
    try {
      const result = await validator.validate(project, content)
      setIssues(result)
      setNotice(result.length ? t('invalid') : t('valid'))
    } catch (error) { reportError(error) }
  }, [content, project, reportError, t, validator])

  const undo = useCallback(() => setHistoryIndex((index) => {
    const next = Math.max(0, index - 1)
    setContent(history.current[next])
    return next
  }), [])
  const redo = useCallback(() => setHistoryIndex((index) => {
    const next = Math.min(history.current.length - 1, index + 1)
    setContent(history.current[next])
    return next
  }), [])

  useEffect(() => {
    document.documentElement.dataset.theme = theme
    localStorage.setItem('studio-theme', theme)
  }, [theme])
  useEffect(() => {
    localStorage.setItem('studio-locale', locale)
    document.documentElement.lang = locale
  }, [locale])
  useEffect(() => {
    const beforeUnload = (event: BeforeUnloadEvent) => { if (dirty) event.preventDefault() }
    const keyboard = (event: KeyboardEvent) => {
      if (!(event.ctrlKey || event.metaKey)) return
      const key = event.key.toLowerCase()
      if (key === 's') { event.preventDefault(); void saveProject(event.shiftKey) }
      if (key === 'o') { event.preventDefault(); void openProject() }
      if (key === 'z' && !event.shiftKey) { event.preventDefault(); undo() }
      if (key === 'y' || (key === 'z' && event.shiftKey)) { event.preventDefault(); redo() }
    }
    window.addEventListener('beforeunload', beforeUnload)
    window.addEventListener('keydown', keyboard)
    return () => {
      window.removeEventListener('beforeunload', beforeUnload)
      window.removeEventListener('keydown', keyboard)
    }
  }, [dirty, openProject, redo, saveProject, undo])
  useEffect(() => {
    if (!notice) return
    const timer = window.setTimeout(() => setNotice(null), 3000)
    return () => window.clearTimeout(timer)
  }, [notice])

  return <I18nContext.Provider value={{ locale, t }}>
    <div className="app-shell">
      <header className="topbar">
        <div className="brand"><div className="brand-mark" aria-hidden="true">OS</div><div><strong>{t('appName')}</strong><small>{path ?? 'project.modbusproj.json'} {dirty && <span className="dirty-dot">●</span>}</small></div></div>
        <nav className="mode-switch" aria-label="Application mode"><button className={mode === 'editor' ? 'active' : ''} onClick={() => setMode('editor')}>{t('editor')}</button><button className={mode === 'monitor' ? 'active' : ''} onClick={() => setMode('monitor')}>{t('monitor')}</button></nav>
        <div className="app-controls">
          <label className="sr-only" htmlFor="locale">Language</label><select id="locale" value={locale} onChange={(e) => setLocale(e.target.value as Locale)}><option value="en">EN</option><option value="ru">RU</option></select>
          <button className="icon-control" aria-label={theme === 'dark' ? t('light') : t('dark')} onClick={() => setTheme(theme === 'dark' ? 'light' : 'dark')}>{theme === 'dark' ? '☀' : '◐'}</button>
        </div>
      </header>
      {mode === 'editor' && <div className="editor-layout">
        <div className="actionbar">
          <div className="toolbar-group"><button onClick={newProject}>{t('new')}</button><button onClick={() => void openProject()}>{t('open')}</button><button onClick={() => void saveProject()}>{t('save')}</button><button onClick={() => void saveProject(true)}>{t('saveAs')}</button></div>
          <div className="toolbar-group"><button disabled={historyIndex === 0} onClick={undo}>{t('undo')}</button><button disabled={historyIndex >= history.current.length - 1} onClick={redo}>{t('redo')}</button></div>
          <button className="primary validate-button" onClick={() => void validate()}>{t('validate')}</button>
        </div>
        <ProjectEditor project={project} content={content} onContentChange={applyContent} onProjectChange={(next) => applyContent(serialize(next))} />
        {issues.length > 0 && <aside className="issues" aria-live="polite"><div className="issues-heading"><strong>{t('invalid')}</strong><span>{issues.length}</span></div>{issues.map((issue, index) => <div className={`issue ${issue.severity}`} key={`${issue.path}-${index}`}><code>{issue.path}</code><span>{issue.message}</span><small>{issue.source}</small></div>)}</aside>}
      </div>}
      {mode === 'monitor' && <Monitor monitor={monitor} defaultEndpoint={project?.opcua?.endpointUrl} />}
      {notice && <div className="toast" role="status">{notice}<button aria-label="Close" onClick={() => setNotice(null)}>×</button></div>}
    </div>
  </I18nContext.Provider>
}
