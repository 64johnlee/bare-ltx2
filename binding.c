#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <bare.h>
#include <js.h>
#include <uv.h>

#include "ltx2.h"

/* -----------------------------------------------------------------------
 * Internal types
 * --------------------------------------------------------------------- */

typedef struct {
  sd_ctx_t *sd_ctx;
} ltx2_ctx_t;

typedef struct {
  uv_work_t   work;
  js_env_t   *env;
  js_ref_t   *callback;

  ltx2_ctx_t *ctx;

  char    *prompt;
  char    *neg_prompt;
  int      width;
  int      height;
  int      frames;
  int      fps;
  int64_t  seed;

  bool     is_i2v;
  uint8_t *init_data;
  int      init_w;
  int      init_h;

  sd_image_t *out_frames;
  int         n_frames;
  bool        success;
} ltx2_work_t;

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

static char *js_to_cstr (js_env_t *env, js_value_t *val) {
  size_t len;
  js_get_value_string_utf8(env, val, NULL, 0, &len);
  char *buf = malloc(len + 1);
  js_get_value_string_utf8(env, val, (utf8_t *)buf, len + 1, NULL);
  buf[len] = '\0';
  return buf;
}

static char *prop_string (js_env_t *env, js_value_t *obj,
                           const char *key, bool required) {
  js_value_t *val;
  if (js_get_named_property(env, obj, key, &val) != 0) {
    if (required) js_throw_error(env, NULL, key);
    return NULL;
  }
  js_value_type_t t;
  js_typeof(env, val, &t);
  if (t == js_null || t == js_undefined) {
    /* A missing/undefined property lands here (not the get-error path above),
       so this is where `required` must actually be enforced. */
    if (required) js_throw_error(env, NULL, key);
    return NULL;
  }
  return js_to_cstr(env, val);
}

static int prop_int (js_env_t *env, js_value_t *obj,
                      const char *key, int def) {
  js_value_t *val;
  if (js_get_named_property(env, obj, key, &val) != 0) return def;
  js_value_type_t t;
  js_typeof(env, val, &t);
  if (t != js_number) return def;
  int32_t v; js_get_value_int32(env, val, &v);
  return (int)v;
}

static int64_t prop_int64 (js_env_t *env, js_value_t *obj,
                            const char *key, int64_t def) {
  js_value_t *val;
  if (js_get_named_property(env, obj, key, &val) != 0) return def;
  js_value_type_t t;
  js_typeof(env, val, &t);
  if (t != js_number) return def;
  int64_t v; js_get_value_int64(env, val, &v);
  return v;
}

/* -----------------------------------------------------------------------
 * ltx2.createContext(opts) → external
 * --------------------------------------------------------------------- */

static js_value_t *ltx2_create_context (js_env_t *env,
                                         js_callback_info_t *info) {
  size_t argc = 1;
  js_value_t *argv[1];
  js_get_callback_info(env, info, &argc, argv, NULL, NULL);

  js_value_t *opts = argv[0];

  char *model      = prop_string(env, opts, "model",      true);
  char *vae        = prop_string(env, opts, "vae",        true);
  char *audio_vae  = prop_string(env, opts, "audioVae",   false);
  char *llm        = prop_string(env, opts, "llm",        true);
  char *connectors = prop_string(env, opts, "connectors", true);
  char *backend    = prop_string(env, opts, "backend",    false);
  int   threads    = prop_int(env, opts, "threads", 0);

  /* Default false: decode-only disables the VAE encoder needed for I2V. */
  bool vae_decode_only = false;
  js_value_t *vdt;
  if (js_get_named_property(env, opts, "vaeDecodeOnly", &vdt) == 0) {
    js_value_type_t vt; js_typeof(env, vdt, &vt);
    if (vt == js_boolean) js_get_value_bool(env, vdt, &vae_decode_only);
  }

  /* prop_string(...true) has thrown for any missing required path; bail before
     handing NULLs to ltx2_new_ctx (which would crash in the native layer). */
  if (!model || !vae || !llm || !connectors) {
    free(model); free(vae); free(audio_vae);
    free(llm); free(connectors); free(backend);
    return NULL;
  }

  sd_ctx_t *sd = ltx2_new_ctx(model, vae, audio_vae, llm, connectors,
                                threads, vae_decode_only, backend);

  free(model); free(vae); free(audio_vae);
  free(llm); free(connectors); free(backend);

  if (!sd) {
    js_throw_error(env, NULL, "ltx2_new_ctx failed — check model paths");
    return NULL;
  }

  ltx2_ctx_t *ctx = malloc(sizeof(ltx2_ctx_t));
  ctx->sd_ctx = sd;

  js_value_t *ext;
  js_create_external(env, ctx, NULL, NULL, &ext);
  return ext;
}

/* -----------------------------------------------------------------------
 * Async worker
 * --------------------------------------------------------------------- */

static void gen_execute (uv_work_t *req) {
  ltx2_work_t *w = (ltx2_work_t *)req->data;
  sd_audio_t *audio = NULL;

  if (w->is_i2v && w->init_data) {
    sd_image_t img = { .width  = (uint32_t)w->init_w,
                       .height = (uint32_t)w->init_h,
                       .channel = 3,
                       .data   = w->init_data };
    w->success = ltx2_generate_i2v(w->ctx->sd_ctx,
      w->prompt, w->neg_prompt, img,
      w->width, w->height, w->frames, w->fps, w->seed,
      &w->out_frames, &w->n_frames, &audio);
  } else {
    w->success = ltx2_generate_t2v(w->ctx->sd_ctx,
      w->prompt, w->neg_prompt,
      w->width, w->height, w->frames, w->fps, w->seed,
      &w->out_frames, &w->n_frames, &audio);
  }

  if (audio) free_sd_audio(audio);
}

static void gen_done (uv_work_t *req, int status) {
  ltx2_work_t *w = (ltx2_work_t *)req->data;
  js_env_t *env = w->env;

  js_handle_scope_t *scope;
  js_open_handle_scope(env, &scope);

  js_value_t *cb;
  js_get_reference_value(env, w->callback, &cb);

  js_value_t *err_arg, *res_arg;

  if (!w->success || w->n_frames == 0) {
    js_create_string_utf8(env, (const utf8_t *)"generation failed", -1, &err_arg);
    js_get_null(env, &res_arg);
  } else {
    js_get_null(env, &err_arg);
    js_create_object(env, &res_arg);

    /* Report the ACTUAL decoded dimensions — LTX/VAE may round the requested
       width/height to the VAE factor, so they can differ from w->width/height. */
    uint32_t out_w = w->out_frames[0].width;
    uint32_t out_h = w->out_frames[0].height;

    js_value_t *tmp;
    js_create_int32(env, (int32_t)out_w,  &tmp); js_set_named_property(env, res_arg, "width",   tmp);
    js_create_int32(env, (int32_t)out_h,  &tmp); js_set_named_property(env, res_arg, "height",  tmp);
    js_create_int32(env, w->n_frames,     &tmp); js_set_named_property(env, res_arg, "nFrames", tmp);

    js_value_t *arr;
    js_create_array_with_length(env, (size_t)w->n_frames, &arr);

    for (int i = 0; i < w->n_frames; i++) {
      /* Size each buffer from the frame's OWN dimensions, not the requested
         size — otherwise a resolution mismatch is an out-of-bounds read. */
      size_t fbytes = (size_t)w->out_frames[i].width
                    * (size_t)w->out_frames[i].height
                    * (size_t)w->out_frames[i].channel;
      void *buf_data; js_value_t *buf;
      js_create_arraybuffer(env, fbytes, &buf_data, &buf);
      if (w->out_frames[i].data && fbytes) memcpy(buf_data, w->out_frames[i].data, fbytes);
      js_set_element(env, arr, (uint32_t)i, buf);
    }
    js_set_named_property(env, res_arg, "frames", arr);
  }

  if (w->out_frames) {
    for (int i = 0; i < w->n_frames; i++) free(w->out_frames[i].data);
    free(w->out_frames);
  }

  js_value_t *global;
  js_get_global(env, &global);
  js_value_t *args[2] = { err_arg, res_arg };
  js_call_function(env, global, cb, 2, args, NULL);

  js_delete_reference(env, w->callback);
  js_close_handle_scope(env, scope);

  free(w->prompt);
  free(w->neg_prompt);
  free(w->init_data);
  free(w);
}

/* -----------------------------------------------------------------------
 * ltx2.generateT2V(ctx, opts, callback)
 * --------------------------------------------------------------------- */

static js_value_t *ltx2_gen_t2v (js_env_t *env, js_callback_info_t *info) {
  size_t argc = 3; js_value_t *argv[3];
  js_get_callback_info(env, info, &argc, argv, NULL, NULL);

  ltx2_ctx_t *ctx;
  js_get_value_external(env, argv[0], (void **)&ctx);

  ltx2_work_t *w = calloc(1, sizeof(ltx2_work_t));
  w->work.data = w; w->env = env; w->ctx = ctx; w->is_i2v = false;
  w->prompt     = prop_string(env, argv[1], "prompt",    true);
  if (!w->prompt) { free(w); js_value_t *u; js_get_undefined(env, &u); return u; }
  w->neg_prompt = prop_string(env, argv[1], "negPrompt", false);
  if (!w->neg_prompt) w->neg_prompt = strdup("worst quality, low quality, blurry");
  w->width  = prop_int(env, argv[1], "width",  1280);
  w->height = prop_int(env, argv[1], "height",  720);
  w->frames = prop_int(env, argv[1], "frames",   33);
  w->fps    = prop_int(env, argv[1], "fps",      24);
  w->seed   = prop_int64(env, argv[1], "seed", -1);

  js_create_reference(env, argv[2], 1, &w->callback);

  uv_loop_t *loop; js_get_env_loop(env, &loop);
  uv_queue_work(loop, &w->work, gen_execute, gen_done);

  js_value_t *u; js_get_undefined(env, &u); return u;
}

/* -----------------------------------------------------------------------
 * ltx2.generateI2V(ctx, opts, initImageBuffer, callback)
 * --------------------------------------------------------------------- */

static js_value_t *ltx2_gen_i2v (js_env_t *env, js_callback_info_t *info) {
  size_t argc = 4; js_value_t *argv[4];
  js_get_callback_info(env, info, &argc, argv, NULL, NULL);

  ltx2_ctx_t *ctx;
  js_get_value_external(env, argv[0], (void **)&ctx);

  ltx2_work_t *w = calloc(1, sizeof(ltx2_work_t));
  w->work.data = w; w->env = env; w->ctx = ctx; w->is_i2v = true;
  w->prompt     = prop_string(env, argv[1], "prompt",    true);
  if (!w->prompt) { free(w); js_value_t *u; js_get_undefined(env, &u); return u; }
  w->neg_prompt = prop_string(env, argv[1], "negPrompt", false);
  if (!w->neg_prompt) w->neg_prompt = strdup("worst quality, low quality, blurry");
  w->width   = prop_int(env, argv[1], "width",      1280);
  w->height  = prop_int(env, argv[1], "height",      720);
  w->frames  = prop_int(env, argv[1], "frames",       33);
  w->fps     = prop_int(env, argv[1], "fps",          24);
  w->seed    = prop_int64(env, argv[1], "seed", -1);
  w->init_w  = prop_int(env, argv[1], "initWidth",  1280);
  w->init_h  = prop_int(env, argv[1], "initHeight",  720);

  void *ab_data; size_t ab_len;
  js_get_arraybuffer_info(env, argv[2], &ab_data, &ab_len);
  size_t expected = (size_t)w->init_w * (size_t)w->init_h * 3;
  if (ab_len < expected) {
    free(w->prompt); free(w->neg_prompt); free(w);
    js_throw_error(env, NULL, "initImage buffer smaller than initWidth*initHeight*3");
    js_value_t *u; js_get_undefined(env, &u); return u;
  }
  w->init_data = malloc(ab_len);
  memcpy(w->init_data, ab_data, ab_len);

  js_create_reference(env, argv[3], 1, &w->callback);

  uv_loop_t *loop; js_get_env_loop(env, &loop);
  uv_queue_work(loop, &w->work, gen_execute, gen_done);

  js_value_t *u; js_get_undefined(env, &u); return u;
}

/* -----------------------------------------------------------------------
 * ltx2.freeContext(ctx)
 * --------------------------------------------------------------------- */

static js_value_t *ltx2_free_context (js_env_t *env, js_callback_info_t *info) {
  size_t argc = 1; js_value_t *argv[1];
  js_get_callback_info(env, info, &argc, argv, NULL, NULL);

  ltx2_ctx_t *ctx;
  js_get_value_external(env, argv[0], (void **)&ctx);
  if (ctx) { free_sd_ctx(ctx->sd_ctx); free(ctx); }

  js_value_t *u; js_get_undefined(env, &u); return u;
}

/* -----------------------------------------------------------------------
 * Module init
 * --------------------------------------------------------------------- */

static js_value_t *bare_ltx2_exports (js_env_t *env, js_value_t *exports) {
#define EXPORT(name, fn) \
  { js_value_t *f; js_create_function(env, name, -1, fn, NULL, &f); \
    js_set_named_property(env, exports, name, f); }

  EXPORT("createContext", ltx2_create_context)
  EXPORT("generateT2V",   ltx2_gen_t2v)
  EXPORT("generateI2V",   ltx2_gen_i2v)
  EXPORT("freeContext",   ltx2_free_context)

#undef EXPORT
  return exports;
}

BARE_MODULE(bare_ltx2, bare_ltx2_exports)
