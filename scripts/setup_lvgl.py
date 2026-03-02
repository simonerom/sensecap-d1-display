"""
PlatformIO pre-build script:
- Copies lv_conf.h into the LVGL library directory so it persists
  across clean builds and is found before library sources are compiled.
- Copies esp32-hal-periman.h stub into Arduino_GFX databus directory so the
  library compiles against Arduino ESP32 core 2.x (which lacks this header).
"""
import os
import shutil
Import("env")

def _copy_lv_conf(env):
    project_dir = env.get("PROJECT_DIR")
    lvgl_conf_src = os.path.join(project_dir, "include", "lv_conf.h")

    libdeps_dir = env.get("PROJECT_LIBDEPS_DIR", "")
    build_env   = env.get("PIOENV", "")
    lvgl_dir    = os.path.join(libdeps_dir, build_env, "lvgl")

    if os.path.isdir(lvgl_dir):
        dst = os.path.join(lvgl_dir, "lv_conf.h")
        shutil.copy(lvgl_conf_src, dst)
        print(f"[setup_lvgl] Copied lv_conf.h -> {dst}")
    else:
        print(f"[setup_lvgl] LVGL directory not found: {lvgl_dir}")
        print("[setup_lvgl] Run 'pio pkg install' before building.")

def _copy_periman_stub(env):
    """Copy esp32-hal-periman.h stub + esp_private/periph_ctrl.h stub into Arduino_GFX
    so it compiles against Arduino ESP32 2.x / IDF 4.x."""
    project_dir = env.get("PROJECT_DIR")
    stub_src = os.path.join(project_dir, "include", "esp32-hal-periman.h")

    libdeps_dir = env.get("PROJECT_LIBDEPS_DIR", "")
    build_env   = env.get("PIOENV", "")
    gfx_databus_dir = os.path.join(libdeps_dir, build_env,
                                    "GFX Library for Arduino", "src", "databus")

    if os.path.isdir(gfx_databus_dir):
        dst = os.path.join(gfx_databus_dir, "esp32-hal-periman.h")
        shutil.copy(stub_src, dst)
        print(f"[setup_lvgl] Copied esp32-hal-periman.h stub -> {dst}")

        # Also create esp_private/periph_ctrl.h stub referenced from Arduino_ESP32SPI.h
        esp_private_dir = os.path.join(gfx_databus_dir, "esp_private")
        os.makedirs(esp_private_dir, exist_ok=True)
        periph_stub = os.path.join(esp_private_dir, "periph_ctrl.h")
        if not os.path.exists(periph_stub):
            with open(periph_stub, "w") as f:
                f.write("// Stub: esp_private/periph_ctrl.h — not needed for SWSPI+RGB on IDF 4.x\n")
                f.write("#pragma once\n")
            print(f"[setup_lvgl] Created esp_private/periph_ctrl.h stub -> {periph_stub}")
    else:
        print(f"[setup_lvgl] Arduino_GFX databus dir not found: {gfx_databus_dir}")

# Run immediately at script load (before any build action)
# so the file is in place when library sources are compiled.
_copy_lv_conf(env)
_copy_periman_stub(env)

# Also register as pre-action to handle incremental rebuilds.
def setup_all(source, target, env):
    _copy_lv_conf(env)
    _copy_periman_stub(env)

env.AddPreAction("buildprog", setup_all)
