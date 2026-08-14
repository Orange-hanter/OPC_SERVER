import { describe, expect, it } from 'vitest'
import { messages, translate, type MessageKey } from './i18n'

describe('i18n', () => {
  it('has a Russian string for every English key', () => {
    const keys = Object.keys(messages.en) as MessageKey[]
    expect(keys.length).toBeGreaterThan(0)
    for (const key of keys) {
      expect(messages.ru[key].length).toBeGreaterThan(0)
      expect(translate('en', key).length).toBeGreaterThan(0)
    }
  })

  it('switches validate labels by locale', () => {
    expect(translate('en', 'validate')).toBe('Validate')
    expect(translate('ru', 'validate')).toBe('Проверить')
  })
})
