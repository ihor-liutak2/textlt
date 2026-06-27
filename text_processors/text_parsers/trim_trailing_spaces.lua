function transform(text, params)
  local trailing_newline = text:sub(-1) == "\n"
  local lines = {}

  for line in (text .. "\n"):gmatch("(.-)\n") do
    lines[#lines + 1] = line:gsub("[ \t]+$", "")
  end

  if trailing_newline and lines[#lines] == "" then
    table.remove(lines)
  end

  local result = table.concat(lines, "\n")
  if trailing_newline then
    result = result .. "\n"
  end
  return result
end
