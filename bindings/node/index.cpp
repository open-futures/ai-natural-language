/**
 * tts — Node.js N-API binding scaffold for the TTS SDK (v3.0).
 *
 * This is a scaffold — the full N-API addon is published separately as
 * the `@tts/sdk` npm package. The scaffold below shows the intended
 * surface; copy this into binding.gyp + index.js for a working addon.
 *
 * Build (theoretical):
 *   node-gyp configure && node-gyp build
 */
#include "tts/ffi.h"
#include <napi.h>

using namespace Napi;

Value Version(const CallbackInfo& info) {
    return String::New(info.Env(), tts_version());
}

Value TtfOpen(const CallbackInfo& info) {
    auto env = info.Env();
    if (!info[0].IsString()) {
        TypeError::New(env, "path must be a string").ThrowAsJavaScriptException();
        return {};
    }
    std::string path = info[0].As<String>().Utf8Value();
    tts_ttf_reader_t reader = nullptr;
    int rc = tts_ttf_open(path.c_str(), &reader);
    if (rc != 0) {
        Error::New(env, tts_last_error()).ThrowAsJavaScriptException();
        return {};
    }
    return External<tts_ttf_reader_t>::New(env, reader,
        [](Env, tts_ttf_reader_t r) { tts_ttf_close(r); });
}

Value TtfGetSection(const CallbackInfo& info) {
    auto env = info.Env();
    auto reader = info[0].As<External<tts_ttf_reader_t>>().Data();
    auto sectionId = info[1].As<Number>().Uint32Value();
    size_t need = 0;
    tts_ttf_get_section(reader, sectionId, nullptr, 0, &need);
    if (need == 0) return String::New(env, "");
    std::vector<char> buf(need);
    size_t got = 0;
    int rc = tts_ttf_get_section(reader, sectionId, buf.data(), buf.size(), &got);
    if (rc != 0) {
        Error::New(env, tts_last_error()).ThrowAsJavaScriptException();
        return {};
    }
    return String::New(env, buf.data(), got);
}

Object Init(Env env, Object exports) {
    exports.Set("version", Function::New(env, Version));
    exports.Set("ttfOpen", Function::New(env, TtfOpen));
    exports.Set("ttfGetSection", Function::New(env, TtfGetSection));
    return exports;
}

NODE_API_MODULE(tts, Init)
