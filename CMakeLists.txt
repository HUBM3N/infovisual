cmake_minimum_required(VERSION 3.16)
project(tf2overlay)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(IMGUI_DIR  ${CMAKE_SOURCE_DIR}/src/imgui)
set(MH_DIR     ${CMAKE_SOURCE_DIR}/src/minhook)
set(GIFLIB_DIR ${CMAKE_SOURCE_DIR}/src/giflib-6.1.3)
set(FREETYPE_STATIC_DIR "C:/vcpkg/installed/x64-windows-static")
# Edge WebView2 SDK (NuGet Microsoft.Web.WebView2, see third_party/webview2/VERSION.txt).
# Static loader: nothing extra to ship next to the DLL.
set(WEBVIEW2_DIR ${CMAKE_SOURCE_DIR}/third_party/webview2)

# The WebView2 engine builds on its own: the DLL's include path puts the June 2010 DirectX SDK
# ahead of the Windows SDK, and this file needs the current d3d11.h / WinRT capture interop.
add_library(wv2host STATIC src/features/webview2host.cpp)
target_include_directories(wv2host PUBLIC ${WEBVIEW2_DIR}/include)
target_compile_definitions(wv2host PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(wv2host PUBLIC ${WEBVIEW2_DIR}/lib/x64/WebView2LoaderStatic.lib d3d11 windowsapp ole32 user32)
set_target_properties(wv2host PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(tf2overlay SHARED
    src/dllmain.cpp
    src/features/config.cpp
    src/features/drawutils.cpp
    src/features/watermark.cpp
    src/features/crosshair.cpp
    src/features/menu.cpp
    src/features/visuals.cpp
    src/features/sheen.cpp
    src/features/netvars.cpp
    src/features/crashhandler.cpp
    src/features/misc.cpp
    src/features/names.cpp
    src/features/skins.cpp
    src/features/render.cpp
    src/features/bloom.cpp
    src/features/screenfx.cpp
    src/features/ssao.cpp
    src/features/dof.cpp
    src/features/frameblend.cpp
    src/features/killfeed.cpp
    src/features/weaponhud.cpp
    src/features/thread.cpp
    src/features/playerlist.cpp
    src/features/vmglow.cpp
    src/features/flipworld.cpp
    src/features/scoreboard_hook.cpp
    src/features/netgraph.cpp
    src/features/jobs.cpp
    src/features/websurface.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_impl_dx9.cpp
    ${IMGUI_DIR}/imgui_impl_win32.cpp
    ${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp
    ${MH_DIR}/hde/hde64.c
    ${MH_DIR}/buffer.c
    ${MH_DIR}/hook.c
    ${MH_DIR}/trampoline.c
    ${GIFLIB_DIR}/dgif_lib.c
    ${GIFLIB_DIR}/gif_err.c
    ${GIFLIB_DIR}/gifalloc.c
    ${GIFLIB_DIR}/openbsd-reallocarray.c
)

# Include directories
target_include_directories(tf2overlay PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/include
    ${IMGUI_DIR}
    ${MH_DIR}
    ${GIFLIB_DIR}
    "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Include"
    "${FREETYPE_STATIC_DIR}/include"
)

target_link_directories(tf2overlay PRIVATE
    "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)/Lib/x64"
    "${FREETYPE_STATIC_DIR}/lib"
    "$<$<CONFIG:Debug>:${FREETYPE_STATIC_DIR}/debug/lib>"
)

target_link_libraries(tf2overlay PRIVATE
    d3d9
    d3dx9
    windowsapp      # WinRT / SMTC media player API
    ole32           # WASAPI loopback (CoCreateInstance, CoInitializeEx)
    winhttp         # HTTPS lyrics fetch (LRCLib)
    user32
    gdi32
    psapi
    wv2host         # Edge WebView2 engine (src/features/webview2host.cpp)
    optimized freetype debug freetyped
    optimized brotlidec debug brotlidec
    optimized brotlicommon debug brotlicommon
    optimized bz2 debug bz2
    optimized libpng16 debug libpng16d
    optimized zlib debug zlibd
)

# Compile definitions
target_compile_definitions(tf2overlay PRIVATE
    _WIN32_WINNT=0x0A00
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    IMGUI_ENABLE_FREETYPE
    # Font atlases build on a Jobs worker (render.cpp Font_Build). With debug tools on,
    # ImGui::MemAlloc writes alloc stats into the live context from that thread.
    IMGUI_DISABLE_DEBUG_TOOLS
)

# Runtime library (static MT/MTd)
set_target_properties(tf2overlay PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    OUTPUT_NAME "infovisual"
    PREFIX ""
    SUFFIX ".dll"
)

# PDB for Release so crash.log RVAs resolve to functions. Explicit /OPT keeps
# code identical to a no-PDB build (/DEBUG alone flips REF/ICF off).
target_compile_options(tf2overlay PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(tf2overlay PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF /INCREMENTAL:NO>)

# Optional: Set x64 platform explicitly
set(CMAKE_GENERATOR_PLATFORM x64 CACHE STRING "" FORCE)

# Gate test for the config value parser (header-only, no game needed).
add_executable(cfgparse_test tests/cfgparse_test.cpp)

# Gate test for Flip World's engine-free math + cl_flipviewmodels hold (header-only).
add_executable(flipworld_test tests/flipworld_test.cpp)

# Gate test for the color correction tone curve (spline, editing, config, LUT bake; header-only).
add_executable(cccurve_test tests/cccurve_test.cpp)

# Gate test for the overlay animation clock (host_framerate / host_timescale, header-only).
add_executable(animclock_test tests/animclock_test.cpp)

# Gate test for the GIF frame compositor (header + giflib's decoder, no device needed).
add_executable(gif_test tests/gif_test.cpp
    ${GIFLIB_DIR}/dgif_lib.c
    ${GIFLIB_DIR}/gif_err.c
    ${GIFLIB_DIR}/gifalloc.c
    ${GIFLIB_DIR}/openbsd-reallocarray.c)
target_include_directories(gif_test PRIVATE ${GIFLIB_DIR})

# Gate test for the CS:GO viewmodel bob/lower/dip port (src/features/vm_csgo.h, shared w/ SimpleVisual).
# Build + run: cmake --build build --config Release --target vmcsgo_test && build\Release\vmcsgo_test.exe
add_executable(vmcsgo_test tests/vmcsgo_test.cpp)

# Gate test for the CS:GO sniper scope port (src/features/scope_csgo.h, shared w/ SimpleVisual).
# Build + run: cmake --build build --config Release --target scopecsgo_test && build\Release\scopecsgo_test.exe
add_executable(scopecsgo_test tests/scopecsgo_test.cpp)

# Gate test for minesweeper rules (src/features/minesweeper.h, header-only).
# Build + run: cmake --build build --config Release --target minesweeper_test && build\Release\minesweeper_test.exe
add_executable(minesweeper_test tests/minesweeper_test.cpp)

# Gate test for the web surface's quad basis / hit test / wrap grid (src/features/websurf_math.h).
# Build + run: cmake --build build --config Release --target websurf_test && build\Release\websurf_test.exe
add_executable(websurf_test tests/websurf_test.cpp)

# Integration harness for the WebView2 engine (src/features/webview2host.cpp), outside TF2.
# Needs the WebView2 runtime + network; not a gate test. See the file header for modes.
# Build + run: cmake --build build --config Release --target wv2_harness
#              build\Release\wv2_harness.exe play https://www.twitch.tv/ 20 twitch.bmp
add_executable(wv2_harness tests/wv2_harness.cpp)
target_include_directories(wv2_harness PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(wv2_harness PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(wv2_harness PRIVATE wv2host)
set_target_properties(wv2_harness PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Gate test for bloom's numbers (src/features/fx_bloom_*.hlsl and the fxc output beside them):
# gaussian weights, the knee, the radius -> pass count map, and whether the compiled headers
# still match the shaders (nothing at build time compiles hlsl, so a stale .h ships silently).
# Same file as dih's, because the shaders are the same shaders.
# Build + run: cmake --build build --config Release --target bloom_test && build\Release\bloom_test.exe
add_executable(bloom_test tests/bloom_test.cpp)
target_compile_definitions(bloom_test PRIVATE BLOOM_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/features")

# Gate test for frame blending's maths (src/features/fbmath.h): the reprojection identities a
# wall and a running player have to satisfy, the bone conversion, and the shutter raised as a
# screw motion. Also catches an fx_fb_*.hlsl edited without rerunning fxc, and a shader rebuilt
# with different constant registers than frameblend.cpp uploads to.
# Build + run: cmake --build build --config Release --target fbmath_test && build\Release\fbmath_test.exe
add_executable(fbmath_test tests/fbmath_test.cpp)
target_compile_definitions(fbmath_test PRIVATE FB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/features")

# Gate test for the killstreak carry-over across demos (src/features/demostreak.h): demo load
# events keep the streak, a rewind does not count the replayed kills twice, a death still ends it,
# live play unchanged. Same file as dih's.
# Build + run: cmake --build build --config Release --target demostreak_test && build\Release\demostreak_test.exe
add_executable(demostreak_test tests/demostreak_test.cpp)
