# tizenclaw-assets

> [!WARNING]
> **DO NOT USE THIS PROJECT AT THE MOMENT.**
> The current project has known issues and is undergoing maintenance. Please refrain from using it until this warning is removed.

Consolidated ML/AI asset package for TizenClaw.

## Contents

### ONNX Runtime
Shared `libonnxruntime.so` for on-device ML inference. Architectures: x86_64, armv7l, aarch64.

**Install path**: `/opt/usr/share/tizenclaw/lib/`

---

### RAG (Retrieval-Augmented Generation)

Pre-built knowledge databases for Tizen API semantic search:

| Database | Size | Description |
|----------|------|-------------|
| `tizen_api.db` | ~45 MB | Tizen C-API reference (35+ modules) |
| `tizen_guide.db` | ~20 MB | Tizen development guides |

**Embedding model**: `all-MiniLM-L6-v2` (ONNX, ~22MB)

**Install path**: `/opt/usr/share/tizenclaw/rag/`, `/opt/usr/share/tizenclaw/models/all-MiniLM-L6-v2/`

---

### OCR (Optical Character Recognition)

PaddleOCR PP-OCRv3 on-device text extraction engine.

| Component | File | Lite (default) | Full |
|-----------|------|------------|------|
| Detection | `det.onnx` | 2.4 MB | 2.4 MB |
| Recognition | `rec.onnx` | **13 MB** (Korean + English) | **84 MB** (Full CJK) |
| Dictionary | `ppocr_keys.txt` | 11,945 chars | 6,622 chars |

**CLI tool**: `tizenclaw-ocr`
```bash
tizenclaw-ocr <image_path> [--json]
# Output: {"texts": [{"text": "...", "confidence": 0.95, "box": [x,y,w,h]}], "count": N}
```

**Install path**: `/opt/usr/share/tizenclaw/models/ppocr/`, `/opt/usr/share/tizen-tools/cli/tizenclaw-ocr/`

---

## Build

```bash
# Default (lightweight Korean + English model)
gbs build -A x86_64 --include-all

# Full CJK model
gbs build -A x86_64 --include-all --define "ocr_model full"
```

## Deploy

```bash
./deploy.sh              # lite model (default)
./deploy.sh --full       # full CJK model
```

## License

- Apache License 2.0
- PaddleOCR models: Apache 2.0
- ONNX Runtime: MIT
- all-MiniLM-L6-v2: Apache 2.0