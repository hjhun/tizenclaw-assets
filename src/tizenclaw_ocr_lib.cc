// Copyright 2024-2026 Samsung Electronics Co., Ltd.
// Licensed under the Apache License, Version 2.0.

#include "tizenclaw_ocr_api.h"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>
#include <cstdlib>

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
    if (!line.empty() && line.back() == '\r') line.pop_back();
    dict.push_back(line);
  }
  dict.push_back(" ");
  return dict;
}

struct Image {
  std::vector<float> data;
  int c, h, w;
};

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
        dst[(y * dst_w + x) * channels + c] =
            v00 * (1 - wy) * (1 - wx) + v01 * (1 - wy) * wx +
            v10 * wy * (1 - wx) + v11 * wy * wx;
      }
    }
  }
}

static std::vector<float> NormalizeHWCtoCHW(const float* hwc, int h, int w) {
  std::vector<float> chw(3 * h * w);
  float mean[3] = {0.485f, 0.456f, 0.406f};
  float std[3] = {0.229f, 0.224f, 0.225f};
  for (int c = 0; c < 3; c++) {
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        chw[c * h * w + y * w + x] = (hwc[(y * w + x) * 3 + c] - mean[c]) / std[c];
      }
    }
  }
  return chw;
}

struct BBox { int x, y, w, h; };

static Image DetPreprocess(const unsigned char* pixels, int img_h, int img_w, int c_in, int is_bgra,
                           int target_size, float& ratio_h, float& ratio_w) {
  float scale = static_cast<float>(target_size) / std::max(img_h, img_w);
  int new_h = ((static_cast<int>(img_h * scale) + 31) / 32) * 32;
  int new_w = ((static_cast<int>(img_w * scale) + 31) / 32) * 32;
  ratio_h = static_cast<float>(new_h) / img_h;
  ratio_w = static_cast<float>(new_w) / img_w;

  std::vector<float> float_img(img_h * img_w * 3);
  for (int i = 0; i < img_h * img_w; i++) {
      if (c_in == 4 && is_bgra) {
          float_img[i * 3 + 0] = pixels[i * c_in + 2] / 255.0f;
          float_img[i * 3 + 1] = pixels[i * c_in + 1] / 255.0f;
          float_img[i * 3 + 2] = pixels[i * c_in + 0] / 255.0f;
      } else {
          float_img[i * 3 + 0] = pixels[i * c_in + 0] / 255.0f;
          float_img[i * 3 + 1] = pixels[i * c_in + 1] / 255.0f;
          float_img[i * 3 + 2] = pixels[i * c_in + 2] / 255.0f;
      }
  }

  std::vector<float> resized(new_h * new_w * 3);
  ResizeBilinear(float_img.data(), img_h, img_w, 3, resized.data(), new_h, new_w);
  return {NormalizeHWCtoCHW(resized.data(), new_h, new_w), 3, new_h, new_w};
}

static std::vector<BBox> DetPostprocess(const float* output, int h, int w, float ratio_h, float ratio_w, int orig_h, int orig_w) {
  std::vector<BBox> boxes;
  std::vector<bool> visited(h * w, false);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      if (output[y * w + x] > 0.3f && !visited[y * w + x]) {
        int min_x = x, max_x = x, min_y = y, max_y = y;
        std::vector<std::pair<int, int>> stack;
        stack.push_back({y, x});
        int area = 0;
        while (!stack.empty()) {
          auto [cy, cx] = stack.back();
          stack.pop_back();
          if (cy < 0 || cy >= h || cx < 0 || cx >= w || visited[cy * w + cx] || output[cy * w + cx] <= 0.3f) continue;
          visited[cy * w + cx] = true;
          area++;
          min_x = std::min(min_x, cx); max_x = std::max(max_x, cx);
          min_y = std::min(min_y, cy); max_y = std::max(max_y, cy);
          stack.push_back({cy - 1, cx}); stack.push_back({cy + 1, cx});
          stack.push_back({cy, cx - 1}); stack.push_back({cy, cx + 1});
        }
        if (area < 3) continue;
        int bx = std::max(0, static_cast<int>(min_x / ratio_w));
        int by = std::max(0, static_cast<int>(min_y / ratio_h));
        int bw = std::min(orig_w - bx, static_cast<int>((max_x - min_x + 1) / ratio_w));
        int bh = std::min(orig_h - by, static_cast<int>((max_y - min_y + 1) / ratio_h));
        if (bw > 2 && bh > 2) boxes.push_back({bx, by, bw, bh});
      }
    }
  }
  return boxes;
}

static std::vector<float> RecPreprocess(const unsigned char* pixels, int img_h, int img_w, int c_in, int is_bgra,
                                        const BBox& box, int rec_h, int& rec_w_out) {
  int crop_w = box.w, crop_h = box.h;
  std::vector<float> crop_float(crop_h * crop_w * 3);
  for (int y = 0; y < crop_h; y++) {
    for (int x = 0; x < crop_w; x++) {
      int sy = box.y + y, sx = box.x + x;
      if (sy >= img_h || sx >= img_w) continue;
      int src_idx = (sy * img_w + sx) * c_in;
      int dst_idx = (y * crop_w + x) * 3;
      if (c_in == 4 && is_bgra) {
          crop_float[dst_idx + 0] = pixels[src_idx + 2] / 255.0f;
          crop_float[dst_idx + 1] = pixels[src_idx + 1] / 255.0f;
          crop_float[dst_idx + 2] = pixels[src_idx + 0] / 255.0f;
      } else {
          crop_float[dst_idx + 0] = pixels[src_idx + 0] / 255.0f;
          crop_float[dst_idx + 1] = pixels[src_idx + 1] / 255.0f;
          crop_float[dst_idx + 2] = pixels[src_idx + 2] / 255.0f;
      }
    }
  }

  int rec_w = std::max(32, ((std::max(1, static_cast<int>(rec_h * (static_cast<float>(crop_w) / crop_h))) + 31) / 32) * 32);
  rec_w_out = rec_w;
  std::vector<float> resized(rec_h * rec_w * 3, 0.0f);
  int actual_w = std::min(rec_w, std::max(1, static_cast<int>(rec_h * (static_cast<float>(crop_w) / crop_h))));
  ResizeBilinear(crop_float.data(), crop_h, crop_w, 3, resized.data(), rec_h, actual_w);

  std::vector<float> chw(3 * rec_h * rec_w, -1.0f);
  for (int c = 0; c < 3; c++)
    for (int y = 0; y < rec_h; y++)
      for (int x = 0; x < actual_w; x++)
        chw[c * rec_h * rec_w + y * rec_w + x] = (resized[(y * rec_w + x) * 3 + c] - 0.5f) / 0.5f;
  return chw;
}

struct RecResult { std::string text; float confidence; };

static RecResult RecPostprocess(const float* output, int seq_len, int char_num, const std::vector<std::string>& dict) {
  std::string text; float total_score = 0; int count = 0, last_idx = 0;
  for (int t = 0; t < seq_len; t++) {
    int max_idx = 0; float max_val = output[t * char_num];
    for (int c = 1; c < char_num; c++) {
      if (output[t * char_num + c] > max_val) { max_val = output[t * char_num + c]; max_idx = c; }
    }
    if (max_idx != 0 && max_idx != last_idx) {
      if (max_idx > 0 && max_idx <= static_cast<int>(dict.size())) text += dict[max_idx - 1];
      total_score += max_val; count++;
    }
    last_idx = max_idx;
  }
  return {text, count > 0 ? total_score / count : 0.0f};
}

struct OcrEngine {
  Ort::Env env;
  Ort::SessionOptions opts;
  Ort::Session det_session;
  Ort::Session rec_session;
  std::vector<std::string> dict;
  Ort::MemoryInfo mem_info;
  Ort::AllocatorWithDefaultOptions alloc;

  OcrEngine(const std::string& model_dir)
      : env(ORT_LOGGING_LEVEL_WARNING, "tizenclaw-ocr"),
        det_session(nullptr), rec_session(nullptr),
        mem_info(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
    opts.SetIntraOpNumThreads(2);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    det_session = Ort::Session(env, (model_dir + "/det.onnx").c_str(), opts);
    rec_session = Ort::Session(env, (model_dir + "/rec.onnx").c_str(), opts);
    dict = LoadDict(model_dir + "/ppocr_keys.txt");
  }
};

extern "C" void* tizenclaw_ocr_create(const char* model_dir) {
  try { return new OcrEngine(model_dir); } catch (...) { return nullptr; }
}

extern "C" void tizenclaw_ocr_destroy(void* handle) {
  delete static_cast<OcrEngine*>(handle);
}

extern "C" char* tizenclaw_ocr_analyze_buffer(void* handle, const unsigned char* pixels, int w, int h, int c, int is_bgra) {
  if (!handle || !pixels) return nullptr;
  auto engine = static_cast<OcrEngine*>(handle);
  
  float ratio_h, ratio_w;
  auto det_img = DetPreprocess(pixels, h, w, c, is_bgra, 960, ratio_h, ratio_w);
  std::array<int64_t, 4> det_shape = {1, 3, det_img.h, det_img.w};
  Ort::Value det_tensor = Ort::Value::CreateTensor<float>(
      engine->mem_info, det_img.data.data(), det_img.data.size(), det_shape.data(), 4);
      
  auto det_in_name = engine->det_session.GetInputNameAllocated(0, engine->alloc);
  auto det_out_name = engine->det_session.GetOutputNameAllocated(0, engine->alloc);
  const char* det_in[] = {det_in_name.get()}; const char* det_out[] = {det_out_name.get()};
  
  auto det_results = engine->det_session.Run(Ort::RunOptions{nullptr}, det_in, &det_tensor, 1, det_out, 1);
  auto det_shape_out = det_results[0].GetTensorTypeAndShapeInfo().GetShape();
  auto boxes = DetPostprocess(det_results[0].GetTensorData<float>(), static_cast<int>(det_shape_out[2]), 
                              static_cast<int>(det_shape_out[3]), ratio_h, ratio_w, h, w);

  std::string json = "{\"texts\": [";
  bool first = true;
  int count = 0;
  
  for (auto& box : boxes) {
    int rec_w_out;
    auto rec_data = RecPreprocess(pixels, h, w, c, is_bgra, box, 48, rec_w_out);
    std::array<int64_t, 4> rec_shape = {1, 3, 48, rec_w_out};
    Ort::Value rec_tensor = Ort::Value::CreateTensor<float>(
        engine->mem_info, rec_data.data(), rec_data.size(), rec_shape.data(), 4);
        
    auto rec_in_name = engine->rec_session.GetInputNameAllocated(0, engine->alloc);
    auto rec_out_name = engine->rec_session.GetOutputNameAllocated(0, engine->alloc);
    const char* rec_in[] = {rec_in_name.get()}; const char* rec_out[] = {rec_out_name.get()};
    
    auto rec_results = engine->rec_session.Run(Ort::RunOptions{nullptr}, rec_in, &rec_tensor, 1, rec_out, 1);
    auto rec_shape_out = rec_results[0].GetTensorTypeAndShapeInfo().GetShape();
    auto res = RecPostprocess(rec_results[0].GetTensorData<float>(), static_cast<int>(rec_shape_out[1]), 
                              static_cast<int>(rec_shape_out[2]), engine->dict);

    if (!res.text.empty() && res.confidence > 0.5f) {
      if (!first) json += ", ";
      json += "{\"text\": \"" + EscapeJson(res.text) + "\", \"confidence\": " + 
               std::to_string(res.confidence) + ", \"box\": [" + 
               std::to_string(box.x) + ", " + std::to_string(box.y) + ", " + 
               std::to_string(box.w) + ", " + std::to_string(box.h) + "]}";
      first = false; count++;
    }
  }
  json += "], \"count\": " + std::to_string(count) + "}";
  
  char* out_str = (char*)malloc(json.length() + 1);
  strcpy(out_str, json.c_str());
  return out_str;
}
