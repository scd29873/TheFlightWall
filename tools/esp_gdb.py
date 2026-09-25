"""
Debug with Espressif's standalone GDB instead of the one inside the compiler
toolchain. Used as a post: extra_script by the waveshare_s3_matrix envs.

For Arduino projects PlatformIO launches the GDB bundled with the toolchain
(toolchain-xtensa-esp32s3 8.4.0+2021r2-patch5), and that build is linked against
Python 2.7, which current Linux releases no longer ship. On Ubuntu 24.04 it
dies before it starts:

    xtensa-esp32s3-elf-gdb: error while loading shared libraries:
    libpython2.7.so.1.0: cannot open shared object file

The env installs espressif/tool-xtensa-esp-elf-gdb through platform_packages,
and this points $GDB -- which is where `pio debug` and VS Code's PIO Debug get
the client from -- at that package's per-chip launcher. The launcher picks a
build matching the Python 3 it finds, or one without Python. 11.2 rather than
12.1 because the platform itself pins 11.2 on Windows, where 12.1 cannot start
the GDB server in pipe mode (platform.py, the esp-idf branch).

Only a launcher that is actually there is used; otherwise $GDB stays as the
platform set it, so a package whose layout differs costs nothing but this
workaround.
"""
import os

Import("env")  # noqa: F821 -- provided by PlatformIO

pkg = env.PioPlatform().get_package_dir("tool-xtensa-esp-elf-gdb")  # noqa: F821
if pkg:
    gdb = os.path.join(pkg, "bin", "xtensa-%s-elf-gdb" % env.BoardConfig().get("build.mcu"))  # noqa: F821
    for candidate in (gdb, gdb + ".exe"):
        if os.path.isfile(candidate):
            env.Replace(GDB=candidate)  # noqa: F821
            break
