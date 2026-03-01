"""
PlatformIO pre-build script to configure LVGL:
- Copies lv_conf.h into the LVGL library directory
"""
import os
import shutil
Import("env")

def setup_lvgl_conf(source, target, env):
    project_dir = env.get("PROJECT_DIR")
    lvgl_conf_src = os.path.join(project_dir, "include", "lv_conf.h")

    # Look for the LVGL directory in PlatformIO-managed libraries
    libdeps_dir = env.get("PROJECT_LIBDEPS_DIR", "")
    build_env   = env.get("PIOENV", "")
    lvgl_dir    = os.path.join(libdeps_dir, build_env, "lvgl")

    if os.path.isdir(lvgl_dir):
        dst = os.path.join(lvgl_dir, "lv_conf.h")
        shutil.copy(lvgl_conf_src, dst)
        print(f"[setup_lvgl] Copied lv_conf.h -> {dst}")
    else:
        print(f"[setup_lvgl] LVGL directory not found: {lvgl_dir}")
        print("[setup_lvgl] Make sure you have run 'pio pkg install' before building.")

env.AddPreAction("buildprog", setup_lvgl_conf)
