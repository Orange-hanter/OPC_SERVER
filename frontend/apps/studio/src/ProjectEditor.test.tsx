import { fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import { createEmptyProject } from './domain'
import { I18nContext, translate } from './i18n'
import { ProjectEditor } from './ProjectEditor'

describe('ProjectEditor', () => {
  it('edits project metadata through an accessible labeled field', async () => {
    const project = createEmptyProject()
    const onProjectChange = vi.fn()
    render(
      <I18nContext.Provider value={{ locale: 'en', t: (key) => translate('en', key) }}>
        <ProjectEditor
          project={project}
          content={JSON.stringify(project)}
          onProjectChange={onProjectChange}
          onContentChange={vi.fn()}
        />
      </I18nContext.Provider>,
    )

    const name = screen.getByLabelText('Name')
    fireEvent.change(name, { target: { value: 'water-treatment' } })
    expect(onProjectChange).toHaveBeenLastCalledWith(expect.objectContaining({ name: 'water-treatment' }))
  })
})
