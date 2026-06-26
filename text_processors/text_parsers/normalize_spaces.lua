local function trim(value)
  return (value:gsub("^%s+", ""):gsub("%s+$", ""))
end

function transform(text, params)
  text = text:gsub("\194\160", " ") -- NO-BREAK SPACE
  text = text:gsub("\226\128\175", " ") -- NARROW NO-BREAK SPACE
  text = text:gsub("\t", " ")

  local result = {}
  for line in (text .. "\n"):gmatch("(.-)\n") do
    line = line:gsub(" +", " ")
    table.insert(result, trim(line))
  end

  return table.concat(result, "\n")
end
