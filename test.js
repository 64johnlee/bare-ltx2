'use strict'

const test = require('brittle')
const ltx2 = require('.')

test('module exports correct API', (t) => {
  t.is(typeof ltx2.createContext,  'function')
  t.is(typeof ltx2.generateT2V,    'function')
  t.is(typeof ltx2.generateI2V,    'function')
  t.is(typeof ltx2.freeContext,    'function')
})

// Integration test — requires model files via env vars, skipped in CI by default
test('T2V generates frames', { skip: !process.env.LTX2_MODEL_PATH }, (t) => {
  t.plan(4)

  const ctx = ltx2.createContext({
    model:      process.env.LTX2_MODEL_PATH,
    vae:        process.env.LTX2_VAE_PATH,
    audioVae:   process.env.LTX2_AUDIO_VAE_PATH || null,
    llm:        process.env.LTX2_LLM_PATH,
    connectors: process.env.LTX2_CONNECTORS_PATH,
  })

  ltx2.generateT2V(ctx, {
    prompt: 'a lovely cat sitting on a sunny windowsill',
    width: 512, height: 288, frames: 9, fps: 8, seed: 42,
  }, (err, result) => {
    t.absent(err)
    t.is(result.nFrames, 9)
    t.is(result.frames.length, 9)
    t.ok(result.frames[0] instanceof ArrayBuffer)
    ltx2.freeContext(ctx)
  })
})
