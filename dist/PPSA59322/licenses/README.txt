ccleste - legal use
===================

PIRACY IS NOT CONDONED. This title contains no games, no BIOS files, no
console firmware and no decryption keys, and none will ever be provided or
linked to. Use only games you own and files from hardware you own.
Downloading or sharing games, BIOS files, firmware or keys you do not own is
piracy: do not do it, and do not ask for help with it.

Licences: the title's own code and Sascha Willems' Vulkan examples are MIT.
It links the PS5 platform layer and the UI kit, which are GPL-3.0-or-later, so the
title as distributed is under GPL-3.0-or-later as a whole, and its complete
source is offered with it. Every part, its licence and its source are listed
in licenses/README.txt; the assets and theirs in assets/NOTICES.txt.

Not affiliated with or endorsed by Sony Interactive Entertainment.
"PlayStation" and "PS5" are trademarks of Sony Interactive Entertainment Inc.
Vulkan is a registered trademark of the Khronos Group Inc.; the RADV port is
not a conformant product (its conformanceVersion is 0.0.0.0).


The parts of this title
=======================

ccleste: its program, the foundation (Sascha Willems' Vulkan examples and base class, the PS5 layer) and the libraries in its tree (Dear ImGui, libktx, tinygltf with stb_image and nlohmann json, volk, the Vulkan headers, glm)
  licence:   MIT (the title, the examples, Dear ImGui, tinygltf, volk, glm); Apache-2.0 (libktx); Apache-2.0 OR MIT (the Vulkan headers)
  copyright: Copyright (c) 2016-2026 Sascha Willems; Copyright (C) 2026 Mihawk; and each library's authors
  in:        eboot.bin, shaders/
  texts:     licenses/title/
  source:    https://github.com/darkxex/PS5_Celeste at 97360dd929b380be7d96ccd9a3dad32833cf2980

the UI kit: BlackBearReloaded's ps5-homebrew-ui with my Vulkan backend (PS5_VKHomebrewUI), and the UI module that draws with it (ps5/ui/)
  licence:   GPL-3.0-or-later
  copyright: Copyright (C) 2026 BlackBearReloaded; Copyright (C) 2026 Mihawk
  in:        eboot.bin, assets/hui/
  texts:     licenses/ui-kit/
  source:    https://github.com/mihawk-99/PS5_VKHomebrewUI at 4451c261ac8631c698291ee765a37456f25bc22c

the PS5 platform layer (libps5platform.a) and the SDK headers, from my payload SDK fork, with musl's regex and dlmalloc
  licence:   GPL-3.0-or-later; musl's regex MIT; dlmalloc public domain
  copyright: Copyright (C) John Törnblom and the ps5-payload-dev contributors; Copyright (C) 2026 Mihawk
  in:        eboot.bin
  texts:     licenses/platform/
  source:    https://github.com/mihawk-99/PS5_PayloadSDK at adc8dd795ada7bf696cd98748aae463a3970ffbb

PS5_Vulkan: the RADV link recipe, the C runtime start-up, the AGC import stubs and sce_module/libc.prx
  licence:   GPL-3.0-or-later
  copyright: Copyright (C) 2026 Mihawk
  in:        eboot.bin, sce_module/libc.prx
  texts:     licenses/ps5-vulkan/
  source:    https://github.com/mihawk-99/PS5_Vulkan at f3cbf875f89978deae9f855b9be2610cec2818ae

RADV, Mesa's Vulkan driver (with ACO, NIR and Mesa's Vulkan runtime), from my PS5_Mesa fork
  licence:   MIT, with other licences stated per file (licenses/radv/Mesa-license.rst)
  copyright: Copyright the Mesa contributors (per-file notices in the source)
  in:        eboot.bin
  texts:     licenses/radv/
  source:    https://github.com/mihawk-99/PS5_Mesa at dc82d010021e4bae5a12cd143ff44db4d7307e47

LLVM libc++, libc++abi, libunwind and compiler-rt builtins (linked into eboot.bin)
  licence:   Apache-2.0 WITH LLVM-exception
  copyright: Copyright (c) 2003-2026 University of Illinois at Urbana-Champaign and the LLVM project contributors
  in:        eboot.bin
  texts:     licenses/llvm-runtime/
  source:    ps5-payload-dev SDK v0.42 release archives: https://github.com/ps5-payload-dev/sdk/releases/tag/v0.42

The assets: assets/NOTICES.txt, with their licence texts in assets/LICENSES/.
