'use strict'

const binding = require('./binding')

/**
 * Create an LTX-2 inference context.
 *
 * @param {object}  opts
 * @param {string}  opts.model       Path to LTX-2.3 diffusion model (.gguf)
 * @param {string}  opts.vae         Path to video VAE weights (.safetensors)
 * @param {string}  [opts.audioVae]  Path to audio VAE weights (.safetensors)
 * @param {string}  opts.llm         Path to Gemma-3 text encoder (.gguf)
 * @param {string}  opts.connectors  Path to embeddings connectors (.safetensors)
 * @param {number}  [opts.threads=0] CPU threads (0 = auto-detect)
 * @param {string}  [opts.backend]   "cpu" | "vulkan" | "metal" | undefined (auto)
 * @param {boolean} [opts.vae_decode_only=true]  Set false to enable I2V VAE encode
 * @returns Opaque context handle — pass to generate* functions, free with freeContext()
 */
exports.createContext = function createContext (opts) {
  return binding.createContext(opts)
}

/**
 * Generate a video from a text prompt (T2V).
 * Runs on a background thread; does not block the event loop.
 *
 * @param {*}        ctx      Context from createContext()
 * @param {object}   opts
 * @param {string}   opts.prompt
 * @param {string}   [opts.negPrompt]
 * @param {number}   [opts.width=1280]
 * @param {number}   [opts.height=720]
 * @param {number}   [opts.frames=33]
 * @param {number}   [opts.fps=24]
 * @param {number}   [opts.seed=-1]   -1 for random
 * @param {Function} callback  (err, {width, height, nFrames, frames: ArrayBuffer[]}) => void
 */
exports.generateT2V = function generateT2V (ctx, opts, callback) {
  binding.generateT2V(ctx, opts, callback)
}

/**
 * Generate a video from a text prompt + reference image (I2V).
 *
 * @param {*}           ctx
 * @param {object}      opts           Same as generateT2V, plus initWidth/initHeight
 * @param {ArrayBuffer} initImage      RGB pixel data: initWidth × initHeight × 3 bytes
 * @param {Function}    callback       (err, result) => void
 */
exports.generateI2V = function generateI2V (ctx, opts, initImage, callback) {
  binding.generateI2V(ctx, opts, initImage, callback)
}

/**
 * Free the inference context and release native memory.
 * Must be called after all generation callbacks have fired.
 *
 * @param {*} ctx  Context from createContext()
 */
exports.freeContext = function freeContext (ctx) {
  binding.freeContext(ctx)
}
