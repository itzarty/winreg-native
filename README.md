# winreg

Native Node.js binding for ultra-fast Windows Registry access.

**Highlights**

- Native C++ binding built with `node-gyp` and `nan`.
- Dramatic speed improvements over JS-based solutions (claimed **250x–5600x** faster depending on operation).
- Promise-first API with synchronous `Sync` variants for blocking operations.
- One callback-based method: `watchValue` for change notifications.

---

## Table of contents

1. [Installation & build](#installation--build)
2. [Quick start](#quick-start)
3. [API reference](#api-reference)
   - [Constants](#constants)
   - [Async methods (Promise)](#async-methods-promise)
   - [Synchronous methods (blocking)](#synchronous-methods-blocking)
   - [watchValue (callback-based)](#watchvalue-callback-based)
4. [Types & conversions](#types--conversions)
5. [Examples](#examples)
   - [Read a value (async/await)](#read-a-value-asyncawait)
   - [Set a value](#set-a-value)
   - [Watch a value](#watch-a-value)
   - [Synchronous usage](#synchronous-usage)
6. [Building / contributing](#building--contributing)
7. [Troubleshooting](#troubleshooting)
8. [License & credits](#license--credits)

---

## Installation & build

**Install from npm**
```bash
npm install winreg-native
```

> This module contains native code and must be built for the target Node.js version and platform (Windows only).

1. Ensure you have a working **Windows** build environment and Node.js development toolchain (Python, Visual Studio Build Tools, `node-gyp`).
2. Add the module source to your project or install it as a dependency.
3. Install `nan` (used by the native code) if not bundled: `npm install --save nan`.

Typical local build steps from the module root:

```bash
npm install        # install JS deps (if present)
node-gyp configure
node-gyp build
```

---

## Quick start

```js
const winreg = require('winreg');

(async () => {
  const v = await winreg.getValue(winreg.HKLM, 'SOFTWARE\\MyApp', 'InstallPath');
  console.log('InstallPath =', v);
})();
```

---

## API reference

### Constants

The module exports hive constants:

- `HKLM` — HKEY\_LOCAL\_MACHINE
- `HKCU` — HKEY\_CURRENT\_USER
- `HKCR` — HKEY\_CLASSES\_ROOT
- `HKU`  — HKEY\_USERS
- `HKCC` — HKEY\_CURRENT\_CONFIG

---

### Async methods (Promise)

All async methods return a `Promise`.

- `getValue(hive, key, value)` → `String | Number | BigInt | Buffer | Array<string>`
- `setValue(hive, key, value, data)` → `Boolean`
- `deleteValue(hive, key, value)` → `Boolean`
- `deleteKey(hive, key)` → `Boolean`
- `deleteTree(hive, key)` → `Boolean`
- `enumerateKeys(hive, key)` → `String[]`
- `enumerateValues(hive, key)` → `Object` mapping `{ valueName: convertedValue }`

All have synchronous counterparts with `Sync` suffix.

---

### Synchronous methods (blocking)

Identical signatures to async methods but block the event loop and return directly.

---

### watchValue (callback-based)

`watchValue(hive, key, value, callback)`

- Watches for changes to a registry value.
- `callback(err, value)` — `value` is converted per [Types & conversions](#types--conversions).

---

## Types & conversions

From **Registry → JS** (`ConvertRegistryValue`):

- `REG_DWORD` → `Number` (unsigned 32-bit)
- `REG_QWORD` → `BigInt` (signed 64-bit)
- `REG_BINARY` → `Buffer`
- `REG_MULTI_SZ` → `Array<string>`
- `REG_SZ`, `REG_EXPAND_SZ` → `String`
- Fallback/default → `String` (raw data interpreted as UTF-8)

From **JS → Registry** (`ConvertJsValue`):

- `String` → `REG_SZ`
- `Boolean` → `REG_DWORD` (0 or 1)
- `BigInt` → `REG_QWORD` if lossless 64-bit, else `REG_SZ` (string form)
- `Number` →
  - `REG_DWORD` if fits in unsigned 32-bit
  - `REG_QWORD` if fits in signed 64-bit
  - otherwise `REG_SZ` (string form)

**Important:**

- Booleans are always written as `REG_DWORD` (0/1) and will be read back as `Number`, not as `true`/`false`.
- Large numbers beyond JS safe integer range will be returned as `BigInt`.

---

## Examples

### Read a value (async/await)

```js
const path = await winreg.getValue(winreg.HKLM, 'SOFTWARE\\MyCompany\\MyApp', 'InstallPath');
console.log('InstallPath:', path);
```

### Set a value

```js
await winreg.setValue(winreg.HKCU, 'Software\\MyApp', 'Enabled', true);
```

### Watch a value

```js
winreg.watchValue(winreg.HKCU, 'Software\\MyApp', 'CurrentVersion', (err, value) => {
  if (err) return console.error(err);
  console.log('new value:', value);
});
```

### Synchronous usage

```js
const v = winreg.getValueSync(winreg.HKLM, 'SOFTWARE\\MyApp', 'InstallPath');
console.log(v);
```

---

## Troubleshooting

- **Booleans not returned as booleans** — expected behavior; interpret `0`/`1` manually.
- **Binary data** — returned as `Buffer`.
- **Build errors** — ensure MSVC Build Tools & `node-gyp` are set up.

---
