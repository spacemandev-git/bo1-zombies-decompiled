# d3d9shim.cmake - Direct3D 9 -> WebGL2 shim for the BO1 web build.
#
#   include(${CMAKE_SOURCE_DIR}/src/web/d3d9/d3d9shim.cmake)
#   target_link_libraries(<engine> PRIVATE bo1_d3d9shim)
#
# Defines the static library target bo1_d3d9shim (shim + vendored MojoShader, GLSL profiles only). Its include
# directory (src/web/d3d9/include: d3d9.h, d3dx9.h, dxerr.h, ddraw.h, ...) is PUBLIC. See src/web/d3d9/README.md.

get_filename_component(D3D9SHIM_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(D3D9SHIM_ROOT "${D3D9SHIM_DIR}/../../.." ABSOLUTE)
set(D3D9SHIM_MOJO_DIR "${D3D9SHIM_ROOT}/third_party/mojoshader")

file(GLOB D3D9SHIM_SOURCES CONFIGURE_DEPENDS "${D3D9SHIM_DIR}/d3d9_*.cpp" "${D3D9SHIM_DIR}/d3dx9_*.cpp")

set(D3D9SHIM_MOJO_SOURCES
    "${D3D9SHIM_MOJO_DIR}/mojoshader.c"
    "${D3D9SHIM_MOJO_DIR}/mojoshader_common.c"
    "${D3D9SHIM_MOJO_DIR}/profiles/mojoshader_profile_common.c"
    "${D3D9SHIM_MOJO_DIR}/profiles/mojoshader_profile_glsl.c")

set(D3D9SHIM_MOJO_DEFINES
    SUPPORT_PROFILE_D3D=0 SUPPORT_PROFILE_BYTECODE=0 SUPPORT_PROFILE_HLSL=0 SUPPORT_PROFILE_GLSL120=0
    SUPPORT_PROFILE_ARB1=0 SUPPORT_PROFILE_ARB1_NV=0 SUPPORT_PROFILE_METAL=0 SUPPORT_PROFILE_SPIRV=0
    SUPPORT_PROFILE_GLSPIRV=0 SUPPORT_PROFILE_GLSL=1 SUPPORT_PROFILE_GLSLES=1 SUPPORT_PROFILE_GLSLES3=1
    MOJOSHADER_NO_VERSION_INCLUDE)

add_library(bo1_d3d9shim STATIC ${D3D9SHIM_SOURCES} ${D3D9SHIM_MOJO_SOURCES})
target_include_directories(bo1_d3d9shim PUBLIC "${D3D9SHIM_DIR}/include")
target_include_directories(bo1_d3d9shim PRIVATE "${D3D9SHIM_DIR}" "${D3D9SHIM_MOJO_DIR}")
target_compile_definitions(bo1_d3d9shim PRIVATE ${D3D9SHIM_MOJO_DEFINES})
target_compile_features(bo1_d3d9shim PRIVATE cxx_std_17)
set_source_files_properties(${D3D9SHIM_MOJO_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
if(EMSCRIPTEN)
    target_link_options(bo1_d3d9shim INTERFACE "-sMAX_WEBGL_VERSION=2" "-sMIN_WEBGL_VERSION=2")
endif()
