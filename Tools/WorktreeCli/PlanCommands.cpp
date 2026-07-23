#include "PlanCommands.h"

#include "PlanScheduler.h"

namespace toolcli
{
	int RunPlanCommand(int iArgumentCount, wchar_t* pArgumentValues[])
	{
		return RunPlanSchedulerCommand(iArgumentCount, pArgumentValues);
	}
}
