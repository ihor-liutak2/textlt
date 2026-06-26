function transform(text, params)
  local replacements = {
    ["\239\187\191"] = "", -- UTF-8 BOM
    ["\226\128\139"] = "", -- ZERO WIDTH SPACE
    ["\226\128\140"] = "", -- ZERO WIDTH NON-JOINER
    ["\226\128\141"] = "", -- ZERO WIDTH JOINER
    ["\226\128\142"] = "", -- LEFT-TO-RIGHT MARK
    ["\226\128\143"] = "", -- RIGHT-TO-LEFT MARK
    ["\226\129\160"] = "", -- WORD JOINER
    ["\194\173"] = "",     -- SOFT HYPHEN
  }

  for pattern, replacement in pairs(replacements) do
    text = text:gsub(pattern, replacement)
  end

  text = text:gsub("[%z\1-\8\11\12\14-\31\127]", "")
  return text
end
