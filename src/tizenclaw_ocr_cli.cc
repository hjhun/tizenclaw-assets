// Copyright 2024-2026 Samsung Electronics Co., Ltd.
// Licensed under the Apache License, Version 2.0.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "tizenclaw_ocr_api.h"
#include <iostream>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: tizenclaw-ocr <image_path> [--json]\n";
    return 1;
  }
  std::string image_path = argv[1];
  int img_w, img_h, img_c;
  unsigned char* pixels = stbi_load(image_path.c_str(), &img_w, &img_h, &img_c, 3);
  if (!pixels) {
    std::cerr << "{\"error\": \"Failed to load image: " << image_path << "\"}\n";
    return 1;
  }

  void* handle = tizenclaw_ocr_create("/opt/usr/share/tizenclaw/models/ppocr");
  if (!handle) {
    stbi_image_free(pixels);
    std::cerr << "{\"error\": \"Failed to initialize OCR engine\"}\n";
    return 1;
  }

  char* output = tizenclaw_ocr_analyze_buffer(handle, pixels, img_w, img_h, 3, 0);
  if (output) {
    std::cout << output << "\n";
    free(output);
  } else {
    std::cerr << "{\"error\": \"Failed to analyze image\"}\n";
  }

  tizenclaw_ocr_destroy(handle);
  stbi_image_free(pixels);
  return 0;
}
