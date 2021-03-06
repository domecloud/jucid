#!/usr/bin/lua

local juci = require("juci/core"); 

function system_filesystems(opts)
	local res = {}; 
	local lines = {}; 
	res["filesystems"] = lines; 
	local stdout = juci.shell("df | tail -n+2"); 
	for line in stdout:gmatch("[^\r\n]+") do 
		local filesystem,total,used,free,percent,path = line:match("([^%s]*)%s*([^%s]*)%s*([^%s]*)%s*([^%s]*)%s*([^%s]*)%s*([^%s]*)%s*"); 
		local obj = {
			["filesystem"] = filesystem, 
			["total"] = total, 
			["used"] = used, 
			["free"] = free,
			["path"] = path
		}; 
		table.insert(lines, obj); 
	end
	return res; 
end

function system_logread(opts)
	local filter = opts.filter;
	local type = opts.type;
	local res = {}; 
	local lines = {}; 
	local limit = 20
	res["lines"] = lines; 
	local stdout;
	if opts.limit then limit = opts.limit; end
	if limit > 200 then limit = 200; end
	-- TODO: change so that filter and limit is sent in insted of concatenated does not work with | now
	if ((not filter or filter == "") and (not type or type == "")) then
		stdout = juci.shell("logread | tail -n %d 2>/dev/null", limit); 
	elseif (not filter or filter == "") then
		stdout = juci.shell("logread | awk '{if($6~/"..type.."/) print($0)}' | tail -n %d 2>/dev/null", limit);
	elseif(not type or type == "") then
		stdout = juci.shell("logread | awk '{if($7~/"..filter.."/) print($0)}' | tail -n %d 2>/dev/null", limit);
	else
		stdout = juci.shell("logread | awk '{if($6~/"..type.."/ && $7~/"..filter.."/) print($0)}' | tail -n %d 2>/dev/null", limit); 
	end
	for line in stdout:gmatch("[^\r\n]+") do 
		local date,type,source,message = line:match("([^%s]*%s+[^%s]*%s+[^%s]*%s+[^%s]*%s+[^%s]*)%s+([^%s]*)%s+([^%s:]*):%s+(.*)"); 
		string.gsub(message, "\n", ""); 
		local obj = {
			["date"] = date, 
			["type"] = type, 
			["source"] = source, 
			["message"] = message
		}; 
		table.insert(lines, obj); 
	end
	return res; 
end

function system_reboot()
	juci.shell("/sbin/reboot 2>/dev/null"); 
	return {}; 
end

function system_defaultreset()
	juci.shell("/sbin/defaultreset 2> /dev/null"); 
	return {}; 
end

function trim(s)
  -- from PiL2 20.4
  	if(not s) then return nil; end
    return (s:gsub("^%s*(.-)%s*$", "%1"))
end

function system_info()
	local system = {}; 
	local db_str = juci.shell("db show hw.board 2>/dev/null"); 
	local db = {}; 
	local cpuinfo = { }; 
	local cpuload = {} 
	for line in db_str:gmatch("[^\r\n]+") do 
		local name, value = line:match("([^=]+)=(.*)"); 
		db[name] = value; 
	end
	for line in tostring(juci.readfile("/proc/cpuinfo")):gmatch("[^\r\n]+") do 
		local name, value = line:match("([^:]+):(.*)"); 
		if( name) then 
			cpuinfo[trim(name)] = trim(value); 
		end
	end

	-- replace the old ugly cpu usage code with new load reporting code (will report load in jiffies)
	-- to get cpu usage, use the counters and sample them over a time interval
	local stat = juci.readfile("/proc/stat"); 
	for line in stat:gmatch("[^\r\n]+") do
		if(line:sub(1,4) == "cpu ") then
			local usr,nic,sys,idle,iowait,irq,softirq,steal = line:match("cpu%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s+.*"); 
			local cpu = {
				timestamp = os.time(), 
				usr = tonumber(usr), 
				nic = tonumber(nic), 
				sys = tonumber(sys), 
				idle = tonumber(idle), 
				iowait = tonumber(iowait), 
				irq = tonumber(irq), 
				softirq = tonumber(softirq), 
				steal = tonumber(steal)
			}; 
			cpu.total = cpu.usr + cpu.nic + cpu.sys + cpu.idle + cpu.iowait + cpu.irq + cpu.softirq + cpu.steal; 
			cpu.busy = cpu.total - cpu.idle - cpu.iowait; 
			system.cpu = cpu; 
		end
	end

	--local numcpus = tonumber(juci.shell("grep processor /proc/cpuinfo | wc -l"):match("(%S+).*")); 
	--local load1, load5, load15 = juci.readfile("/proc/loadavg"):match("(%S+)%s+(%S+)%s+(%S+).+"); 
	--cpuload.avg = { tonumber(load1) / numcpus * 1000, tonumber(load5) / numcpus * 1000, tonumber(load15) / numcpus * 1000};  
	local version = tostring(juci.readfile("/proc/version") or ""); 
	local systype,_,sysversion = version:match("(%S+)%s+(%S+)%s+(%S+)"); 	
	system["name"] = juci.readfile("/proc/sys/kernel/hostname"); 
	system["hardware"] = db["hw.board.hardwareVersion"]; 
	system["model"] = db["hw.board.routerModel"]; 
	system["filesystem"] = db["hw.board.filesystem"];
	system["boardid"] = db["hw.board.boardId"]; 
	system["firmware"] = db["hw.board.iopVersion"]; 
	system["brcmver"] = db["hw.board.brcmVersion"]; 
	system["socmod"] = cpuinfo["Revision"]; --db["hw.board.socModel"]; 
	system["socver"] = cpuinfo["Hardware"]; --db["hw.board.socRevision"]; 
	system["cfever"] = db["hw.board.cfeVersion"]; 
	system["kernel"] = systype.." "..sysversion; --db["hw.board.kernelVersion"]; 
	system["basemac"] = db["hw.board.BaseMacAddr"]; 
	system["serialno"] = cpuinfo["Serial"]; --db["hw.board.serialNumber"]; 
	--system["filesystem"] = juci.shell("awk '$2 ~ /rom/{print $3}' /proc/mounts"):match("%S+"); 	

	local keys = {
		auth = db["hw.board.authKey"], 
		des = db["hw.board.desKey"], 
		wpa = db["hw.board.wpaKey"]
	}; 
	
	local mem_str = juci.readfile("/proc/meminfo"); 
	local meminfo = {}; 
	for line in mem_str:gmatch("[^\r\n]+") do
		local name, value = line:match("([^:]+):%s+([^%s])");
		meminfo[name] = value; 
	end 
	
	local memory = {
		total = meminfo["MemTotal"],
		used = meminfo["MemTotal"] - meminfo["MemFree"], 
		free = meminfo["MemFree"]
	}; 
	
	local ethernet = {}; 
	local port_names = {}; 
	local port_order = {}; 
	local ports = {}; 
	if db["hw.board.ethernetPortNames"] then 
		for v in string.gmatch(db["hw.board.ethernetPortNames"], "[^%s]+") do table.insert(port_names, v); end
	end
	if db["hw.board.ethernetPortOrder"] then 
		for v in string.gmatch(db["hw.board.ethernetPortOrder"], "[^%s]+") do table.insert(port_order, v); end
	end
	for i,v in ipairs(port_names) do table.insert(ports, {name=v, device = port_order[i]}); end
	
	return {
		system = system, 
		load = cpuload, 
		keys = keys, 
		memoryKB = memory, 
		eth_ports = ports
	}; 
end

return {
	["info"] = system_info, 
	["log"] = system_logread, 
	["defaultreset"] = system_defaultreset,
	["filesystems"] = system_filesystems, 
	["reboot"] = system_reboot
}; 

