// Settings, as a person changes them: through the controls, and read back from
// the app's own state and from what it shows.
import { test, expect } from '../support/fixtures.mjs';

async function openSettings(app) {
  await app.click('launch.settings');
  await app.waitForScreen('settings');
}

test.describe('settings', () => {
  test('are reached from the launch screen and left by Back', async ({ app }) => {
    await openSettings(app);
    expect(await app.find('settings.back')).not.toBeNull();
    await app.click('settings.back');
    await app.waitForScreen('launch');
  });

  test('offer the cameras the browser lists, and look again on request', async ({ app }) => {
    await openSettings(app);
    const texts = await app.texts();
    expect(texts).toContain('Fake camera');

    await app.page.evaluate(() => window.__fakeCamera.setDevices([
      { deviceId: 'a', label: 'Desk camera' },
      { deviceId: 'b', label: 'Document camera' },
    ]));
    await app.click('settings.refreshCameras');
    await expect.poll(async () => (await app.texts()).includes('Document camera')).toBe(true);
    expect(await app.texts()).toContain('Desk camera');
  });

  test('keep the theme through a reload', async ({ app }) => {
    await openSettings(app);
    await app.click('settings.theme.2'); // Dark
    await expect.poll(async () => (await app.singleton('SettingsState')).themeMode).toBe('dark');

    // At once, not after a wait for the app to have written it: a reload the
    // moment after the click must find the choice kept (the page flushes its
    // settings as it is hidden, settings_web.cpp).
    await app.reload();
    expect((await app.singleton('SettingsState')).themeMode).toBe('dark');
    await openSettings(app);
    const chips = await app.find('settings.theme');
    expect(chips.currentIndex).toBe(2);
  });

  test('keep the solving method, and say what it is like', async ({ app }) => {
    await openSettings(app);
    const note = async () => (await app.find('settings.method.note')).text;
    const beginner = await note();
    expect(beginner).toMatch(/Layer by layer/);

    await app.click('settings.method.2'); // Advanced
    await expect.poll(async () => (await app.singleton('SettingsState')).solveMethod).toBe(2);
    expect(await note()).not.toBe(beginner);

    await app.reload();
    expect((await app.singleton('SettingsState')).solveMethod).toBe(2);
  });

  test('turn the cube on its own, or not', async ({ app }) => {
    await openSettings(app);
    expect((await app.find('settings.cubeAutoRotate')).checked).toBe(true);
    await app.clickToggle('settings.cubeAutoRotate');
    await expect.poll(async () => (await app.singleton('SettingsState')).cubeAutoRotate).toBe(false);
    await app.clickToggle('settings.cubeAutoRotate');
    await expect.poll(async () => (await app.singleton('SettingsState')).cubeAutoRotate).toBe(true);
  });

  test('only offer the graphics backend this page has', async ({ app }) => {
    await openSettings(app);
    const texts = await app.texts();
    expect(texts).toContain('WebGL');
    expect(texts).not.toContain('Direct3D11');
    expect(texts).not.toContain('Metal');
  });

  test('show the statistics overlay when asked', async ({ app }) => {
    await openSettings(app);
    await app.clickToggle('settings.statsForNerds');
    await expect.poll(async () => (await app.singleton('SettingsState')).statsForNerds).toBe(true);
  });
});

// Where the app keeps a setting in the browser's localStorage: Qt's own scheme
// (organisation, application, then the settings group and name).
const stored = (name) => `qt-v0-Cubeulator-Cubeulator-${name}`;

test.describe('settings are kept by the page', () => {
  // A QML Settings object writes a change half a second after it is made. A
  // page that is hidden, or left, inside that time has to write it as it goes:
  // reloading straight after a click (above) is the case that comes with the
  // page being left, these are the page being put away and staying.
  for (const kind of ['visibilitychange', 'pagehide']) {
    test(`are in the browser's storage as soon as the page is put away (${kind})`, async ({ app }) => {
      await openSettings(app);
      await app.click('settings.theme.2'); // Dark
      await app.click('settings.method.2'); // Advanced
      await expect.poll(async () => (await app.singleton('SettingsState')).themeMode).toBe('dark');
      await expect.poll(async () => (await app.singleton('SettingsState')).solveMethod).toBe(2);

      await app.putAway(kind);
      const kept = await app.stored();
      expect(kept[stored('appearance/themeMode')]).toBe('dark');
      expect(kept[stored('solving/method')]).toBe('2');
    });
  }
});

test.describe('settings an earlier visit left', () => {
  // The keys and values a build that used Qt.labs.settings wrote, as read from
  // its page. The app's Settings are QtCore's now; they have to find these.
  test.use({
    appOptions: {
      storage: {
        [stored('appearance/themeMode')]: 'dark',
        [stored('solving/method')]: '2',
        [stored('rendering/graphicsBackend')]: 'opengl',
      },
    },
  });

  test('are read when the app starts, and still the ones it keeps', async ({ app }) => {
    const settings = await app.singleton('SettingsState');
    expect(settings.themeMode).toBe('dark');
    expect(settings.solveMethod).toBe(2);
    expect(settings.graphicsBackend).toBe('opengl');

    // Not read once and then written somewhere else: a reload finds them where
    // they were, and a change is written to the same place.
    await app.reload();
    expect((await app.singleton('SettingsState')).themeMode).toBe('dark');
    await openSettings(app);
    await app.click('settings.theme.1'); // Light
    await expect.poll(async () => (await app.singleton('SettingsState')).themeMode).toBe('light');
    await app.putAway();
    expect((await app.stored())[stored('appearance/themeMode')]).toBe('light');
  });
});

test.describe('known today', () => {
  // EXPECTED TO CHANGE: a toggle's label does not answer a press, only its
  // 20 px box does (SettingsScreen.qml, Toggle). Says what is true so that the
  // fix shows up as a failure to update.
  test('a toggle is its box, not its label', async ({ app }) => {
    await openSettings(app);
    await app.reveal('settings.cubeAutoRotate');
    const item = await app.find('settings.cubeAutoRotate');
    const box = await app.canvasBox();
    await app.page.mouse.click(box.left + item.x + item.width - 8, box.top + item.y + item.height / 2);
    await app.page.waitForTimeout(500);
    expect((await app.singleton('SettingsState')).cubeAutoRotate).toBe(true);
  });
});

const manager = (app) => app.singleton('LanguageManager');

test.describe('language', () => {
  test('lists every language in its own script', async ({ app }) => {
    await openSettings(app);
    const texts = await app.texts();
    for (const name of ['English', 'Deutsch', 'Français', 'Español', '日本語', '简体中文', 'עברית', 'العربية', 'יידיש']) {
      expect(texts, `${name} is offered`).toContain(name);
    }
  });

  // The language chip order is: the device's, then the languages in the order
  // of languages.json (English, Deutsch, Français, Español, 日本語, 简体中文,
  // עברית, العربية, יידיש).
  const LANGUAGES = [
    [2, 'de', 'Deutsch', false],
    [3, 'fr', 'Français', false],
    [4, 'es', 'Español', false],
    [5, 'ja', '日本語', false],
    [6, 'zh-CN', '简体中文', false],
    [7, 'he', 'עברית', true],
    [8, 'ar', 'العربية', true],
    [9, 'yi', 'יידיש', true],
  ];

  for (const [chip, code, name, rtl] of LANGUAGES) {
    test(`${name} can be chosen, and the app takes its language and direction`, async ({ app }) => {
      await openSettings(app);
      await app.click(`settings.language.${chip}`);
      await expect.poll(async () => (await manager(app)).selectedLanguage).toBe(code);

      const state = await manager(app);
      expect(state.currentLanguage).toBe(code);
      expect(state.currentLanguageName).toBe(name);
      expect(state.rtl).toBe(rtl);

      // The launch screen's language button wears the language's own name.
      await app.click('settings.back');
      await app.waitForScreen('launch');
      await expect.poll(async () => (await app.find('launch.language')).text).toBe(name);
    });
  }

  for (const [chip, code] of [[7, 'he'], [8, 'ar']]) {
    test(`mirrors the whole layout for ${code}`, async ({ app }) => {
      await openSettings(app);
      const window = (await app.state()).window;
      const backIn = async () => {
        const item = await app.find('settings.back');
        return item.x + item.width / 2;
      };
      // Left to right: Back sits at the start, which is the left.
      expect(await backIn()).toBeLessThan(window.width / 2);

      await app.click(`settings.language.${chip}`);
      await expect.poll(async () => (await manager(app)).rtl).toBe(true);
      await app.settle();
      // Right to left: the start is the right.
      expect(await backIn()).toBeGreaterThan(window.width / 2);
    });
  }

  test('shows a string no catalogue has translated in English, never as its id', async ({ app }) => {
    // The catalogues are drafts today (every entry unfinished, which the build
    // leaves out), so a chosen language shows English. When translations land
    // the text changes and this still holds: what is on screen is words.
    await openSettings(app);
    await app.click('settings.language.2');
    await expect.poll(async () => (await manager(app)).selectedLanguage).toBe('de');
    await app.settle();
    for (const text of await app.texts()) {
      expect(text, 'a raw string id on screen').not.toMatch(/^[a-z][A-Za-z0-9]*(\.[A-Za-z0-9]+)+$/);
    }
  });

  test('is kept through a reload', async ({ app }) => {
    await openSettings(app);
    await app.click('settings.language.2');
    await expect.poll(async () => (await manager(app)).selectedLanguage).toBe('de');

    await app.reload();
    expect((await manager(app)).selectedLanguage).toBe('de');
    expect((await app.find('launch.language')).text).toBe('Deutsch');
  });
});
