#include "ClaudeEditorSubsystem.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "Containers/StringConv.h"
#include "Editor.h"

// ---------------------------------------------------------------------------
// Obfuscation legere de la cle API stockee sur disque — PAS un chiffrement reel
// ---------------------------------------------------------------------------
// Fix 2026-07-21 (audit RoomGenerator) : la cle etait stockee en clair dans
// Saved/Claude/apikey.txt. Ce projet n'a pas de depot Git (verifie au moment du fix), donc le
// risque de fuite via un commit est nul en pratique aujourd'hui — mais un simple XOR+Base64
// evite au moins qu'un partage d'ecran, une synchro cloud du dossier Saved/, ou un coup d'oeil
// dans l'Explorateur n'expose la cle en clair a l'oeil nu.
// AVERTISSEMENT HONNETE : ceci n'est PAS un chiffrement. Le pad XOR est fixe et vit dans ce
// binaire — quiconque a acces au code source (ou desassemble le plugin) retrouve la cle
// trivialement. Pour une vraie protection il faudrait un coffre d'identifiants OS (Windows
// Credential Manager via DPAPI) — hors scope de ce fix, a envisager si la cle devient sensible
// (compte payant, quota partage, etc.) plutot qu'un compte gratuit Groq a usage local.
static const TCHAR* ObfuscationPad = TEXT("RPG_Test-RoomGenerator-2026");

static FString ObfuscateKeyForStorage(const FString& PlainKey)
{
    if (PlainKey.IsEmpty()) return FString();

    FTCHARToUTF8 Convert(*PlainKey);
    TArray<uint8> Bytes((const uint8*)Convert.Get(), Convert.Length());
    const int32 PadLen = FCString::Strlen(ObfuscationPad);
    for (int32 i = 0; i < Bytes.Num(); ++i)
        Bytes[i] ^= (uint8)ObfuscationPad[i % PadLen];

    return TEXT("OBF1:") + FBase64::Encode(Bytes);
}

// Retourne la cle en clair. Gere aussi la migration transparente depuis un ancien fichier
// stocke en clair (pas de prefixe "OBF1:" -> on suppose que le contenu EST deja la cle en
// clair, comme avant ce fix) — le prochain SaveApiKey() le reecrit obfusque.
static FString DeobfuscateKeyFromStorage(const FString& Stored)
{
    FString Trimmed = Stored.TrimStartAndEnd();
    if (Trimmed.IsEmpty()) return FString();
    if (!Trimmed.StartsWith(TEXT("OBF1:")))
        return Trimmed; // ancien format en clair, migration au prochain SaveApiKey()

    FString B64 = Trimmed.RightChop(5);
    TArray<uint8> Bytes;
    if (!FBase64::Decode(B64, Bytes))
        return Trimmed; // decodage rate : ne pas casser un cas limite, traiter comme en clair

    const int32 PadLen = FCString::Strlen(ObfuscationPad);
    for (int32 i = 0; i < Bytes.Num(); ++i)
        Bytes[i] ^= (uint8)ObfuscationPad[i % PadLen];

    FUTF8ToTCHAR ConvertBack((const ANSICHAR*)Bytes.GetData(), Bytes.Num());
    return FString(ConvertBack.Length(), ConvertBack.Get());
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

void UClaudeEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Priorite 1 : variable d'environnement
    ApiKey = FPlatformMisc::GetEnvironmentVariable(TEXT("GROQ_API_KEY"));

    // Priorite 2 : fichier de config persistant (si env var absente)
    if (ApiKey.IsEmpty())
    {
        FString KeyFilePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Claude"), TEXT("apikey.txt"));
        FString RawStored;
        FFileHelper::LoadFileToString(RawStored, *KeyFilePath);
        ApiKey = DeobfuscateKeyFromStorage(RawStored);
        // Ignorer le placeholder mal sauvegarde par une ancienne version du code
        if (ApiKey == TEXT("(chargee depuis GROQ_API_KEY)"))
        {
            ApiKey = TEXT("");
            UE_LOG(LogTemp, Warning, TEXT("ClaudePanel: placeholder detecte dans apikey.txt, ignore. Ressaisir la cle dans le panneau."));
        }
        else if (!ApiKey.IsEmpty())
        {
            UE_LOG(LogTemp, Log, TEXT("ClaudePanel: cle API chargee depuis %s"), *KeyFilePath);
        }
    }

    LoadSystemPrompt();
}

void UClaudeEditorSubsystem::SaveApiKey()
{
    FString KeyFilePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Claude"), TEXT("apikey.txt"));
    FString Dir         = FPaths::GetPath(KeyFilePath);
    IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
    FString ToWrite = ObfuscateKeyForStorage(ApiKey);
    FFileHelper::SaveStringToFile(ToWrite, *KeyFilePath, FFileHelper::EEncodingOptions::ForceUTF8);
    UE_LOG(LogTemp, Log, TEXT("ClaudePanel: cle API sauvegardee (obfusquee) dans %s"), *KeyFilePath);
}

void UClaudeEditorSubsystem::LoadSystemPrompt()
{
    // Priorite 1 : CLAUDE_AGENT.md (prompt court optimise pour l'agent UE5)
    FString AgentMdPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("CLAUDE_AGENT.md"));
    if (FFileHelper::LoadFileToString(SystemPrompt, *AgentMdPath))
    {
        UE_LOG(LogTemp, Log, TEXT("ClaudePanel: CLAUDE_AGENT.md charge (%d chars)."), SystemPrompt.Len());
        return;
    }

    // Fallback : CLAUDE.md tronque a 4000 chars pour rester sous les limites TPM
    FString ClaudeMdPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("CLAUDE.md"));
    if (FFileHelper::LoadFileToString(SystemPrompt, *ClaudeMdPath))
    {
        if (SystemPrompt.Len() > 4000)
        {
            SystemPrompt = SystemPrompt.Left(4000);
            SystemPrompt += TEXT("\n[...tronque pour limites TPM...]");
        }
        UE_LOG(LogTemp, Log, TEXT("ClaudePanel: CLAUDE.md charge (%d chars)."), SystemPrompt.Len());
    }
    else
    {
        SystemPrompt = TEXT("Tu es un expert Unreal Engine 5.7 integre dans l'editeur. "
                            "Utilise execute_python pour toute action dans UE5. "
                            "Commence par `from ue5_utils import *`.");
        UE_LOG(LogTemp, Warning, TEXT("ClaudePanel: aucun fichier prompt trouve, defaut utilise."));
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void UClaudeEditorSubsystem::SendMessage(const FString& UserMessage)
{
    if (bIsProcessing) return;

    if (ApiKey.IsEmpty())
    {
        OnMessage.Broadcast(
            TEXT("Cle API manquante.\n"
                 "1. Cree un compte GRATUIT sur console.groq.com\n"
                 "2. Cree une cle API (commence par gsk_)\n"
                 "3. Colle-la dans le champ 'Cle API' ci-dessous\n"
                 "   OU definis la variable d'environnement GROQ_API_KEY"), true);
        return;
    }

    bIsProcessing = true;
    OnThinking.Broadcast();

    // Ajouter le message utilisateur a l'historique (format OpenAI)
    TSharedPtr<FJsonObject> UserMsg = MakeShareable(new FJsonObject);
    UserMsg->SetStringField(TEXT("role"),    TEXT("user"));
    UserMsg->SetStringField(TEXT("content"), UserMessage);
    History.Add(UserMsg);

    CallApi();
}

void UClaudeEditorSubsystem::ClearHistory()
{
    History.Empty();
    bIsProcessing = false;
}

// ---------------------------------------------------------------------------
// HTTP call — format OpenAI (Groq)
// ---------------------------------------------------------------------------

void UClaudeEditorSubsystem::CallApi()
{
    // --- Message systeme (va en tete du tableau messages) ---
    TSharedPtr<FJsonObject> SysMsg = MakeShareable(new FJsonObject);
    SysMsg->SetStringField(TEXT("role"),    TEXT("system"));
    SysMsg->SetStringField(TEXT("content"), SystemPrompt);

    // --- Tableau messages : system + historique ---
    TArray<TSharedPtr<FJsonValue>> MessagesArr;
    MessagesArr.Add(MakeShareable(new FJsonValueObject(SysMsg)));
    for (const TSharedPtr<FJsonObject>& Msg : History)
    {
        MessagesArr.Add(MakeShareable(new FJsonValueObject(Msg)));
    }

    // --- Definition de l'outil (format OpenAI) ---
    TSharedPtr<FJsonObject> CodeProp = MakeShareable(new FJsonObject);
    CodeProp->SetStringField(TEXT("type"),
        TEXT("string"));
    CodeProp->SetStringField(TEXT("description"),
        TEXT("Code Python a executer dans UE5.7. "
             "Utilise ue5_utils, BPGraph DSL, BatchWireGraph, subsystems C++ custom."));

    TSharedPtr<FJsonObject> Props = MakeShareable(new FJsonObject);
    Props->SetObjectField(TEXT("code"), CodeProp);

    TArray<TSharedPtr<FJsonValue>> Required;
    Required.Add(MakeShareable(new FJsonValueString(TEXT("code"))));

    TSharedPtr<FJsonObject> Parameters = MakeShareable(new FJsonObject);
    Parameters->SetStringField(TEXT("type"),       TEXT("object"));
    Parameters->SetObjectField(TEXT("properties"), Props);
    Parameters->SetArrayField(TEXT("required"),    Required);

    TSharedPtr<FJsonObject> FnDef = MakeShareable(new FJsonObject);
    FnDef->SetStringField(TEXT("name"),        TEXT("execute_python"));
    FnDef->SetStringField(TEXT("description"),
        TEXT("Execute du code Python dans Unreal Engine 5.7 editor. "
             "Acces complet : ue5_utils, BPGraph DSL, BatchWireGraph, "
             "URoomGeneratorSubsystem, UBlueprintEditingSubsystem. "
             "IMPORTANT : chaque execution est automatiquement validee (test_suite "
             "avant/apres) et se termine par une ligne '--- VERDICT : ... ---'. "
             "OK = aucune regression. REGRESSION DETECTEE ou ECHEC = la modification "
             "n'est pas sure, corriger avant de continuer plutot que d'ignorer ce "
             "verdict ou de relancer le meme code tel quel."));
    FnDef->SetObjectField(TEXT("parameters"),  Parameters);

    TSharedPtr<FJsonObject> Tool = MakeShareable(new FJsonObject);
    Tool->SetStringField(TEXT("type"),     TEXT("function"));
    Tool->SetObjectField(TEXT("function"), FnDef);

    TArray<TSharedPtr<FJsonValue>> ToolsArr;
    ToolsArr.Add(MakeShareable(new FJsonValueObject(Tool)));

    // --- Payload racine ---
    TSharedPtr<FJsonObject> Payload = MakeShareable(new FJsonObject);
    Payload->SetStringField(TEXT("model"),      TEXT("llama-3.3-70b-versatile"));
    Payload->SetNumberField(TEXT("max_tokens"), 4096);
    Payload->SetArrayField(TEXT("messages"),    MessagesArr);
    Payload->SetArrayField(TEXT("tools"),       ToolsArr);

    FString JsonBody;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonBody);
    FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);

    // --- Requete HTTP ---
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
        FHttpModule::Get().CreateRequest();
    Request->SetURL(TEXT("https://api.groq.com/openai/v1/chat/completions"));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Authorization"),
        FString::Printf(TEXT("Bearer %s"), *ApiKey));
    Request->SetHeader(TEXT("content-type"), TEXT("application/json"));
    Request->SetContentAsString(JsonBody);
    Request->OnProcessRequestComplete().BindUObject(
        this, &UClaudeEditorSubsystem::OnHttpResponse);
    Request->ProcessRequest();
}

// ---------------------------------------------------------------------------
// HTTP response
// ---------------------------------------------------------------------------

void UClaudeEditorSubsystem::OnHttpResponse(
    FHttpRequestPtr /*Request*/, FHttpResponsePtr Response, bool bWasSuccessful)
{
    if (!bWasSuccessful || !Response.IsValid())
    {
        bIsProcessing = false;
        OnMessage.Broadcast(TEXT("Echec de la requete HTTP. Verifie ta connexion."), true);
        OnDone.Broadcast();
        return;
    }

    const int32 Code = Response->GetResponseCode();
    const FString Body = Response->GetContentAsString();

    if (Code != 200)
    {
        bIsProcessing = false;
        OnMessage.Broadcast(FString::Printf(TEXT("Erreur API %d : %s"), Code, *Body), true);
        OnDone.Broadcast();
        return;
    }

    TSharedPtr<FJsonObject> RespObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
    if (!FJsonSerializer::Deserialize(Reader, RespObj) || !RespObj.IsValid())
    {
        bIsProcessing = false;
        OnMessage.Broadcast(TEXT("Reponse JSON invalide."), true);
        OnDone.Broadcast();
        return;
    }

    HandleResponse(RespObj);
}

void UClaudeEditorSubsystem::HandleResponse(TSharedPtr<FJsonObject> ResponseObj)
{
    // Format OpenAI : choices[0].message
    const TArray<TSharedPtr<FJsonValue>>* ChoicesArr = nullptr;
    if (!ResponseObj->TryGetArrayField(TEXT("choices"), ChoicesArr) || ChoicesArr->Num() == 0)
    {
        bIsProcessing = false;
        OnMessage.Broadcast(TEXT("Champ 'choices' manquant dans la reponse."), true);
        OnDone.Broadcast();
        return;
    }

    TSharedPtr<FJsonObject> Choice = (*ChoicesArr)[0]->AsObject();
    if (!Choice.IsValid())
    {
        bIsProcessing = false;
        OnMessage.Broadcast(TEXT("Reponse 'choices' invalide."), true);
        OnDone.Broadcast();
        return;
    }

    const TSharedPtr<FJsonObject>* MsgPtr = nullptr;
    if (!Choice->TryGetObjectField(TEXT("message"), MsgPtr))
    {
        bIsProcessing = false;
        OnMessage.Broadcast(TEXT("Champ 'message' manquant."), true);
        OnDone.Broadcast();
        return;
    }
    TSharedPtr<FJsonObject> Message = *MsgPtr;

    // Contenu texte (peut etre null si tool_calls uniquement)
    FString TextContent;
    Message->TryGetStringField(TEXT("content"), TextContent);
    if (!TextContent.IsEmpty())
    {
        OnMessage.Broadcast(TextContent, false);
    }

    // Tool calls ?
    const TArray<TSharedPtr<FJsonValue>>* ToolCallsArr = nullptr;
    if (Message->TryGetArrayField(TEXT("tool_calls"), ToolCallsArr) && ToolCallsArr->Num() > 0)
    {
        // Sauvegarder le message assistant avec tool_calls dans l'historique
        TSharedPtr<FJsonObject> AssistantMsg = MakeShareable(new FJsonObject);
        AssistantMsg->SetStringField(TEXT("role"), TEXT("assistant"));
        if (!TextContent.IsEmpty())
            AssistantMsg->SetStringField(TEXT("content"), TextContent);
        else
            AssistantMsg->SetField(TEXT("content"), MakeShareable(new FJsonValueNull()));
        AssistantMsg->SetArrayField(TEXT("tool_calls"), *ToolCallsArr);
        History.Add(AssistantMsg);

        // Executer chaque outil
        for (const TSharedPtr<FJsonValue>& ToolCallVal : *ToolCallsArr)
        {
            TSharedPtr<FJsonObject> ToolCall = ToolCallVal->AsObject();
            if (!ToolCall.IsValid()) continue;

            FString ToolCallId;
            ToolCall->TryGetStringField(TEXT("id"), ToolCallId);

            const TSharedPtr<FJsonObject>* FnObjPtr = nullptr;
            if (!ToolCall->TryGetObjectField(TEXT("function"), FnObjPtr)) continue;
            TSharedPtr<FJsonObject> FnObj = *FnObjPtr;

            FString FnName, FnArgsStr;
            FnObj->TryGetStringField(TEXT("name"),      FnName);
            FnObj->TryGetStringField(TEXT("arguments"), FnArgsStr);

            FString ToolResult;
            if (FnName == TEXT("execute_python"))
            {
                // Deserialiser les arguments (JSON string -> objet)
                FString Code;
                TSharedPtr<FJsonObject> ArgsObj;
                TSharedRef<TJsonReader<>> ArgReader = TJsonReaderFactory<>::Create(FnArgsStr);
                if (FJsonSerializer::Deserialize(ArgReader, ArgsObj) && ArgsObj.IsValid())
                {
                    ArgsObj->TryGetStringField(TEXT("code"), Code);
                }

                // Afficher le code
                OnMessage.Broadcast(FString::Printf(TEXT("```python\n%s\n```"), *Code), false);

                // Executer
                ToolResult = ExecutePython(Code);
                OnMessage.Broadcast(FString::Printf(TEXT("Resultat : %s"), *ToolResult), false);
            }
            else
            {
                ToolResult = FString::Printf(TEXT("Outil inconnu : %s"), *FnName);
            }

            // Ajouter le resultat a l'historique (role "tool" format OpenAI)
            TSharedPtr<FJsonObject> ToolResultMsg = MakeShareable(new FJsonObject);
            ToolResultMsg->SetStringField(TEXT("role"),         TEXT("tool"));
            ToolResultMsg->SetStringField(TEXT("tool_call_id"), ToolCallId);
            ToolResultMsg->SetStringField(TEXT("content"),      ToolResult);
            History.Add(ToolResultMsg);
        }

        // Rappeler l'API pour la suite de la conversation
        OnThinking.Broadcast();
        CallApi();
        return;
    }

    // Pas de tool_calls : sauvegarder le message assistant et terminer
    TSharedPtr<FJsonObject> AssistantMsg = MakeShareable(new FJsonObject);
    AssistantMsg->SetStringField(TEXT("role"),    TEXT("assistant"));
    AssistantMsg->SetStringField(TEXT("content"), TextContent);
    History.Add(AssistantMsg);

    bIsProcessing = false;
    OnDone.Broadcast();
}

// ---------------------------------------------------------------------------
// Python execution — via fichier temporaire + commande console "py"
// Ne necessite pas WITH_PYTHON ni IPythonScriptPlugin au compile time.
// ---------------------------------------------------------------------------

FString UClaudeEditorSubsystem::ExecutePython(const FString& Code)
{
    if (!GEditor)
    {
        return TEXT("ERREUR : GEditor non disponible.");
    }

    FString TempDir    = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Claude"));
    FString ExecFile   = FPaths::Combine(TempDir, TEXT("_exec.py"));
    FString WrapFile   = FPaths::Combine(TempDir, TEXT("_wrap.py"));
    FString OutputFile = FPaths::Combine(TempDir, TEXT("_output.txt"));
    IFileManager::Get().MakeDirectory(*TempDir, true);

    // 1. Ecrire le code utilisateur
    if (!FFileHelper::SaveStringToFile(Code, *ExecFile, FFileHelper::EEncodingOptions::ForceUTF8))
        return TEXT("ERREUR : impossible d'ecrire _exec.py");

    // Chemins normalises (forward slashes pour Python)
    FString ExecPath   = ExecFile;   ExecPath  .ReplaceInline(TEXT("\\"), TEXT("/"));
    FString OutputPath = OutputFile; OutputPath.ReplaceInline(TEXT("\\"), TEXT("/"));

    // 2. Wrapper qui capture stdout/stderr dans _output.txt
    // Fix 2026-08-10 (boucle generation -> validation fermee) : jusqu'ici ce wrapper
    // exec()-utait le code renvoye par le LLM directement, sans aucune validation —
    // un code qui ne levait pas d'exception Python passait pour "reussi" meme s'il
    // cassait un Blueprint ou une regression testee ailleurs dans le projet. On route
    // desormais TOUJOURS l'execution via ue5_utils.execute_validated(), qui fait
    // tourner test_suite.run_all() avant/apres et renvoie un verdict explicite
    // (OK / ECHEC / REGRESSION DETECTEE) au lieu d'un simple stdout brut — voir
    // ue5_utils.py pour le detail. Repli sur l'ancien comportement (exec direct) si
    // ue5_utils est introuvable, pour ne jamais bloquer silencieusement un projet qui
    // ne l'aurait pas (ImportError capturee explicitement, pas un except Exception
    // large qui masquerait aussi de vraies erreurs de execute_validated elle-meme).
    FString Wrapper = FString::Printf(
        TEXT("import sys, io, traceback\n")
        TEXT("_so, _se = sys.stdout, sys.stderr\n")
        TEXT("_buf = io.StringIO()\n")
        TEXT("sys.stdout = sys.stderr = _buf\n")
        TEXT("try:\n")
        TEXT("    with open(r'%s', encoding='utf-8') as _f:\n")
        TEXT("        _code = _f.read()\n")
        TEXT("    try:\n")
        TEXT("        from ue5_utils import execute_validated\n")
        TEXT("    except ImportError:\n")
        TEXT("        print('[ExecutePython] ue5_utils.execute_validated introuvable, '\n")
        TEXT("              'repli sur exec() direct SANS validation.')\n")
        TEXT("        exec(_code, globals())\n")
        TEXT("    else:\n")
        TEXT("        print(execute_validated(_code, source='claude_panel'))\n")
        TEXT("except Exception:\n")
        TEXT("    traceback.print_exc()\n")
        TEXT("finally:\n")
        TEXT("    sys.stdout, sys.stderr = _so, _se\n")
        TEXT("_r = _buf.getvalue().strip()\n")
        TEXT("open(r'%s', 'w', encoding='utf-8').write(_r if _r else 'OK')\n"),
        *ExecPath, *OutputPath
    );

    if (!FFileHelper::SaveStringToFile(Wrapper, *WrapFile, FFileHelper::EEncodingOptions::ForceUTF8))
        return TEXT("ERREUR : impossible d'ecrire _wrap.py");

    // Fix 2026-07-21 (audit RoomGenerator) : l'ancien code relisait _output.txt sans jamais
    // verifier qu'il avait bien ete reecrit par CE run. Si "py" echoue silencieusement cote
    // editeur (chemin invalide, plugin Python desactive, etc.), _output.txt contenait le
    // resultat du run PRECEDENT et le panneau affichait un faux succes — meme famille de piege
    // que les "screenshots stale" documentes dans CLAUDE.md. On supprime l'ancienne sortie AVANT
    // d'executer : le wrapper (ci-dessus) ecrit TOUJOURS _output.txt en sortie normale (buffer ou
    // "OK"), donc son absence apres l'appel signale sans ambiguite que "py" n'a meme pas reussi a
    // lancer le wrapper.
    IFileManager::Get().Delete(*OutputFile, /*RequireExists=*/false, /*EvenReadOnly=*/true);

    // 3. Executer le wrapper
    FString WrapPath = WrapFile;
    WrapPath.ReplaceInline(TEXT("\\"), TEXT("/"));
    GEditor->Exec(GEditor->GetEditorWorldContext().World(),
                  *FString::Printf(TEXT("py \"%s\""), *WrapPath));

    // 4. Lire la sortie capturee
    if (!IFileManager::Get().FileExists(*OutputFile))
    {
        return TEXT("ERREUR : _output.txt non regenere par ce run — la commande 'py' a "
                     "probablement echoue silencieusement cote editeur (chemin invalide, plugin "
                     "Python desactive, etc.). Ne pas faire confiance a un ancien resultat : "
                     "verifier Output Log / Saved/Logs manuellement.");
    }

    FString Output;
    if (FFileHelper::LoadFileToString(Output, *OutputFile))
    {
        Output = Output.TrimStartAndEnd();
        // Tronquer si trop long (eviter de saturer le contexte LLM).
        // Fix 2026-08-10 (trouve en validant la boucle generation->validation) :
        // Left(2000) coupait la FIN de la sortie, c'est-a-dire exactement la ligne
        // "--- VERDICT : ... ---" qu'execute_validated() place a la fin de son
        // rapport — un rapport long (beaucoup de print() dans le code du LLM)
        // pouvait donc perdre le verdict au moment ou il compte le plus. On garde
        // desormais la FIN (Right) plutot que le debut.
        if (Output.Len() > 2000)
            Output = TEXT("[...debut tronque...]\n") + Output.Right(2000);
        return Output;
    }

    return TEXT("OK (pas de sortie capturee — verifier Output Log)");
}
