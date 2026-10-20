-- xmake.lua
-- Tretro: play any libretro core in a terminal (Windows / MinGW only)
-- requires xmake >= 2.7.0
--
--   xmake f --plat=windows --toolchain=mingw --sdk="C:/path/to/mingw64"
--   xmake f --plat=windows --arch=x86 --toolchain=mingw --sdk="C:/path/to/mingw32"
--   xmake
--
-- LTO is opt-in (xmake f --lto=y). GCC 15.x / MinGW has a confirmed ICE in
-- binds_to_current_def_p (symtab.cc), so it stays off by default.

set_project("Tretro")
set_version("1.0.0")

add_rules("mode.debug", "mode.release", "mode.releasedbg")
set_languages("cxx17")
set_defaultmode("release")

-- ── Options ────────────────────────────────────────────────────
option("lto")
    set_default(false)
    set_showmenu(true)
    set_description("Enable Link-Time Optimization (-flto)")
option_end()

option("analyze")
    set_default(false)
    set_showmenu(true)
    set_description("Print removed sections and write a linker map file")
option_end()

option("mem_analyze")
    set_default(false)
    set_showmenu(true)
    set_description("Enable background memory leak analysis and symbol resolution")
option_end()

local default_engine = "RTerminal/src"

option("engine_dir")
    set_default(default_engine)
    set_showmenu(true)
    set_description("Root directory of the custom engine (Git submodule or custom path)")
option_end()

local ENGINE = get_config("engine_dir") or default_engine

local ENGINE_SUBDIRS = {
    "core",    "math",    "platform",
    "audio",   "render",  "animation",
    "asset",   "physics", "geometry",
    "data",    "script"
}

-- ── Main target ────────────────────────────────────────────────
target("Tretro")
    set_kind("binary")

    add_cxflags(
        "-Wall", "-Wextra",
        "-Wno-unused-parameter",
        "-Wno-missing-field-initializers",
        "-Wno-implicit-fallthrough",
        {force = true}
    )

    -- ── Release ───────────────────────────────────────────────
    if is_mode("release") then
        local rel_cxflags = {
            "-Oz",
            "-mcmodel=small",
            "-fno-pie",
            "-fno-pic",
            "-fno-ident",
            "-ffast-math",
            "-ffunction-sections",
            "-fdata-sections",
            "-fno-rtti",
            "-fno-builtin",
            "-fno-exceptions",
            "-fno-stack-protector",
            "-fno-ipa-cp",
            "-fno-partial-inlining",
            "-fmerge-all-constants",
            "-fvisibility=hidden",
            "-fvisibility-inlines-hidden",
            "-fno-threadsafe-statics",
            "-mno-stack-arg-probe",
            "-fno-use-cxa-atexit",
            "-fno-plt",
            "-fipa-pta"
        }

        if has_config("lto") then
            table.insert(rel_cxflags, "-flto")
            table.insert(rel_cxflags, "-flto-partition=none")
        end

        if has_config("mem_analyze") then
            table.insert(rel_cxflags, "-fno-omit-frame-pointer")
            table.insert(rel_cxflags, "-fasynchronous-unwind-tables")
            table.insert(rel_cxflags, "-funwind-tables")
            set_symbols("debug")
            add_defines("MEM_ANALYZE")
        else
            table.insert(rel_cxflags, "-fomit-frame-pointer")
            table.insert(rel_cxflags, "-fno-asynchronous-unwind-tables")
            table.insert(rel_cxflags, "-fno-unwind-tables")
        end

        add_cxflags(rel_cxflags, {force = true})
        add_defines("NDEBUG")

        if is_arch("x86_64") then
            add_cxflags("-msse4.1", "-msse2", "-mfpmath=sse", {force = true})
        elseif is_arch("x86", "i386", "i686") then
            add_cxflags("-msse2", "-mfpmath=sse", {force = true})
        end

    -- ── Debug ─────────────────────────────────────────────────
    elseif is_mode("debug") then
        add_defines("DEBUG", "_DEBUG")
        set_symbols("debug")
        set_optimize("none")
        add_cxflags("-fno-omit-frame-pointer", "-fasynchronous-unwind-tables", {force = true})
    end

    -- ── Linker map (analyze & release) ────────────────────────
    if has_config("analyze") or is_mode("release") then
        add_ldflags("-Wl,-Map,build/Tretro.map", {force = true})
    end

    before_build(function(target)
        if not os.isdir(ENGINE) then
            raise("\n[xmake error] Engine directory not found at: %s\n" ..
                  "  -> If you cloned this repo from GitHub, run:\n" ..
                  "       git submodule update --init --recursive\n" ..
                  "  -> Or configure the path to your engine:\n" ..
                  "       xmake f --engine_dir=/path/to/engine\n", ENGINE)
        end
        os.mkdir("build")
    end)

    -- ── Sources ───────────────────────────────────────────────
    add_files(
        "src/main.cpp",
        "src/frontend.cpp",
        "src/video.cpp",
        "src/audio.cpp",
        "src/input.cpp",
        "src/interface.cpp",
        "src/autodetect.cpp",
        "src/path_utils.cpp",
        "src/win32_dispath.cpp",
        "src/config_manager.cpp",
        "src/gl_render.cpp",
        "src/core_download_manager.cpp"
    )

    add_files(
        ENGINE .. "/render/display.cpp",
        ENGINE .. "/render/render_utils.cpp",
        ENGINE .. "/audio/audio.cpp",
        ENGINE .. "/platform/input.cpp",
        ENGINE .. "/platform/gamepad.cpp",
        ENGINE .. "/platform/file_system.cpp",
        ENGINE .. "/platform/console_utils.cpp",
        ENGINE .. "/platform/threading.cpp",
        ENGINE .. "/render/ui.cpp",
        ENGINE .. "/core/arena.cpp",
        ENGINE .. "/core/dyn_string.cpp",
        ENGINE .. "/core/memory.cpp",
        ENGINE .. "/core/entry.cpp",
        ENGINE .. "/core/stdio.cpp",
        ENGINE .. "/core/init.cpp",
        ENGINE .. "/core/crt_sys.cpp",
        ENGINE .. "/core/string_id.cpp",
        ENGINE .. "/core/string_table.cpp"
    )

    add_includedirs("src")
    for _, sub in ipairs(ENGINE_SUBDIRS) do
        add_includedirs(ENGINE .. "/" .. sub)
    end

    if os.isfile("res/icon.rc") and os.isfile("res/icon.ico") then
        add_files("res/icon.rc")
    end

    -- ── Windows / MinGW ───────────────────────────────────────
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")

    if is_arch("x86", "i386", "i686") then
        add_cxflags("-mstackrealign", {force = true})
    end

    if is_mode("release") then
        add_syslinks("kernel32")

        local win_ldflags = {
            "-nodefaultlibs",
            "-nostartfiles",
            "-Wl,-Bdynamic",
            "-Wl,--gc-sections",
            "-Wl,-O1",
            "-Wl,--disable-reloc-section",
            "-Wl,--build-id=none",
            "-Wl,--no-insert-timestamp",
            "-Wl,--disable-auto-import",
            "-Wl,--dynamicbase",
            "-Wl,--high-entropy-va",
            "-Wl,--nxcompat",
            "-Wl,--allow-multiple-definition"
        }

        if not has_config("mem_analyze") then
            table.insert(win_ldflags, "-Wl,--exclude-all-symbols")
            table.insert(win_ldflags, "-s")
        end

        add_ldflags(win_ldflags, {force = true})
    end

    -- ── Post-build: pack + analyse ────────────────────────────
    after_build(function(target)
        if not is_mode("release") then return end
        import("lib.detect.find_tool")
        local python = find_tool("python3") or find_tool("python")

        local function find_tool_script(script_name)
            for _, p in ipairs({
                path.join(os.projectdir(), "tools", script_name),
                path.join(ENGINE, "tools", script_name),
                path.join(ENGINE, "..", "tools", script_name)
            }) do
                if os.isfile(p) then return p end
            end
            return nil
        end

        -- Packing breaks runtime stack walking, so skip it in mem_analyze mode.
        if not has_config("mem_analyze") then
            local packer = find_tool_script("packer.py")
            if python and packer then
                print("Packing executable with custom packer (" .. packer .. ")...")
                os.execv(python.program, {packer, target:targetfile()})
            else
                print("Warning: Packer not found or python missing.")
            end
        end

        local analyzer = find_tool_script("map_analyzer.py")
        local mapfile  = path.join("build", "Tretro.map")
        if python and analyzer and os.isfile(mapfile) then
            print("Analyzing binary size breakdown (" .. analyzer .. ")...")
            os.execv(python.program, {analyzer, mapfile})
        end
    end)

-- ── Task: create directories ───────────────────────────────────
task("setup")
    set_category("plugin")
    set_menu({
        usage       = "xmake setup",
        description = "Create system/, saves/, cores/, roms/, res/, tools/, build/"
    })
    on_run(function()
        for _, d in ipairs({"system", "saves", "cores", "roms", "res", "tools", "build"}) do
            os.mkdir(d)
        end
        local rc_file = "res/icon.rc"
        if not os.isfile(rc_file) then
            io.writefile(rc_file, 'IDI_ICON1 ICON DISCARDABLE "icon.ico"\n')
        end
    end)