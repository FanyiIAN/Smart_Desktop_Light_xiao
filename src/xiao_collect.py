"""
capture_xiao_dataset_auto.py

Capture images from XIAO ESP32S3 Sense minimal /capture server without SD card.

Behavior:
  1. Run this script.
  2. Choose a label before capture starts.
  3. Wait 3 seconds.
  4. Capture one image every 3 seconds.
  5. Save images into the current directory under:
       ./xiao_dataset/<label>/

Controls during capture:
  Press Ctrl+C to stop.

Before running:
  pip install requests

Edit XIAO_IP below to the IP printed in Arduino Serial Monitor.
Example:
  Open capture: http://192.168.0.114/capture
"""

import time
import csv
from pathlib import Path
from datetime import datetime

import requests


# ========== CHANGE THIS ==========
XIAO_IP = "172.20.10.6"
# =================================

CAPTURE_URL = f"http://{XIAO_IP}/capture"

VALID_LABELS = {
    "1": "computer",
    "2": "empty",
    "3": "ipad",
    "4": "phone",
}

START_DELAY_SECONDS = 3
INTERVAL_SECONDS = 3

# Saves to the current working directory where you run the script
DATASET_DIR = Path.cwd() / "xiao_dataset"
METADATA_CSV = DATASET_DIR / "metadata.csv"


def now_str() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S_%f")


def ensure_metadata_file() -> None:
    DATASET_DIR.mkdir(parents=True, exist_ok=True)
    if not METADATA_CSV.exists():
        with METADATA_CSV.open("w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(["filename", "label", "timestamp", "url", "bytes"])


def append_metadata(filename: Path, label: str, size_bytes: int) -> None:
    with METADATA_CSV.open("a", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow([
            str(filename),
            label,
            datetime.now().isoformat(timespec="seconds"),
            CAPTURE_URL,
            size_bytes,
        ])


def choose_label() -> str:
    print("=" * 60)
    print("Choose label before capture:")
    print("  1 = computer         (using computer)")
    print("  2 = empty            (no one at desk)")
    print("  3 = ipad             (using iPad)")
    print("  4 = phone            (using phone)")
    print("=" * 60)

    while True:
        choice = input("Enter label number [1/2/3]: ").strip()
        if choice in VALID_LABELS:
            label = VALID_LABELS[choice]
            print(f"Selected label: {label}")
            return label

        if choice in VALID_LABELS.values():
            print(f"Selected label: {choice}")
            return choice

        print("Invalid choice. Please enter 1, 2, 3, or a valid label name.")


def capture_one(label: str, index: int) -> bool:
    label_dir = DATASET_DIR / label
    label_dir.mkdir(parents=True, exist_ok=True)

    filename = label_dir / f"{label}_{now_str()}_{index:05d}.jpg"

    try:
        response = requests.get(CAPTURE_URL, timeout=10)
    except requests.RequestException as e:
        print(f"[ERROR] Request failed: {e}")
        return False

    if response.status_code != 200:
        print(f"[ERROR] HTTP status {response.status_code}")
        return False

    if len(response.content) < 1000:
        print(f"[ERROR] Response too small: {len(response.content)} bytes")
        return False

    content_type = response.headers.get("Content-Type", "")
    if "image" not in content_type.lower() and not response.content.startswith(b"\xff\xd8"):
        print(f"[WARN] Response may not be JPEG. Content-Type={content_type}")

    filename.write_bytes(response.content)
    append_metadata(filename, label, len(response.content))
    print(f"[SAVED] {filename} ({len(response.content)} bytes)")
    return True


def main() -> None:
    ensure_metadata_file()

    print("XIAO dataset auto capture")
    print(f"Capture URL: {CAPTURE_URL}")
    print(f"Saving under: {DATASET_DIR}")
    print()

    label = choose_label()

    print()
    print(f"Capture will start in {START_DELAY_SECONDS} seconds.")
    print(f"Then it will capture 1 image every {INTERVAL_SECONDS} seconds.")
    print("Press Ctrl+C to stop.")
    print()

    time.sleep(START_DELAY_SECONDS)

    index = 0
    saved = 0

    try:
        while True:
            print(f"Capture #{index + 1} for label [{label}] ...")
            if capture_one(label, index):
                saved += 1

            index += 1
            time.sleep(INTERVAL_SECONDS)

    except KeyboardInterrupt:
        print()
        print("Stopped by user.")
        print(f"Saved {saved} images for label [{label}].")
        print(f"Dataset folder: {DATASET_DIR}")


if __name__ == "__main__":
    main()