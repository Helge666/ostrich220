-- SPDX-License-Identifier: GPL-3.0-or-later
-- Press keys of the emulated U-220 on a schedule, for headless runs.
--
--   U220_PRESS="bit@seconds[:hold],..."  e.g. U220_PRESS="3@4,3@6:0.5"
--   mame u220 ... -autoboot_script tools/u220_keys.lua
--
-- Keys are the KEYS port's bits (roland_u220.cpp).  Default hold is 0.2 s.

local plan = {}
for item in string.gmatch(os.getenv("U220_PRESS") or "", "[^,]+") do
	local bit, at, hold = string.match(item, "^(%d+)@([%d.]+):?([%d.]*)$")
	if bit then
		table.insert(plan, { mask = 1 << tonumber(bit), from = tonumber(at),
			to = tonumber(at) + (tonumber(hold) or 0.2) })
	end
end

local port = manager.machine.ioport.ports[":KEYS"]
local fields = {}
for _, f in pairs(port.fields) do fields[f.mask] = f end

local held = {}
emu.register_periodic(function()
	local t = manager.machine.time:as_double()
	for _, p in ipairs(plan) do
		local f = fields[p.mask]
		local want = (t >= p.from and t < p.to) and 1 or 0
		if f and held[p] ~= want then
			f:set_value(want)
			held[p] = want
			emu.print_info(string.format("u220_keys: %.3f bit mask %04x %s", t, p.mask,
				want == 1 and "down" or "up"))
		end
	end
end)
