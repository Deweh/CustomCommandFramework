#include "CCF_API.h"

namespace Commands
{
	void InstallHooks();
	void RegisterCommand(const char* name, CCF::CommandCallback func);
}