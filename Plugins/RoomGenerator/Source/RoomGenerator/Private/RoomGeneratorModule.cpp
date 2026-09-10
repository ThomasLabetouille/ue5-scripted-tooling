#include "RoomGeneratorModule.h"
#include "SClaudeChatPanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "RoomGenerator"

static const FName ClaudeTabName("ClaudeAssistant");

void FRoomGeneratorModule::StartupModule()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        ClaudeTabName,
        FOnSpawnTab::CreateRaw(this, &FRoomGeneratorModule::SpawnClaudeTab))
        .SetDisplayName(LOCTEXT("ClaudeTabTitle", "Claude AI"))
        .SetTooltipText(LOCTEXT("ClaudeTabTooltip", "Ton agent IA pour RPG_Test"))
        .SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

    // Le panneau "Outil Blockout" (SBlockoutToolPanel) vit desormais uniquement dans le
    // plugin BlockoutTools (2026-07-30, session 21) — la copie ici a ete supprimee pour
    // eliminer la divergence entre les deux fichiers de ~3700 lignes. RPG_Test consomme
    // BlockoutTools comme n'importe quel autre projet (voir RPG_Test.uproject).

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FRoomGeneratorModule::RegisterMenus));
}

void FRoomGeneratorModule::ShutdownModule()
{
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(ClaudeTabName);
}

TSharedRef<SDockTab> FRoomGeneratorModule::SpawnClaudeTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SClaudeChatPanel)
        ];
}

void FRoomGeneratorModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
    FToolMenuSection& Section = Menu->FindOrAddSection("Tools");
    Section.AddMenuEntry(
        "OpenClaudeAssistant",
        LOCTEXT("OpenClaude", "Claude AI"),
        LOCTEXT("OpenClaudeTooltip", "Ouvre le panneau Claude AI"),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([]()
        {
            FGlobalTabmanager::Get()->TryInvokeTab(FName("ClaudeAssistant"));
        })));
}

#undef LOCTEXT_NAMESPACE
