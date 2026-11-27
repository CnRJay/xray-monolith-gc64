#include "stdafx.h"
#include "xrParams.h"

#include <magic_enum/magic_enum.hpp>

void xrParams::LoadParams()
{
	xr_string CommandLine = Core.Params;
	auto CommandList = CommandLine.Split(' ');

	for (auto& Command : CommandList)
	{
		if (!Command.StartWith("-"))
			continue;

		Command = Command.substr(1);

		// Special handle for some commands
		if (Command == "60hz") Command = "_" + Command;
		else if (Command == "-dxgi-old") Command = "dxgi_old";
		else if (Command == "-dxgi-dbg") Command = "dxgi_dbg";

		auto EnumData = magic_enum::enum_cast<ECoreParams>(Command);
		if (EnumData.has_value())
		{
			Core.ParamsData.set(EnumData.value(), true);
		}
	}
}