cmake_minimum_required(VERSION 3.24)
project(rockmorse C)

# Box2D 3 needs C17.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# raylib 6.0 and Box2D v3.1.1 are downloaded on the first configure and
# checked against their SHA-256 hashes. Both build as static libraries, so
# they end up inside the game binary or WebAssembly file.
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
if(EMSCRIPTEN)
    set(PLATFORM Web CACHE STRING "" FORCE)
endif()
FetchContent_Declare(raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz
    URL_HASH SHA256=2b3ee1e2120c7a0796b33062c7e9a694dd8a8caa56a96319ac8c8ecf54a90d0b
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# The v3.1.1 tag, not main: main is 3.2 work in progress and renames the
# sensor functions. SIMD stays on, Emscripten builds it as wasm SIMD.
set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX2D_AVX2 OFF CACHE BOOL "" FORCE)
FetchContent_Declare(box2d
    URL https://github.com/erincatto/box2d/archive/refs/tags/v3.1.1.tar.gz
    URL_HASH SHA256=fb6ef914b50f4312d7d921a600eabc12318bb3c55a0b8c0b90608fa4488ef2e4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(raylib box2d)

set(GAME_SOURCES
    src/unity.c
    src/physics.c
    src/intro.c
    src/title.c
    src/next_scene_on_space.c
    src/end_game.c
    src/game.c
    src/mars.c
    src/earth.c
    src/rover_screen.c
    src/player.c
    src/camera_focus.c
    src/rock.c
    src/rock_generator.c
    src/effects.c
    src/rock_dragger.c
    src/drag_target.c
    src/rover.c
    src/rocket.c
    src/morse.c
    src/level_loader.c
    src/rock_placeholder.c)

# The engine layer rasterises the Joystix font itself, with the
# stb_truetype copy that ships inside raylib (see unity.c).
function(rockmorse_setup target)
    target_link_libraries(${target} raylib box2d)
    target_include_directories(${target} PRIVATE "${raylib_SOURCE_DIR}/src/external")
    # Unity computed in float32 with no fused multiply-add. Keep it so, for
    # the eased values (SmoothDamp, GetAxis) to match bit for bit.
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -ffp-contract=off -Wall -Wextra)
        target_link_libraries(${target} m)
    endif()
endfunction()

add_executable(rockmorse src/main.c ${GAME_SOURCES})
rockmorse_setup(rockmorse)

if(EMSCRIPTEN)
    # The web page around the game and its touch controls are shared with
    # Kurban Bayrami, which keeps them in port/web/. Each configure downloads
    # them from that repo. By default it follows the main branch, so the
    # latest version arrives on the next configure. For a build that must not
    # change, pin a commit: cmake -DSHARED_WEB_REF=<commit hash> ...
    # GitHub may serve a branch's files up to 5 minutes old after a push.
    # To try changes to the shared files before they are pushed, point
    # SHARED_WEB_DIR at a local kurban-bayrami checkout's port/web folder.
    set(SHARED_WEB_REF "main" CACHE STRING "kurban-bayrami branch or commit to take the shared web files from")
    set(SHARED_WEB_DIR "" CACHE PATH "Local folder to copy the shared web files from, instead of downloading them")
    set(SHARED_WEB_URL "https://raw.githubusercontent.com/eralpkaraduman/kurban-bayrami/${SHARED_WEB_REF}/port/web")
    set(WEB_DIR "${CMAKE_BINARY_DIR}/shared-web")
    foreach(name shell.html.in site.html.in touch-controls.js touch-controls.css)
        if(SHARED_WEB_DIR)
            file(COPY_FILE "${SHARED_WEB_DIR}/${name}" "${WEB_DIR}/${name}")
            continue()
        endif()
        file(DOWNLOAD "${SHARED_WEB_URL}/${name}" "${WEB_DIR}/${name}" STATUS status)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            list(GET status 1 reason)
            message(FATAL_ERROR "Could not download ${name} from ${SHARED_WEB_URL}: ${reason}")
        endif()
    endforeach()
    if(SHARED_WEB_DIR)
        message(STATUS "Shared web files from ${SHARED_WEB_DIR}")
    else()
        message(STATUS "Shared web files from kurban-bayrami at ${SHARED_WEB_REF}")
    endif()

    # Fill in the page for this game. Key codes are raylib's: arrows 262-265,
    # Space 32, M 77, H 72. Pull sends Space, so it also dismisses messages,
    # skips the story screens and starts the game from the title. A tap on
    # the game is a mouse click, for the title's buttons and the screens
    # that a click skips.
    set(WEB_TITLE "Rockmorse")
    set(WEB_TOUCH_DPAD "{ left: 263, right: 262, up: 265, down: 264 }")
    set(WEB_TOUCH_BUTTONS "[{ label: 'Space', caption: 'Pull', key: 32 }, { label: 'M', caption: 'Map', key: 77 }, { label: 'H', caption: 'Help', key: 72 }]")
    set(WEB_TOUCH_TAP_CLICKS ON)
    configure_file("${WEB_DIR}/shell.html.in" "${CMAKE_BINARY_DIR}/shell.html" @ONLY)

    # The GitHub Pages cover page, from the shared template. The page text
    # comes from web/site-content.html.
    set(SITE_TAGLINE "Stranded on Mars, you answer Earth by dragging rocks into Morse code. A Ludum Dare 30 game by Super Damage, rebuilt in C.")
    set(SITE_URL "https://eralpkaraduman.github.io/rockmorse/")
    set(SITE_THEME "--bg: #1a120c; --surface: #261a12; --text: #f3e9df; --muted: #b8a595; --accent: #ff8a3d; --accent-strong: #ffd2a8; --border: #3d2b1e;")
    set(SITE_GAME_RATIO "3 / 2")
    # On touch screens the game frame holds the game at full width plus the
    # control bar. The D-pad has all four arms, as in Kurban Bayrami, so its
    # three rows set the bar's height. The game is 3:2, so the frame is
    # taller than wide and the D-pad sizes its arms by the screen width.
    set(SITE_TOUCH_FRAME_HEIGHT "calc(100vw * 2 / 3 + 3 * clamp(44px, 13vw, 60px) + 42px)")
    set(SITE_HINT_KEYBOARD "Click <b>Play</b> in the game, or press <kbd>Space</kbd>. You need a keyboard.")
    set(SITE_HINT_TOUCH "Tap <b>Play</b> in the game to start. Play with the buttons under the game.")
    file(READ "${CMAKE_SOURCE_DIR}/web/site-content.html" SITE_CONTENT)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/web/site-content.html")
    configure_file("${WEB_DIR}/site.html.in" "${CMAKE_BINARY_DIR}/site.html" @ONLY)

    target_compile_definitions(rockmorse PRIVATE PLATFORM_WEB)
    # itch.io wants index.html at the root of the uploaded zip.
    set_target_properties(rockmorse PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
    target_link_options(rockmorse PRIVATE
        # The raylib wiki advises a fixed heap over memory growth. The sounds
        # are WAV files decoded into memory, about 40 MB in all with the
        # 115 s ambience loop, so 128 MB leaves room.
        # emscripten_set_main_loop drives frames, so ASYNCIFY is not needed.
        -sUSE_GLFW=3
        -sINITIAL_MEMORY=134217728
        -sENVIRONMENT=web
        --preload-file ${CMAKE_SOURCE_DIR}/assets@assets
        --shell-file ${CMAKE_BINARY_DIR}/shell.html)
    # Relink when the page changes, and put the touch controls beside it.
    set_target_properties(rockmorse PROPERTIES LINK_DEPENDS
        "${CMAKE_BINARY_DIR}/shell.html;${WEB_DIR}/touch-controls.js;${WEB_DIR}/touch-controls.css")
    add_custom_command(TARGET rockmorse POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${WEB_DIR}/touch-controls.js" "${WEB_DIR}/touch-controls.css"
            "${CMAKE_BINARY_DIR}"
        VERBATIM)
    set(WEB_FILES index.html index.js index.wasm index.data touch-controls.js touch-controls.css)
    # cmake --build build-web --target itch  ->  build-web/rockmorse-itch.zip
    add_custom_target(itch
        COMMAND ${CMAKE_COMMAND} -E tar cf rockmorse-itch.zip --format=zip ${WEB_FILES}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS rockmorse)
    # cmake --build build-web --target pages  ->  build-web/pages/, the GitHub
    # Pages site: a cover page at the root and the game itself under play/.
    # The shared page names its heading font fonts/system.ttf, so it gets
    # the game's Joystix font under that name.
    set(PAGES_DIR "${CMAKE_BINARY_DIR}/pages")
    add_custom_target(pages
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${PAGES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PAGES_DIR}/play" "${PAGES_DIR}/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/site.html" "${PAGES_DIR}/index.html"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/rockmorse-cover-630x500.png" "${PAGES_DIR}/cover.png"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/screenshots" "${PAGES_DIR}/screenshots"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/assets/Joystix/joystix monospace.ttf" "${PAGES_DIR}/fonts/system.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy ${WEB_FILES} "${PAGES_DIR}/play"
        COMMAND ${CMAKE_COMMAND} -E touch "${PAGES_DIR}/.nojekyll"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS rockmorse
        VERBATIM)
else()
    # Copy assets next to the binary. main.c changes into that folder.
    add_custom_command(TARGET rockmorse POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:rockmorse>/assets)

    # Logic checks, desktop only: ./build/rockmorse_check from this folder.
    add_executable(rockmorse_check tests/check.c ${GAME_SOURCES})
    rockmorse_setup(rockmorse_check)
endif()
