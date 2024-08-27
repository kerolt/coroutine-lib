add_rules("mode.debug")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

set_project("coroutine-lib")
set_languages("c++20")

add_includedirs("src")

target("test_coroutine")
    set_kind("binary")
    add_files("test/test_coroutine.cpp", "src/coroutine.cpp")
