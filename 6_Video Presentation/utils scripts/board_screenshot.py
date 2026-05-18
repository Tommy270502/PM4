from pathlib import Path
from datetime import datetime
import subprocess
import shutil
import sys

from PIL import Image


# =========================
# User configuration
# =========================

WIDTH = 240
HEIGHT = 320
BYTES_PER_PIXEL = 4  # ARGB8888

FRAMEBUFFER_ADDRESS = "0xD0000000"
FRAMEBUFFER_SIZE = WIDTH * HEIGHT * BYTES_PER_PIXEL

OUTPUT_DIR = Path(__file__).resolve().parents[1] / "img" / "screenshots"
KEEP_RAW_FILE = False

STM32_CLI = r"C:\ST\STM32CubeIDE_1.19.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.200.202503041107\tools\bin\STM32_Programmer_CLI.exe"


# =========================
# Screenshot capture
# =========================

def capture_framebuffer(raw_file: Path) -> None:
    cmd = [
        STM32_CLI,
        "-c",
        "port=SWD",
        "mode=HOTPLUG",
        "--upload",
        FRAMEBUFFER_ADDRESS,
        str(FRAMEBUFFER_SIZE),
        str(raw_file),
    ]

    print("Reading framebuffer from STM32...")
    print(" ".join(f'"{x}"' if " " in x else x for x in cmd))

    try:
        result = subprocess.run(
            cmd,
            check=True,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except FileNotFoundError:
        print()
        print("ERROR: STM32_Programmer_CLI.exe was not found.")
        print("Edit STM32_CLI in this script and set the full path manually.")
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print()
        print("ERROR: STM32CubeProgrammer failed.")
        print()
        print("STDOUT:")
        print(e.stdout)
        print()
        print("STDERR:")
        print(e.stderr)
        sys.exit(1)

    print(result.stdout)

    if not raw_file.exists():
        print(f"ERROR: Raw file was not created: {raw_file}")
        sys.exit(1)

    actual_size = raw_file.stat().st_size
    if actual_size != FRAMEBUFFER_SIZE:
        print(f"ERROR: Unexpected raw file size: {actual_size} bytes")
        print(f"Expected: {FRAMEBUFFER_SIZE} bytes")
        sys.exit(1)


# =========================
# ARGB8888 -> PNG conversion
# =========================

def convert_argb8888_to_png(raw_file: Path, png_file: Path) -> None:
    print("Converting framebuffer to PNG...")

    data = raw_file.read_bytes()

    img = Image.new("RGB", (WIDTH, HEIGHT))
    pixels = img.load()

    for y in range(HEIGHT):
        for x in range(WIDTH):
            i = 4 * (y * WIDTH + x)

            # ARGB8888 in little-endian memory order:
            # byte 0 = B
            # byte 1 = G
            # byte 2 = R
            # byte 3 = A
            b = data[i + 0]
            g = data[i + 1]
            r = data[i + 2]
            # a = data[i + 3]  # unused

            pixels[x, y] = (r, g, b)

    img = img.rotate(180, expand=True) # Rotate for flipped display
    img.save(png_file)
    print(f"Saved screenshot: {png_file}")


# =========================
# Main
# =========================

def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")

    raw_file = OUTPUT_DIR / f"lcd_{timestamp}.bin"
    png_file = OUTPUT_DIR / f"lcd_{timestamp}.png"

    capture_framebuffer(raw_file)
    convert_argb8888_to_png(raw_file, png_file)

    if not KEEP_RAW_FILE:
        raw_file.unlink(missing_ok=True)

    print("Done.")


if __name__ == "__main__":
    main()