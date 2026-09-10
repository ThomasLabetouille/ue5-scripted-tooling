#include "Modules/ModuleManager.h"

// Module minimal : toute la logique vit dans UBTAuthoringLibrary (BTAuthoringLibrary.h/.cpp).
// Rien a initialiser/nettoyer au chargement — pas de UI, pas de commande enregistree, juste une
// bibliotheque de fonctions exposees a Python via BlueprintCallable.
class FBTAuthoringKitEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FBTAuthoringKitEditorModule, BTAuthoringKitEditor)
