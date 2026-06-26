local function trim(value)
  return (value:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function is_blank(value)
  return trim(value) == ""
end

local function should_join(previous, current, min_line_length)
  if previous == "" or current == "" then
    return false
  end

  if previous:match("[%!%?%.:%;]$") then
    return false
  end

  if current:match("^%s*[%-%*%u%d]+[%.)]%s+") then
    return false
  end

  return #previous >= min_line_length
end

function transform(text, params)
  local min_line_length = tonumber(params.min_line_length or "20") or 20
  local paragraphs = {}
  local current = ""

  for raw_line in (text .. "\n"):gmatch("(.-)\n") do
    local line = trim(raw_line:gsub("\r$", ""))

    if is_blank(line) then
      if current ~= "" then
        table.insert(paragraphs, current)
        current = ""
      end
    elseif current == "" then
      current = line
    elseif current:match("%-$") then
      current = current:gsub("%-$", "") .. line
    elseif should_join(current, line, min_line_length) then
      current = current .. " " .. line
    else
      table.insert(paragraphs, current)
      current = line
    end
  end

  if current ~= "" then
    table.insert(paragraphs, current)
  end

  return table.concat(paragraphs, "\n\n")
end
