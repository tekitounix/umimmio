if os.projectdir() == os.scriptdir() then
    set_languages("c++23")
end

includes("library.lua")
includes("tests", "examples")
