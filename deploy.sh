#!/bin/bash
# deploy.sh — Build and deploy tizenclaw-assets
set -eo pipefail

ARCH="${ARCH:-x86_64}"
OCR_DEFINE=""

# Parse args
for arg in "$@"; do
  case $arg in
    --full) OCR_DEFINE="--define 'ocr_model full'" ;;
    --arch=*) ARCH="${arg#*=}" ;;
  esac
done

echo "═══════════════════════════════════════════"
echo "  TizenClaw Assets — Build & Deploy"
echo "  Arch: $ARCH | OCR: ${OCR_DEFINE:-lite (default)}"
echo "═══════════════════════════════════════════"

# Build
echo "[BUILD] Running gbs build..."
eval gbs build -A "$ARCH" --include-all --noinit $OCR_DEFINE

# Find RPM
RPM_DIR="$HOME/GBS-ROOT/local/repos/tizen/$ARCH/RPMS"
RPM=$(find "$RPM_DIR" -name "tizenclaw-assets-*.rpm" ! -name "*debug*" | sort -V | tail -1)
if [ -z "$RPM" ]; then
  echo "[ERROR] No tizenclaw-assets RPM found"
  exit 1
fi
echo "[  OK  ] Found: $(basename $RPM)"

# Deploy
echo "[DEPLOY] Pushing to device..."
sdb root on
sdb shell mount -o remount,rw /
sdb push "$RPM" /tmp/
sdb shell rpm -Uvh --force --nodeps "/tmp/$(basename $RPM)"
sdb shell rm -f "/tmp/$(basename $RPM)"

echo ""
echo "═══════════════════════════════════════════"
echo "  Deploy Complete!"
echo "═══════════════════════════════════════════"
echo "[TEST ] sdb shell /opt/usr/share/tizen-tools/cli/tizenclaw-ocr/tizenclaw-ocr /tmp/test.png"
