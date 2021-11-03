workspace "simpledwrite"
    configurations { "Debug", "Release" }
    platforms { "x64" }
    basedir "demo"

project "simpledwrite"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    characterset "ASCII"
    toolset "v143"
    basedir "demo"

    configurations { "Debug", "Release" }
    platforms { "x64" }

    files { "**.cc", "**.h" }
    targetdir "bin/%{cfg.buildcfg}"
    includedirs { ".", }

    filter { "platforms:x64" }
        system "Windows"
        architecture "x86_64"
        buildoptions { "/execution-charset:utf-8", "/source-charset:utf-8" }

    filter "configurations:Debug*"
        defines { "_DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "On"
