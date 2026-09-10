#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class SDockTab;
class FSpawnTabArgs;

/**
 * Module editeur du plugin "Outil Blockout".
 * Enregistre l'onglet dockable et l'entree de menu Tools -> Outil Blockout.
 * Aucun code de gameplay, aucune dependance a un projet particulier : ce plugin
 * peut etre depose tel quel dans n'importe quel projet UE5.
 */
class FBlockoutToolsModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedRef<SDockTab> SpawnBlockoutTab(const FSpawnTabArgs& Args);
    void RegisterMenus();
};
