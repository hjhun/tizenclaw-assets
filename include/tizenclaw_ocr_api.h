#ifndef TIZENCLAW_OCR_API_H
#define TIZENCLAW_OCR_API_H

#ifdef __cplusplus
extern "C" {
#endif

// Returns a handle to the OCR engine context. NULL on failure.
void* tizenclaw_ocr_create(const char* model_dir);

// Destroys the OCR engine
void tizenclaw_ocr_destroy(void* handle);

// Analyzes the pixel buffer.
// pixels: raw byte buffer.
// w, h: image dimensions.
// c: channels (e.g. 4 for RGBA).
// is_bgra: 1 if channels are BGRA, 0 if RGBA (only applies if c=4).
// Returns a dynamically allocated JSON string pointer. The caller MUST free() it.
char* tizenclaw_ocr_analyze_buffer(void* handle, const unsigned char* pixels, int w, int h, int c, int is_bgra);

#ifdef __cplusplus
}
#endif

#endif // TIZENCLAW_OCR_API_H
