'use strict'

// Minimal runnable example: text-to-video with bare-ltx2.
//
// Provide paths to the LTX-2.3 weights via environment variables, then:
//
//   LTX2_MODEL_PATH=/models/ltx2.3-diffusion.gguf \
//   LTX2_VAE_PATH=/models/ltx2.3-vae.safetensors \
//   LTX2_LLM_PATH=/models/gemma3-encoder.gguf \
//   LTX2_CONNECTORS_PATH=/models/ltx2.3-connectors.safetensors \
//   bare examples/generate.js
//
// Frames come back as raw RGB ArrayBuffers (width * height * 3 bytes each).
// The ffmpeg line near the bottom shows how to turn them into an mp4.

const ltx2 = require('..')

// Bare exposes env via Bare.env; fall back to process.env for portability.
const env = (key) =>
  (typeof Bare !== 'undefined' ? Bare?.env?.[key] : undefined) ??
  (typeof process !== 'undefined' ? process.env?.[key] : undefined)

const model = env('LTX2_MODEL_PATH')
const vae = env('LTX2_VAE_PATH')
const llm = env('LTX2_LLM_PATH')
const connectors = env('LTX2_CONNECTORS_PATH')

if (!model || !vae || !llm || !connectors) {
  console.log(
    'Set LTX2_MODEL_PATH, LTX2_VAE_PATH, LTX2_LLM_PATH and LTX2_CONNECTORS_PATH ' +
    'to run this example (see the comment at the top of this file).'
  )
} else {
  const ctx = ltx2.createContext({
    model,
    vae,
    llm,
    connectors,
    audioVae: env('LTX2_AUDIO_VAE_PATH') || null
  })

  const opts = {
    prompt: 'a lovely cat sitting on a sunny windowsill',
    width: 512,
    height: 288,
    frames: 25,
    fps: 24,
    seed: 42
  }

  ltx2.generateT2V(ctx, opts, (err, result) => {
    if (err) {
      console.error('generation failed:', err)
      ltx2.freeContext(ctx)
      return
    }

    const frameBytes = result.width * result.height * 3
    console.log(`generated ${result.nFrames} frames at ${result.width}x${result.height}`)
    console.log(`each frame is ${frameBytes} bytes of packed RGB`)

    // To encode the clip, write the frames in order to a raw file and run ffmpeg:
    //
    //   const fs = require('bare-fs')
    //   const fd = fs.openSync('frames.rgb', 'w')
    //   for (const f of result.frames) fs.writeSync(fd, Buffer.from(f))
    //   fs.closeSync(fd)
    //
    //   ffmpeg -f rawvideo -pixel_format rgb24 -video_size 512x288 -framerate 24 \
    //          -i frames.rgb -pix_fmt yuv420p out.mp4

    ltx2.freeContext(ctx)
  })
}
