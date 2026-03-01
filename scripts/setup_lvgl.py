"""
PlatformIO pre-build script to configure LVGL:
- Copies lv_conf.h into the LVGL library directory so it persists
  across clean builds and is found before library sources are compiled.
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

# Run immediately at script load (before any build action)
# so the file is in place when library sources are compiled.
_copy_lv_conf(env)

# Also register as pre-action to handle incremental rebuilds.
def setup_lvgl_conf(source, target, env):
    _copy_lv_conf(env)

env.AddPreAction("buildprog", setup_lvgl_conf)
