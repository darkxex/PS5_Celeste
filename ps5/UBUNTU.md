# Building on Ubuntu

What a fresh Ubuntu 26.04 PC needed before `ps5/tools/bootstrap.sh` and
`ps5/tools/build.sh` went through, beyond what the bootstrap's check names.

## In one command

Everything the bootstrap's check does not name, for the LLVM version installed
(install `llvm` first, below, if `llvm-config` is missing):

```bash
v=$(llvm-config --version | cut -d. -f1) && sudo apt install libllvmspirvlib-$v-dev llvm-spirv-$v libclc-$v-dev libclang-$v-dev libclang-cpp$v-dev spirv-tools-dev
```

The sections below say why each is needed.

## What the check names

`ps5/tools/bootstrap.sh --check` reported these missing, and they install from
Ubuntu's own packages (numpy and mako from apt: `pip install --user` is refused on
the system's Python):

```bash
sudo apt install clang lld llvm glslang-tools curl python3-numpy python3-mako
```

## What the check does not name

The RADV build (`tools/build-radv.sh` in PS5_Vulkan) first builds Mesa's `mesa_clc`
for the PC, which compiles RADV's OpenCL C kernels. It needs the SPIR-V translator,
libclc and Clang's development files, all of the same major version as the host's
LLVM (21 on Ubuntu 26.04). Without them the bootstrap reports the RADV archive
missing, after this in its log:

```text
Run-time dependency llvmspirvlib found: NO (tried pkgconfig and cmake)
.deps/work/radv-src/meson.build:2102:21: ERROR: Dependency "LLVMSPIRVLib" not found, tried pkgconfig and cmake
```

What I installed, after which RADV built:

```bash
sudo apt install libllvmspirvlib-21-dev llvm-spirv-21 libclc-21-dev \
                 libclang-21-dev libclang-cpp21-dev spirv-tools-dev
```

`LLVMSPIRVLib` is the one the error names; the others went in with it in one step,
so this list is what worked rather than the smallest that does. Change `21` to the
version `llvm-config --version` prints.

A failed setup leaves `../PS5_Vulkan/.deps/work/radv-clc-build/` behind; remove it
before running the bootstrap again:

```bash
rm -rf ../PS5_Vulkan/.deps/work/radv-clc-build ../PS5_Vulkan/.deps/work/radv-clc-build.setup.log
ps5/tools/bootstrap.sh
```

Meson's notes that `libdrm` and `libudev` were not found are harmless: that build is
only the PC tool.

## Optional

```bash
sudo apt install gh    # to fork and push from the command line
```
