set_project("SPHinXsys")

set_xmakever("2.7.3")

option("sphinxsys_2d")
    set_default(true)
    set_showmenu(true)
    set_description("Build sphinxsys_2d library.")
option_end()

option("sphinxsys_3d")
    set_default(true)
    set_showmenu(true)
    set_description("Build sphinxsys_3d library.")
option_end()

add_requires("boost", {configs = {program_options = true}})
add_requires("tbb")
add_requires("simbody >=3.6.0")
add_requires("eigen >=3.4")

set_languages("c++17")

add_defines("_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING")

if is_host("windows") then
    add_defines("_USE_MATH_DEFINES", "NOMINMAX")
    add_cxxflags("/MP", "/permissive-", {tools = "cl"})
end

if is_plat("windows") and is_kind("shared") then
    add_rules("utils.symbols.export_all", {export_classes = true})
end

add_packages("boost", "tbb", "simbody", "eigen")

function _find_headers(target)
    local dirs = {}
    local headers = {}
    local targetdir = target:name():find("2d") and "for_2D_build" or "for_3D_build"
    table.join2(headers, os.files("SPHINXsys/src/shared/**.h"))
    table.join2(headers, os.files("SPHINXsys/src/shared/**.hpp"))
    table.join2(headers, os.files("SPHINXsys/src/" .. targetdir .. "/**.hpp"))
    table.join2(headers, os.files("SPHINXsys/src/" .. targetdir .. "/**.h"))
    for _, value in ipairs(headers) do
        table.insert(dirs, path.directory(value))
    end
    target:add("includedirs", table.unique(dirs), {public = true})
    -- target:add("headerfiles", headers)
end

if has_config("sphinxsys_2d") then
    target("sphinxsys_2d")
        set_kind("$(kind)")
        add_files("SPHINXsys/src/for_2D_build/**.cpp")
        add_files("SPHINXsys/src/shared/**.cpp")
        on_load(_find_headers)
    target_end()
end

if has_config("sphinxsys_3d") then
    target("sphinxsys_3d")
        set_kind("$(kind)")
        add_files("SPHINXsys/src/for_3D_build/**.cpp")
        add_files("SPHINXsys/src/shared/**.cpp")
        on_load(_find_headers)
    target_end()
end

target("structural_simulation_module")
    set_kind("$(kind)")
    add_files("modules/structural_simulation/structural_simulation_class.cpp")
    add_deps("sphinxsys_3d")
