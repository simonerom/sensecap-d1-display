"""
Script pre-build PlatformIO per configurare LVGL:
- Copia lv_conf.h nella directory delle librerie LVGL
"""
import os
import shutil
Import("env")

def setup_lvgl_conf(source, target, env):
    project_dir = env.get("PROJECT_DIR")
    lvgl_conf_src = os.path.join(project_dir, "include", "lv_conf.h")

    # Cerca la directory LVGL nelle librerie gestite da PlatformIO
    libdeps_dir = env.get("PROJECT_LIBDEPS_DIR", "")
    build_env   = env.get("PIOENV", "")
    lvgl_dir    = os.path.join(libdeps_dir, build_env, "lvgl")

    if os.path.isdir(lvgl_dir):
        dst = os.path.join(lvgl_dir, "lv_conf.h")
        shutil.copy(lvgl_conf_src, dst)
        print(f"[setup_lvgl] Copiato lv_conf.h -> {dst}")
    else:
        print(f"[setup_lvgl] Directory LVGL non trovata: {lvgl_dir}")
        print("[setup_lvgl] Assicurati di aver eseguito 'pio pkg install' prima del build.")

env.AddPreAction("buildprog", setup_lvgl_conf)
