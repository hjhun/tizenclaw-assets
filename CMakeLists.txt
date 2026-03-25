CMAKE_MINIMUM_REQUIRED(VERSION 3.16)
PROJECT(tizenclaw-assets VERSION 1.0.0 LANGUAGES CXX)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

SET(APP_DATA_DIR "/opt/usr/share/tizenclaw" CACHE STRING "App data directory")
SET(OCR_CLI_DIR "/opt/usr/share/tizen-tools/cli/tizenclaw-ocr")

## Detect architecture
IF(NOT DEFINED TIZENCLAW_ARCH)
  SET(TIZENCLAW_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
ENDIF()

## OCR model selection (lite = Korean+English, full = CJK)
IF(NOT DEFINED OCR_MODEL)
  SET(OCR_MODEL "lite")
ENDIF()

## ONNX Runtime paths
SET(ORT_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/${TIZENCLAW_ARCH}")
SET(ORT_RUNTIME_DIR "${APP_DATA_DIR}/lib")
SET(ORT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

## ── OCR CLI binary ────────────────────────────
ADD_EXECUTABLE(tizenclaw-ocr src/tizenclaw_ocr.cc)
TARGET_INCLUDE_DIRECTORIES(tizenclaw-ocr PRIVATE ${ORT_INCLUDE_DIR})
TARGET_LINK_DIRECTORIES(tizenclaw-ocr PRIVATE ${ORT_LIB_DIR})
TARGET_LINK_LIBRARIES(tizenclaw-ocr onnxruntime)
SET_TARGET_PROPERTIES(tizenclaw-ocr PROPERTIES
  INSTALL_RPATH "${ORT_RUNTIME_DIR}"
  BUILD_WITH_INSTALL_RPATH TRUE)
INSTALL(TARGETS tizenclaw-ocr DESTINATION ${OCR_CLI_DIR})
INSTALL(FILES res/tool.md DESTINATION ${OCR_CLI_DIR})

## ── ONNX Runtime (arch-specific) ──────────────
FILE(GLOB ORT_LIBS "${ORT_LIB_DIR}/libonnxruntime.so*")
IF(ORT_LIBS)
  INSTALL(FILES ${ORT_LIBS} DESTINATION "${APP_DATA_DIR}/lib")
ENDIF()

## ── RAG databases ─────────────────────────────
FILE(GLOB RAG_DB_FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/rag/*.db")
IF(RAG_DB_FILES)
  INSTALL(FILES ${RAG_DB_FILES} DESTINATION "${APP_DATA_DIR}/rag")
ENDIF()

## ── Embedding model ───────────────────────────
INSTALL(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/data/models/all-MiniLM-L6-v2/model.onnx"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/models/all-MiniLM-L6-v2/vocab.txt"
  DESTINATION "${APP_DATA_DIR}/models/all-MiniLM-L6-v2"
  OPTIONAL)

## ── OCR models (det is always the same) ───────
INSTALL(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/models/ppocr/det.onnx"
  DESTINATION "${APP_DATA_DIR}/models/ppocr")

## Install correct rec model based on OCR_MODEL option
IF(OCR_MODEL STREQUAL "full")
  INSTALL(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/models/ppocr/rec_full.onnx"
    DESTINATION "${APP_DATA_DIR}/models/ppocr" RENAME "rec.onnx")
  INSTALL(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/models/ppocr/ppocr_keys_full.txt"
    DESTINATION "${APP_DATA_DIR}/models/ppocr" RENAME "ppocr_keys.txt")
ELSE()
  INSTALL(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/models/ppocr/rec_lite.onnx"
    DESTINATION "${APP_DATA_DIR}/models/ppocr" RENAME "rec.onnx")
  INSTALL(FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/models/ppocr/ppocr_keys_lite.txt"
    DESTINATION "${APP_DATA_DIR}/models/ppocr" RENAME "ppocr_keys.txt")
ENDIF()
