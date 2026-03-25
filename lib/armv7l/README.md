# ONNX Runtime for armv7l

The Python wheel builds from this community repository contain `OrtGetApiBase` as a LOCAL symbol,
which cannot be used with dlopen/dlsym.

For armv7l on-device embedding support, a custom ONNX Runtime build with the C API (ORT_EXPORT) is needed.
Build from source: https://github.com/microsoft/onnxruntime/blob/main/BUILD.md

Without this library, TizenClaw will gracefully fall back to LLM-backend-based embedding on armv7l devices.
