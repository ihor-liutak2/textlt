-- Lua lexer sample
local M = {}

function M.normalize(value)
    if value == nil or value == false then
        return "empty"
    end

    local text = [[
multi-line style text in a long bracket string
]]
    print(string.format("%s:%d", text, 42))
    return value + 0x10
end

return M
