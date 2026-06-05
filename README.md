# bare-ltx2

A [Bare](https://github.com/holepunchto/bare) native addon for **LTX-2.3** video
generation — text-to-video (T2V) and image-to-video (I2V) — built on top of
[`stable-diffusion.cpp`](https://github.com/leejet/stable-diffusion.cpp).

Generation runs on a libuv worker thread, so calls do **not** block the Bare
event loop. Frames are returned to JavaScript as raw RGB `ArrayBuffer`s.

> Status: `0.1.0`, early/experimental. CPU works everywhere; GPU backends
> (Vulkan / Metal) depend on how the underlying `stable-diffusion.cpp` fork is
> built.

## Install

Not yet published to npm. Install from the repository:

```sh
npm install github:64johnlee/bare-ltx2
```

The addon is compiled from source on install (see [Building](#building)), so you
need a C/C++ toolchain, CMake (≥ 3.25) and Ninja available. There are no
prebuilt binaries published yet — prebuilds are produced only on tagged releases.

## Requirements

- [Bare](https://github.com/holepunchto/bare) runtime (`npm i -g bare-runtime`)
- CMake ≥ 3.25 and Ninja
- A C/C++ compiler (Clang, GCC, or MSVC)
- LTX-2.3 model weights (not bundled — see [Models](#models))

CI builds and smoke-tests on Node 22 / Ubuntu 22.04; prebuild targets are
`linux-x64`, `darwin-arm64`, and `win32-x64`.

## Models

`createContext` needs five paths. The audio VAE is optional.

| Option       | What it is                                    | Required |
|--------------|-----------------------------------------------|----------|
| `model`      | LTX-2.3 diffusion model (`.gguf`)             | ✅       |
| `vae`        | Video VAE weights (`.safetensors`)            | ✅       |
| `llm`        | Gemma-3 text encoder (`.gguf`)                | ✅       |
| `connectors` | Embeddings connectors (`.safetensors`)        | ✅       |
| `audioVae`   | Audio VAE weights (`.safetensors`)            | optional |

## Usage

```js
const ltx2 = require('bare-ltx2')

const ctx = ltx2.createContext({
  model:      '/models/ltx2.3-diffusion.gguf',
  vae:        '/models/ltx2.3-vae.safetensors',
  llm:        '/models/gemma3-encoder.gguf',
  connectors: '/models/ltx2.3-connectors.safetensors',
  // audioVae:  '/models/ltx2.3-audio-vae.safetensors', // optional
  // threads: 0,            // 0 = auto-detect
  // backend: 'vulkan',     // 'cpu' | 'vulkan' | 'metal' | undefined (auto)
  // vaeDecodeOnly: false,  // must stay false for I2V (encoder is needed)
})

ltx2.generateT2V(ctx, {
  prompt: 'a lovely cat sitting on a sunny windowsill',
  width: 1280,
  height: 720,
  frames: 33,
  fps: 24,
  seed: 42,        // -1 for random
}, (err, result) => {
  if (err) throw new Error(err)

  // result = { width, height, nFrames, frames: ArrayBuffer[] }
  // each frame is width * height * 3 bytes of packed RGB
  console.log(`got ${result.nFrames} frames at ${result.width}x${result.height}`)

  ltx2.freeContext(ctx)
})
```

### Image-to-video

Pass a reference image as a packed RGB `ArrayBuffer` of
`initWidth * initHeight * 3` bytes. `vaeDecodeOnly` must be `false` (the default)
because I2V needs the VAE encoder.

```js
const initImage = /* ArrayBuffer: initWidth * initHeight * 3 bytes of RGB */

ltx2.generateI2V(ctx, {
  prompt: 'the camera slowly pans left',
  initWidth: 1280,
  initHeight: 720,
  width: 1280,
  height: 720,
  frames: 33,
  fps: 24,
  seed: -1,
}, initImage, (err, result) => {
  if (err) throw new Error(err)
  // ... same result shape as T2V
  ltx2.freeContext(ctx)
})
```

## API

### `createContext(opts)` → `ctx`

Creates an inference context. Throws if a required model path is missing or the
model fails to load. Returns an opaque handle to pass to the `generate*`
functions; release it with `freeContext`.

| Option          | Type      | Default     | Notes                                                       |
|-----------------|-----------|-------------|-------------------------------------------------------------|
| `model`         | `string`  | —           | required                                                    |
| `vae`           | `string`  | —           | required                                                    |
| `llm`           | `string`  | —           | required                                                    |
| `connectors`    | `string`  | —           | required                                                    |
| `audioVae`      | `string`  | `null`      | optional                                                    |
| `threads`       | `number`  | `0`         | CPU threads; `0` auto-detects                               |
| `backend`       | `string`  | auto        | `'cpu'` \| `'vulkan'` \| `'metal'`                          |
| `vaeDecodeOnly` | `boolean` | `false`     | `true` saves memory for T2V-only; disables I2V              |

### `generateT2V(ctx, opts, callback)`

Generates a video from a text prompt. Runs on a worker thread.
`callback(err, result)` where `result` is
`{ width, height, nFrames, frames: ArrayBuffer[] }`. On failure, `err` is a
string and `result` is `null`.

| Option      | Type     | Default                                        |
|-------------|----------|------------------------------------------------|
| `prompt`    | `string` | required                                       |
| `negPrompt` | `string` | `'worst quality, low quality, blurry'`         |
| `width`     | `number` | `1280`                                         |
| `height`    | `number` | `720`                                          |
| `frames`    | `number` | `33`                                           |
| `fps`       | `number` | `24`                                           |
| `seed`      | `number` | `-1` (random)                                  |

### `generateI2V(ctx, opts, initImage, callback)`

Same as `generateT2V`, plus a reference image. `opts` additionally accepts
`initWidth` (default `1280`) and `initHeight` (default `720`). `initImage` is an
`ArrayBuffer` of `initWidth * initHeight * 3` bytes of packed RGB; an
`ArrayBuffer` smaller than that throws.

### `freeContext(ctx)`

Frees the native context and releases its memory. Call it only after all
generation callbacks for that context have fired.

## Building

The build uses [`bare-make`](https://github.com/holepunchto/bare-make), which
drives CMake. `CMakeLists.txt` fetches the
`ltx2-video-generation` branch of the `stable-diffusion.cpp` fork via
`cmake-fetch`, so the first build downloads and compiles that dependency.

```sh
npm run build
# equivalent to:
#   bare-make generate && bare-make build && bare-make install
```

## Testing

```sh
npm test          # runs: bare test.js
```

The default smoke test only checks that the addon loads and exports the expected
functions — no model files required.

A full integration test (actually generating frames) runs only when model paths
are provided through environment variables:

```sh
LTX2_MODEL_PATH=/models/ltx2.3-diffusion.gguf \
LTX2_VAE_PATH=/models/ltx2.3-vae.safetensors \
LTX2_LLM_PATH=/models/gemma3-encoder.gguf \
LTX2_CONNECTORS_PATH=/models/ltx2.3-connectors.safetensors \
LTX2_AUDIO_VAE_PATH=/models/ltx2.3-audio-vae.safetensors \
bare test.js
```

## License

MIT
