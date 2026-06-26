local function trim(value)
  return (value:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function utf8_sub(value, first_char, last_char)
  if value == "" then
    return ""
  end

  local start_byte = utf8.offset(value, first_char)
  if start_byte == nil then
    return ""
  end

  local end_byte = nil
  if last_char ~= nil then
    end_byte = utf8.offset(value, last_char + 1)
  end

  if end_byte == nil then
    return value:sub(start_byte)
  end

  return value:sub(start_byte, end_byte - 1)
end

function transform(text, params)
  local left_width = tonumber(params.left_width or "30") or 30
  local gap_width = tonumber(params.gap_width or "4") or 4
  local right_width = tonumber(params.right_width or "30") or 30

  local result = {}

  for raw_line in (text .. "\n"):gmatch("(.-)\n") do
    local line = raw_line:gsub("\r$", "")

    local left = trim(utf8_sub(line, 1, left_width))
    local right_start = left_width + gap_width + 1
    local right = trim(utf8_sub(line, right_start, right_start + right_width - 1))

    if left ~= "" then
      table.insert(result, left)
    end

    if right ~= "" then
      table.insert(result, right)
    end
  end

  return table.concat(result, "\n")
end
