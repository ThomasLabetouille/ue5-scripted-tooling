#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class SDockTab;
class FSpawnTabArgs;

class FRoomGeneratorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedRef<SDockTab> SpawnClaudeTab(const FSpawnTabArgs& Args);
    void RegisterMenus();
};
