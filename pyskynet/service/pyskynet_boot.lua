local skynet = require "skynet"
require "skynet.manager"	-- import skynet.launch, ...
local core = require "skynet.core"

local foreign = require "pyskynet.foreign"
local thlua = require "thlua"

----------------
-- BOOT items --
----------------
local BOOT = {}

-- for pyskynet repl
function BOOT.repl(script)
	local func, err = thlua.load("return "..script, "(pyskynet repl)")
	if not func then
		func, err = thlua.load(script, "(pyskynet repl)")
	end
	if not func then
		print(err)
	else
		local function evalprint(ok, ...)
			if ok and (select("#", ...) >= 1) then
				print(...)
			end
		end
		evalprint(xpcall(func, function(...)
			print(...)
			print(debug.traceback())
		end))
	end
end

-- for pyskynet script
function BOOT.cmdline(scriptName, ...)
	local main, err = loadfile(scriptName)
	if not main then
		error(err)
	else
		main(...)
	end
end

-- foreign message for boot, command line pyskynet xxx
foreign.dispatch(BOOT)

------------------
-- HARBOR items --
------------------

local globalname = {}
local queryname = {}
local HARBOR = {}
local harbor_service

skynet.register_protocol {
	name = "harbor",
	id = skynet.PTYPE_HARBOR,
	pack = function(...) return ... end,
	unpack = skynet.tostring,
	dispatch = function() end
}

skynet.register_protocol {
	name = "text",
	id = skynet.PTYPE_TEXT,
	pack = function(...) return ... end,
	unpack = skynet.tostring,
	dispatch = function() end
}

local function response_name(name)
	local address = globalname[name]
	if queryname[name] then
		local tmp = queryname[name]
		queryname[name] = nil
		for _,resp in ipairs(tmp) do
			resp(true, address)
		end
	end
end

function HARBOR.REGISTER(name, handle)
	assert(globalname[name] == nil)
	globalname[name] = handle
	response_name(name)
	skynet.redirect(harbor_service, handle, "harbor", 0, "N " .. name)
end

function HARBOR.QUERYNAME(name)
	if name:byte() == 46 then	-- "." , local name
		skynet.ret(skynet.pack(skynet.localname(name)))
		return
	end
	local result = globalname[name]
	if result then
		skynet.ret(skynet.pack(result))
		return
	end
	local queue = queryname[name]
	if queue == nil then
		queue = { skynet.response() }
		queryname[name] = queue
	else
		table.insert(queue, skynet.response())
	end
end

function HARBOR.LINK(id)
	skynet.ret()
end

function HARBOR.CONNECT(id)
	skynet.error("Can't connect to other harbor in single node mode")
end

-- lua message for harbor, used for name register
skynet.dispatch("lua", function (session,source,command,...)
	local f = assert(HARBOR[command])
	f(...)
end)

skynet.start(function()
	-- 1. harbor, self as .cslave
	harbor_service = assert(skynet.launch("harbor", 0, skynet.self()))
    skynet.name(".cslave", skynet.self())

    -- 2. launcher, .launcher
    local launcher = assert(skynet.launch("snlua","launcher"))
    skynet.name(".launcher", launcher)

    -- 3. service_mgr, .service
    skynet.newservice "service_mgr"

    -- 4. wakeup .python
	core.send(".python", 0, 0, skynet.pack(skynet.self()))
end)
