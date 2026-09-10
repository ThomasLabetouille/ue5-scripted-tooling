#include "BlockoutToolsModule.h"
#include "SBlockoutToolPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "BlockoutTools"

// Historique (jusqu'au 2026-07-30, session 20) : ce nom devait rester DIFFERENT de
// "BlockoutTool" (RoomGenerator), qui embarquait alors sa propre copie de ce panneau —
// deux modules enregistrant le meme nom d'onglet nomade se marchent dessus au demarrage
// de l'editeur. La copie de RoomGenerator a ete supprimee le 2026-07-30 (session 21,
// voir CLAUDE.md/GAME_MEMORY.md) : RPG_Test consomme desormais ce plugin comme seule
// source du panneau. Le nom "BlockoutToolsPanel" est conserve tel quel (renommer n'aurait
// aucun benefice fonctionnel et casserait le layout d'onglet deja sauvegarde des
// utilisateurs existants).
static const FName BlockoutTabName("BlockoutToolsPanel");

void FBlockoutToolsModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        BlockoutTabName,
        FOnSpawnTab::CreateRaw(this, &FBlockoutToolsModule::SpawnBlockoutTab))
        .SetDisplayName(LOCTEXT("BlockoutTabTitle", "Outil Blockout"))
        .SetTooltipText(LOCTEXT("BlockoutTabTooltip", "Generation et verification de geometrie de blockout"))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FBlockoutToolsModule::RegisterMenus));
}

void FBlockoutToolsModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(BlockoutTabName);
}

TSharedRef<SDockTab> FBlockoutToolsModule::SpawnBlockoutTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SBlockoutToolPanel)
        ];
}

void FBlockoutToolsModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
    FToolMenuSection& Section = Menu->FindOrAddSection("Tools");
    // Meme remarque que pour BlockoutTabName : identifiant d'entree de menu distinct
    // de celui de RoomGenerator tant que les deux plugins cohabitent.
    Section.AddMenuEntry(
        "OpenBlockoutToolsPanel",
        LOCTEXT("OpenBlockout", "Outil Blockout"),
        LOCTEXT("OpenBlockoutTooltip", "Ouvre le panneau d'outils de blockout"),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]()
        {
            FGlobalTabmanager::Get()->TryInvokeTab(BlockoutTabName);
        })));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlockoutToolsModule, BlockoutTools)
