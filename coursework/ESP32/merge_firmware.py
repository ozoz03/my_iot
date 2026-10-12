Import("env")
import os

# ═══════════════════════════════════════════════════════════
# Автоматичний merge_bin після кожної збірки.
# Склеює bootloader + таблицю розділів + програму в merged.bin,
# який Wokwi вантажить як цілий образ (інакше 0x8000 порожня і OTA не бачить слотів).
#
# Шляхи й параметри беремо з env — нічого не хардкодимо.
# Ставиться в platformio.ini:  extra_scripts = post:merge_firmware.py
# ═══════════════════════════════════════════════════════════

def merge_bin(source, target, env):
    build_dir = env.subst("$BUILD_DIR")          # .pio/build/<env>
    merged    = os.path.join(build_dir, "merged.bin")

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    # Саме firmware.bin — це те, що щойно зібрав PlatformIO.
    # firmware_1_1.bin — ваша ручна копія для S3, вона може бути від іншої
    # збірки. Склеїш її — Wokwi завантажить не той код, що ти щойно зібрав.
    firmware   = os.path.join(build_dir, "firmware.bin")

    # Параметри флешу беремо з board — не вгадуємо
    flash_mode = env.subst("$BOARD_FLASH_MODE") or "dio"
    # розмір флешу: у більшості ESP32 DevKit — 4MB
    flash_size = "4MB"

    # Адреса bootloader: на ESP32 — 0x1000, на S3/C3 — 0x0.
    # Беремо з env, якщо задано, інакше 0x1000 для класичного ESP32.
    boot_addr = env.subst("$ESP32_APP_OFFSET")   # не завжди визначено
    boot_addr = "0x1000"                         # ESP32 DevKit V1

    # Шлях до esptool з пакета PlatformIO — той самий, що працює вручну
    esptool = os.path.join(
        env.subst("$PROJECT_PACKAGES_DIR"),
        "tool-esptoolpy", "esptool.py"
    )
    python = env.subst("$PYTHONEXE")

    cmd = [
        python, esptool,
        "--chip", "esp32",
        "merge_bin",
        "-o", merged,
        "--flash_mode", flash_mode,
        "--flash_size", flash_size,
        boot_addr,  bootloader,
        "0x8000",   partitions,
        "0x10000",  firmware,
    ]

    print("=" * 60)
    print("[merge] Склеюю merged.bin для Wokwi...")
    env.Execute(env.VerboseAction(" ".join('"%s"' % c if " " in c else c for c in cmd),
                                  "Merging into merged.bin"))
    print("[merge] Готово: " + merged)
    print("=" * 60)


# Запускати ПІСЛЯ того, як зібрано firmware.bin
env.AddPostAction("$BUILD_DIR/firmware.bin", merge_bin)