// Copyright 2024-2026 Samsung Electronics Co., Ltd.
// Licensed under the Apache License, Version 2.0.
//
// tizenclaw-ocr: On-device OCR using PaddleOCR PP-OCRv3 ONNX models.
// Uses stb_image for image loading (no OpenCV dependency).
// Pipeline: Detection → Crop → Recognition → JSON output.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

static constexpr const char* kModelDir =
    "/opt/usr/share/tizenclaw/models/ppocr";

// ── Utility ─────────────────────────────────────────

static std::string EscapeJson(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
}

static std::vector<std::string> LoadDict(const std::string& path) {
  std::vector<std::string> dict;
  std::ifstream ifs(path);
  std::string line;
  while (std::getline(ifs, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    dict.push_back(line);
  }
  dict.push_back(" ");  // blank token
  return dict;
}

// ── Image operations (no OpenCV) ────────────────────

struct Image {
  std::vector<float> data;  // CHW, normalized
  int c, h, w;
};

// Bilinear resize for float HWC image
static void ResizeBilinear(const float* src, int src_h, int src_w, int channels,
                           float* dst, int dst_h, int dst_w) {
  float scale_y = static_cast<float>(src_h) / dst_h;
  float scale_x = static_cast<float>(src_w) / dst_w;
  for (int y = 0; y < dst_h; y++) {
    float fy = (y + 0.5f) * scale_y - 0.5f;
    int y0 = std::max(0, static_cast<int>(std::floor(fy)));
    int y1 = std::min(src_h - 1, y0 + 1);
    float wy = fy - y0;
    for (int x = 0; x < dst_w; x++) {
      float fx = (x + 0.5f) * scale_x - 0.5f;
      int x0 = std::max(0, static_cast<int>(std::floor(fx)));
      int x1 = std::min(src_w - 1, x0 + 1);
      float wx = fx - x0;
      for (int c = 0; c < channels; c++) {
        float v00 = src[(y0 * src_w + x0) * channels + c];
        float v01 = src[(y0 * src_w + x1) * channels + c];
        float v10 = src[(y1 * src_w + x0) * channels + c];
        float v11 = src[(y1 * src_w + x1) * channels + c];
        float v = v00 * (1 - wy) * (1 - wx) + v01 * (1 - wy) * wx +
                  v10 * wy * (1 - wx) + v11 * wy * wx;
        dst[(y * dst_w + x) * channels + c] = v;
      }
    }
  }
}

// HWC float [0,1] → CHW float normalized
static std::vector<float> NormalizeHWCtoCHW(const float* hwc, int h, int w,
                                            const float mean[3],
                                            const float std_dev[3]) {
  std::vector<float> chw(3 * h * w);
  for (int c = 0; c < 3; c++) {
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        float v = hwc[(y * w + x) * 3 + c];
        chw[c * h * w + y * w + x] = (v - mean[c]) / std_dev[c];
      }
    }
  }
  return chw;
}

// ── Detection ───────────────────────────────────────

struct BBox {
  int x, y, w, h;
};

static Image DetPreprocess(const unsigned char* pixels, int img_h,
                           int img_w, int target_size,
                           float& ratio_h, float& ratio_w) {
  float scale = static_cast<float>(target_size) / std::max(img_h, img_w);
  int new_h = ((static_cast<int>(img_h * scale) + 31) / 32) * 32;
  int new_w = ((static_cast<int>(img_w * scale) + 31) / 32) * 32;
  ratio_h = static_cast<float>(new_h) / img_h;
  ratio_w = static_cast<float>(new_w) / img_w;

  // Convert uint8 RGB to float [0,1]
  std::vector<float> float_img(img_h * img_w * 3);
  for (int i = 0; i < img_h * img_w * 3; i++) {
    float_img[i] = pixels[i] / 255.0f;
  }

  // Resize
  std::vector<float> resized(new_h * new_w * 3);
  ResizeBilinear(float_img.data(), img_h, img_w, 3,
                 resized.data(), new_h, new_w);

  // Normalize HWC -> CHW
  float mean[3] = {0.485f, 0.456f, 0.406f};
  float std_dev[3] = {0.229f, 0.224f, 0.225f};
  auto chw = NormalizeHWCtoCHW(resized.data(), new_h, new_w, mean, std_dev);

  return {chw, 3, new_h, new_w};
}

static std::vector<BBox> DetPostprocess(const float* output, int h, int w,
                                        float ratio_h, float ratio_w,
                                        int orig_h, int orig_w,
                                        float thresh = 0.3f,
                                        int min_size = 3) {
  // Simple connected-component-like approach: scan for regions above threshold
  // and merge into bounding boxes using run-length encoding
  std::vector<BBox> boxes;
  std::vector<bool> visited(h * w, false);

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      if (output[y * w + x] > thresh && !visited[y * w + x]) {
        // Flood fill to find connected region
        int min_x = x, max_x = x, min_y = y, max_y = y;
        std::vector<std::pair<int, int>> stack;
        stack.push_back({y, x});
        int area = 0;
        while (!stack.empty()) {
          auto [cy, cx] = stack.back();
          stack.pop_back();
          if (cy < 0 || cy >= h || cx < 0 || cx >= w) continue;
          if (visited[cy * w + cx]) continue;
          if (output[cy * w + cx] <= thresh) continue;
          visited[cy * w + cx] = true;
          area++;
          min_x = std::min(min_x, cx);
          max_x = std::max(max_x, cx);
          min_y = std::min(min_y, cy);
          max_y = std::max(max_y, cy);
          stack.push_back({cy - 1, cx});
          stack.push_back({cy + 1, cx});
          stack.push_back({cy, cx - 1});
          stack.push_back({cy, cx + 1});
        }
        if (area < min_size) continue;

        // Scale back to original image coordinates
        int bx = static_cast<int>(min_x / ratio_w);
        int by = static_cast<int>(min_y / ratio_h);
        int bw = static_cast<int>((max_x - min_x + 1) / ratio_w);
        int bh = static_cast<int>((max_y - min_y + 1) / ratio_h);
        bx = std::max(0, bx);
        by = std::max(0, by);
        bw = std::min(bw, orig_w - bx);
        bh = std::min(bh, orig_h - by);
        if (bw > 2 && bh > 2) {
          boxes.push_back({bx, by, bw, bh});
        }
      }
    }
  }
  return boxes;
}

// ── Recognition ─────────────────────────────────────

static std::vector<float> RecPreprocess(const unsigned char* pixels,
                                        int img_h, int img_w,
                                        const BBox& box,
                                        int rec_h, int& rec_w_out) {
  // Crop region from original image
  int crop_w = box.w, crop_h = box.h;
  std::vector<float> crop_float(crop_h * crop_w * 3);
  for (int y = 0; y < crop_h; y++) {
    for (int x = 0; x < crop_w; x++) {
      int sy = box.y + y, sx = box.x + x;
      if (sy >= img_h || sx >= img_w) continue;
      for (int c = 0; c < 3; c++) {
        crop_float[(y * crop_w + x) * 3 + c] =
            pixels[(sy * img_w + sx) * 3 + c] / 255.0f;
      }
    }
  }

  // Resize to rec_h maintaining aspect ratio
  float aspect = static_cast<float>(crop_w) / crop_h;
  int rec_w = std::max(1, static_cast<int>(rec_h * aspect));
  rec_w = ((rec_w + 31) / 32) * 32;  // pad to multiple of 32
  rec_w = std::max(32, rec_w);
  rec_w_out = rec_w;

  std::vector<float> resized(rec_h * rec_w * 3, 0.0f);
  int actual_w = std::max(1, static_cast<int>(rec_h * aspect));
  actual_w = std::min(actual_w, rec_w);
  ResizeBilinear(crop_float.data(), crop_h, crop_w, 3,
                 resized.data(), rec_h, actual_w);

  // Normalize: (x - 0.5) / 0.5 = 2x - 1
  // Then convert HWC -> CHW
  std::vector<float> chw(3 * rec_h * rec_w, -1.0f);  // pad with -1
  for (int c = 0; c < 3; c++) {
    for (int y = 0; y < rec_h; y++) {
      for (int x = 0; x < actual_w; x++) {
        float v = resized[(y * rec_w + x) * 3 + c];
        chw[c * rec_h * rec_w + y * rec_w + x] = (v - 0.5f) / 0.5f;
      }
    }
  }
  return chw;
}

struct RecResult {
  std::string text;
  float confidence;
};

static RecResult RecPostprocess(const float* output, int seq_len,
                                int char_num,
                                const std::vector<std::string>& dict) {
  std::string text;
  float total_score = 0;
  int count = 0;
  int last_idx = 0;

  for (int t = 0; t < seq_len; t++) {
    int max_idx = 0;
    float max_val = output[t * char_num];
    for (int c = 1; c < char_num; c++) {
      float v = output[t * char_num + c];
      if (v > max_val) {
        max_val = v;
        max_idx = c;
      }
    }
    if (max_idx != 0 && max_idx != last_idx) {
      if (max_idx > 0 && max_idx <= static_cast<int>(dict.size())) {
        text += dict[max_idx - 1];
      }
      total_score += max_val;
      count++;
    }
    last_idx = max_idx;
  }
  float avg_score = count > 0 ? total_score / count : 0.0f;
  return {text, avg_score};
}

// ── Main ────────────────────────────────────────────

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: tizenclaw-ocr <image_path> [--json]\n";
    return 1;
  }

  std::string image_path = argv[1];
  std::string model_dir = kModelDir;

  // Load image via stb_image (always RGB)
  int img_w, img_h, img_c;
  unsigned char* pixels = stbi_load(image_path.c_str(), &img_w, &img_h,
                                    &img_c, 3);
  if (!pixels) {
    std::cerr << "{\"error\": \"Failed to load image: "
              << image_path << "\"}\n";
    return 1;
  }

  // Load dictionary
  auto dict = LoadDict(model_dir + "/ppocr_keys.txt");
  if (dict.size() <= 1) {
    stbi_image_free(pixels);
    std::cerr << "{\"error\": \"Failed to load dictionary\"}\n";
    return 1;
  }

  // ONNX Runtime
  Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "tizenclaw-ocr");
  Ort::SessionOptions opts;
  opts.SetIntraOpNumThreads(2);
  opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

  auto mem_info = Ort::MemoryInfo::CreateCpu(
      OrtArenaAllocator, OrtMemTypeDefault);
  Ort::AllocatorWithDefaultOptions alloc;

  // ── Detection ──
  std::string det_path = model_dir + "/det.onnx";
  Ort::Session det_session(env, det_path.c_str(), opts);

  float ratio_h, ratio_w;
  auto det_img = DetPreprocess(pixels, img_h, img_w, 960, ratio_h, ratio_w);

  std::array<int64_t, 4> det_shape = {1, 3, det_img.h, det_img.w};
  Ort::Value det_tensor = Ort::Value::CreateTensor<float>(
      mem_info, det_img.data.data(), det_img.data.size(),
      det_shape.data(), 4);

  auto det_in_name = det_session.GetInputNameAllocated(0, alloc);
  auto det_out_name = det_session.GetOutputNameAllocated(0, alloc);
  const char* det_in[] = {det_in_name.get()};
  const char* det_out[] = {det_out_name.get()};

  auto det_results = det_session.Run(
      Ort::RunOptions{nullptr}, det_in, &det_tensor, 1, det_out, 1);

  auto det_shape_out = det_results[0].GetTensorTypeAndShapeInfo().GetShape();
  int out_h = static_cast<int>(det_shape_out[2]);
  int out_w = static_cast<int>(det_shape_out[3]);
  const float* det_data = det_results[0].GetTensorData<float>();

  auto boxes = DetPostprocess(det_data, out_h, out_w,
                               ratio_h, ratio_w, img_h, img_w);

  // ── Recognition ──
  std::string rec_path = model_dir + "/rec.onnx";
  Ort::Session rec_session(env, rec_path.c_str(), opts);

  std::vector<RecResult> results;
  std::vector<BBox> valid_boxes;

  for (auto& box : boxes) {
    int rec_w_out;
    auto rec_data = RecPreprocess(pixels, img_h, img_w, box, 48, rec_w_out);

    std::array<int64_t, 4> rec_shape = {1, 3, 48, rec_w_out};
    Ort::Value rec_tensor = Ort::Value::CreateTensor<float>(
        mem_info, rec_data.data(), rec_data.size(),
        rec_shape.data(), 4);

    auto rec_in_name = rec_session.GetInputNameAllocated(0, alloc);
    auto rec_out_name = rec_session.GetOutputNameAllocated(0, alloc);
    const char* rec_in[] = {rec_in_name.get()};
    const char* rec_out[] = {rec_out_name.get()};

    auto rec_results = rec_session.Run(
        Ort::RunOptions{nullptr}, rec_in, &rec_tensor, 1, rec_out, 1);

    auto rec_shape_out =
        rec_results[0].GetTensorTypeAndShapeInfo().GetShape();
    int seq_len = static_cast<int>(rec_shape_out[1]);
    int char_num = static_cast<int>(rec_shape_out[2]);
    const float* rec_out_data = rec_results[0].GetTensorData<float>();

    auto res = RecPostprocess(rec_out_data, seq_len, char_num, dict);
    if (!res.text.empty() && res.confidence > 0.5f) {
      results.push_back(res);
      valid_boxes.push_back(box);
    }
  }

  stbi_image_free(pixels);

  // ── JSON output ──
  std::cout << "{\"texts\": [";
  for (size_t i = 0; i < results.size(); i++) {
    if (i > 0) std::cout << ", ";
    auto& b = valid_boxes[i];
    std::cout << "{\"text\": \"" << EscapeJson(results[i].text)
              << "\", \"confidence\": "
              << std::fixed << results[i].confidence
              << ", \"box\": [" << b.x << ", " << b.y
              << ", " << b.w << ", " << b.h << "]}";
  }
  std::cout << "], \"count\": " << results.size() << "}\n";

  return 0;
}
