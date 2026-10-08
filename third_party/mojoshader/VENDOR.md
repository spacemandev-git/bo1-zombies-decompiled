# MojoShader (vendored subset)

* Upstream: https://github.com/icculus/mojoshader
* Commit: `ad5dff84830c2863c841f4b1f4e3df78c705b383` ("hlsl: Add BLENDWEIGHT case for pixel shader input"),
  committed 2026-09-03; fetched 2026-10-07.
* License: zlib (see `LICENSE.txt`, unchanged). Copyright (c) 2008-2025 Ryan C. Gordon.
* Used by: the Direct3D 9 -> WebGL2 shim (`src/web/d3d9`), which translates the game's precompiled vs_3_0/ps_3_0
  bytecode to GLSL ES 3.00 with the `glsles3` profile. Build integration: `src/web/d3d9/d3d9shim.cmake`.

## Files taken (unmodified)

`mojoshader.c`, `mojoshader.h`, `mojoshader_internal.h`, `mojoshader_common.c`, `profiles/mojoshader_profile.h`,
`profiles/mojoshader_profile_common.c`, `profiles/mojoshader_profile_glsl.c`, `LICENSE.txt`, `README.md`.

Everything else upstream (the GL/D3D11/SDL GPU glue, effects, the other profiles, SPIR-V, tests, utils, upstream
agent instruction files) is not vendored. Build with the GLSL profiles only:
`SUPPORT_PROFILE_{D3D,BYTECODE,HLSL,GLSL120,ARB1,ARB1_NV,METAL,SPIRV,GLSPIRV}=0`, `MOJOSHADER_NO_VERSION_INCLUDE`.

## Local patches

Marked `BO1-WEB` in the source. Keep this list in sync.

1. `profiles/mojoshader_profile_glsl.c`, `make_GLSL_destarg_assign`: predicated instructions (`(p0.x) mov r0, c0`)
   were rejected ("predicated destinations unsupported"). Predicated writes to float registers are now emitted as a
   component-wise select, `dst.mask = mix(dst.mask, value, p0.<swizzle per written component>)` (`not(...)` for
   `!p0`), or `if (p0.c) dst = value;` for one component.
2. `mojoshader_internal.h`, `scalar_register`: the pixel-shader predicate register was treated as a scalar although
   the GLSL profile declares it `bvec4` (D3D9's p0 has four components in both shader types; Wine and vkd3d agree), so
   a full-mask `setp` assigned a bool to a bvec4. Now never scalar.

Not patched here but adapted in the shim (`src/web/d3d9/d3d9_shader_translate.cpp`): output-text fixups (precision,
wrapper `main()` for the D3D->GL clip-space fixup and alpha test, the D3D9 VPOS half pixel, FOG/PSIZE scalar outputs
declared as vec4 varyings), the predicate token order (D3D bytecode stores the predicate token right after the
destination, as Wine/vkd3d parse it; MojoShader reads it after the sources - the shim reorders the tokens before
parsing), and a synthetic CTAB for relative addressing in shaders without one.

## Updating

Clone upstream at the new commit into a scratch directory, copy the files above over these, update the commit/date
here, rebuild and run the shim's unit tests (`src/web/d3d9/test/run_unit_tests.sh`), which check the exact output
lines the shim post-processes.
