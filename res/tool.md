# tizenclaw-ocr

**Description**: On-device OCR — extract text from screenshots and images using PaddleOCR PP-OCRv3.  
**Category**: Perception

## Usage

```
tizenclaw-ocr <image_path> [--json]
```

## Options

| Option | Description | Default |
|--------|-------------|---------|
| `<image_path>` | Path to input image (PNG, JPG) | (required) |
| `--json` | Output structured JSON | Enabled by default |

## Output

JSON array of detected text regions with confidence scores and bounding boxes:

```json
{
  "texts": [
    {"text": "Samsung Galaxy", "confidence": 0.95, "box": [10, 20, 200, 50]},
    {"text": "설정", "confidence": 0.91, "box": [300, 400, 380, 440]}
  ],
  "count": 2
}
```

## Languages

Supports Korean and English text recognition.

## Integration

Combine with `aurum-cli screenshot` for screen text extraction:
```bash
aurum-cli screenshot --output /tmp/screen.png
tizenclaw-ocr /tmp/screen.png
```
