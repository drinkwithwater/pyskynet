local modify = require "pyskynet.modify"
local skynet = require "skynet"
local thlua = require "thlua"

local scriptName, scriptIndex = ...
local scriptCode = modify.getscript(scriptIndex)

if not scriptCode then
	error("script node found, key="..tostring(scriptIndex)..",name="..tostring(scriptName))
end

SERVICE_SCRIPT = scriptCode

local main, err = thlua.load(scriptCode, scriptName or string.format("script@%s", scriptIndex))

if not main then
	error(err)
else
	main(select(3, ...))
end
