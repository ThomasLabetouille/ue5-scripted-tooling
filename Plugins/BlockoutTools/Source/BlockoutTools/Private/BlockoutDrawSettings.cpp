#include "BlockoutDrawSettings.h"

UBlockoutDrawSettings* UBlockoutDrawSettings::Get()
{
	// Le CDO d'une UCLASS(config=...) charge ses UPROPERTY(config) automatiquement.
	// C'est volontairement la meme instance pour le panneau Slate et pour le mode :
	// activer le mode ne doit PAS recopier des valeurs, sinon un champ modifie pendant
	// que le mode est actif serait ignore sans que rien ne le dise.
	return GetMutableDefault<UBlockoutDrawSettings>();
}
