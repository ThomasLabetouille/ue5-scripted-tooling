#pragma once
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Http.h"
#include "Dom/JsonObject.h"
#include "ClaudeEditorSubsystem.generated.h"

// Delegates — broadcast sur le Game Thread
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnClaudeMessage,  const FString& /*Text*/, bool /*bIsError*/);
DECLARE_MULTICAST_DELEGATE(FOnClaudeThinking);
DECLARE_MULTICAST_DELEGATE(FOnClaudeDone);

UCLASS()
class ROOMGENERATOR_API UClaudeEditorSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** Envoie un message utilisateur ; les delegates notifient le panneau en retour. */
    void SendMessage(const FString& UserMessage);

    /** Efface l'historique de conversation. */
    void ClearHistory();

    /** Execute du Python dans l'editeur via IPythonScriptPlugin. */
    FString ExecutePython(const FString& Code);

    /** Cle API Groq — lue depuis GROQ_API_KEY, le fichier de config, ou saisie dans le panel. */
    FString ApiKey;

    /** Sauvegarde la cle API dans ProjectSaved/Claude/apikey.txt */
    void SaveApiKey();

    FOnClaudeMessage  OnMessage;
    FOnClaudeThinking OnThinking;
    FOnClaudeDone     OnDone;

private:
    TArray<TSharedPtr<FJsonObject>> History;
    FString SystemPrompt;
    bool bIsProcessing = false;

    void LoadSystemPrompt();
    void CallApi();
    void OnHttpResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
    void HandleResponse(TSharedPtr<FJsonObject> ResponseObj);
};
