add_rules("mode.debug")
add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

set_project("coroutine-lib")
set_languages("c++20")

add_includedirs("src")
add_files("src/*.cpp")

target("test_coroutine")
    set_kind("binary")
    add_files("test/test_coroutine.cpp")

target("test_scheduler")
    set_kind("binary")
    add_files("test/test_scheduler.cpp")

target("test_iomanager")
    set_kind("binary")
    add_files("test/test_iomanager.cpp")

target("test_timer")
    set_kind("binary")
    add_files("test/test_timer.cpp")

target("test_hook_sleep")
    set_kind("binary")
    add_files("test/test_hook_sleep.cpp")
