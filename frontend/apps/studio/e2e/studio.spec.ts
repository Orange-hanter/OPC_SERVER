import { expect, test } from '@playwright/test'

test('edits, validates, and monitors a project', async ({ page }) => {
  await page.goto('/')

  await expect(page.getByText('OPC Engineering Studio').first()).toBeVisible()
  await page.getByRole('button', { name: 'Endpoints' }).click()
  await page.getByRole('button', { name: 'Add' }).click()
  await expect(page.getByText('endpoint-2').first()).toBeVisible()

  await page.getByRole('button', { name: 'Validate' }).click()
  await expect(page.getByRole('status')).toContainText('Project is valid')

  await page.getByRole('button', { name: 'OPC UA monitor' }).click()
  await page.getByRole('button', { name: 'Connect' }).click()
  await expect(page.getByText('Connected', { exact: true })).toBeVisible()

  await page.getByRole('button', { name: 'Browse', exact: true }).click()
  await page.getByLabel('Select Level').check()
  await page.getByRole('button', { name: 'Subscribe selection' }).click()
  await expect(page.locator('.live-table-wrap tbody')).toContainText('Level')
  await expect(page.locator('.quality')).toContainText(/Good|Uncertain/)

  await page.locator('#locale').selectOption('ru')
  await expect(page.getByRole('button', { name: 'Редактор проекта' })).toBeVisible()
})
