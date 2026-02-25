#!/usr/bin/env bash
set -euo pipefail

IMAGE_NAME="${IMAGE_NAME:-myos-buildenv}"
QEMU_BIN="${QEMU_BIN:-qemu-system-x86_64}"
QEMU_RAM="${QEMU_RAM:-256M}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ISO_PATH="$ROOT_DIR/dist/x86_64/kernel.iso"

if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
  echo "Erreur: $QEMU_BIN introuvable dans le PATH."
  exit 1
fi

if ! sudo docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
  echo "Erreur: image Docker '$IMAGE_NAME' introuvable."
  echo "Construis-la une fois avec:"
  echo "  sudo docker build buildenv -t $IMAGE_NAME"
  exit 1
fi

echo "[1/2] Build du kernel dans Docker..."
sudo docker run --rm -t \
  -v "$ROOT_DIR:/root/env" \
  "$IMAGE_NAME" \
  bash -lc "make build-x86_64"

if [ ! -f "$ISO_PATH" ]; then
  echo "Erreur: ISO non générée: $ISO_PATH"
  exit 1
fi

echo "[2/2] Lancement QEMU..."
exec "$QEMU_BIN" -cdrom "$ISO_PATH" -m "$QEMU_RAM" -display sdl
