import { readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import { BrowserProjectValidator } from './adapters'

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '../../../..')

const readJson = (relative: string) =>
  JSON.parse(readFileSync(join(repoRoot, relative), 'utf8')) as unknown

describe('schema parity with C++ fixtures', () => {
  const validator = new BrowserProjectValidator()

  it('accepts demo-plant and the shared minimal fixture', async () => {
    await expect(validator.validate(readJson('DOCs/examples/demo-plant.modbusproj.json'))).resolves.toEqual([])
    await expect(validator.validate(readJson('tests/fixtures/valid/minimal.modbusproj.json'))).resolves.toEqual([])
  })

  it('rejects schema-invalid maps that C++ also rejects', async () => {
    const issues = await validator.validate(readJson('tests/fixtures/invalid/missing-required.json'))
    expect(issues.length).toBeGreaterThan(0)
    expect(issues.every((issue) => issue.source === 'schema')).toBe(true)
  })

  it('allows schema-valid maps with broken xref (C++ semantic layer catches those)', async () => {
    const issues = await validator.validate(readJson('tests/fixtures/invalid/unknown-endpoint.json'))
    expect(issues).toEqual([])
  })
})
