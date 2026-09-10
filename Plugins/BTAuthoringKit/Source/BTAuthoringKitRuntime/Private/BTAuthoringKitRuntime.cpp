#include "Modules/ModuleManager.h"

class FBTAuthoringKitRuntimeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FBTAuthoringKitRuntimeModule, BTAuthoringKitRuntime)
