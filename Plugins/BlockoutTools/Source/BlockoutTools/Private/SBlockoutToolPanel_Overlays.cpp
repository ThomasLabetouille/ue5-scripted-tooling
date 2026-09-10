#include "SBlockoutToolPanel.h"
#include "BlockoutGeometrySubsystem.h"
#include "Editor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "LevelEditorViewport.h"

#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
// Resolution automatique du personnage du projet hote (ResolveProjectCharacterClass) --
// ce plugin n'a aucun chemin de Blueprint code en dur, il interroge le projet.
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "GameMapsSettings.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "CollisionShape.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/UObjectGlobals.h"

#include "AssetRegistry/AssetData.h"
#include "PropertyCustomizationHelpers.h"   // SObjectPropertyEntryBox (module PropertyEditor)
#include "ScopedTransaction.h"              // FScopedTransaction (undo Ctrl+Z du Swap)

// Generateur depuis un plan 2D -- lecture d'une Texture2D (FTextureSource, editeur)
// ou d'un PNG sur disque (IImageWrapper).
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"   // persistance des champs (GEditorPerProjectIni)

#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "BlockoutToolPanel"

// SBlockoutToolPanel_Overlays.cpp -- les 3 overlays temps reel (metriques gameplay,
// FOV/frustum, verificateur de pente + hauteur libre) + le ticker qui les pilote.
// Issu du decoupage de SBlockoutToolPanel.cpp (2026-07-30, session 21).

void SBlockoutToolPanel::DrawGameplayOverlay()
{
    UWorld* World = GetDrawTargetWorld();
    if (!World)
    {
        return;
    }

    // ── Portee des ennemis : cercles au sol autour de chaque acteur portant le tag ──
    // Le tag est SAISISSABLE (defaut "Enemy") : ce plugin ne connait pas les conventions
    // du projet hote. On lit ensuite DetectRadius/AttackRadius par reflexion, donc sans
    // rien savoir de la classe C++/Blueprint concernee -- si l'acteur n'a pas ces
    // proprietes, il est simplement ignore, jamais d'erreur.
    FString EnemyTag = TEXT("Enemy");
    if (MetricsEnemyTagBox.IsValid())
    {
        FString Typed = MetricsEnemyTagBox->GetText().ToString();
        Typed.TrimStartAndEndInline();
        if (!Typed.IsEmpty())
        {
            EnemyTag = Typed;
        }
    }

    TArray<AActor*> Enemies;
    UGameplayStatics::GetAllActorsWithTag(World, FName(*EnemyTag), Enemies);
    int32 EnemiesDrawn = 0;
    for (AActor* Enemy : Enemies)
    {
        if (!Enemy)
        {
            continue;
        }
        const FVector Loc = Enemy->GetActorLocation() + FVector(0.f, 0.f, 5.f);
        float DetectRadius = 0.f;
        float AttackRadius = 0.f;
        const bool bHasDetect = GetFloatProperty(Enemy, TEXT("DetectRadius"), DetectRadius);
        const bool bHasAttack = GetFloatProperty(Enemy, TEXT("AttackRadius"), AttackRadius);

        if (bHasDetect && DetectRadius > 0.f)
        {
            DrawDebugCircle(World, Loc, DetectRadius, 48, FColor::Yellow, false, -1.f, 0, 3.f,
                FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
        }
        if (bHasAttack && AttackRadius > 0.f)
        {
            DrawDebugCircle(World, Loc, AttackRadius, 48, FColor::Red, false, -1.f, 0, 3.f,
                FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
        }
        if (bHasDetect || bHasAttack)
        {
            ++EnemiesDrawn;
        }
    }

    // ── Hauteur de saut max : ligne verticale a la position du joueur (PIE) ──
    // ou du PlayerStart (hors PIE).
    FString JumpSource;
    const float JumpHeight = ComputeMaxJumpHeight(JumpSource);

    FVector RefLoc = FVector::ZeroVector;
    bool bHaveRef = false;
    if (World->IsGameWorld())
    {
        if (ACharacter* PlayerChar = UGameplayStatics::GetPlayerCharacter(World, 0))
        {
            RefLoc = PlayerChar->GetActorLocation();
            bHaveRef = true;
        }
    }
    if (!bHaveRef)
    {
        if (APlayerStart* PS = Cast<APlayerStart>(UGameplayStatics::GetActorOfClass(World, APlayerStart::StaticClass())))
        {
            RefLoc = PS->GetActorLocation();
            bHaveRef = true;
        }
    }

    if (bHaveRef && JumpHeight > 0.f)
    {
        const FVector Bottom = RefLoc;
        const FVector Top = RefLoc + FVector(0.f, 0.f, JumpHeight);
        DrawDebugLine(World, Bottom, Top, FColor::Green, false, -1.f, 0, 4.f);
        DrawDebugCircle(World, Top, 40.f, 24, FColor::Green, false, -1.f, 0, 3.f,
            FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
    }

    if (MetricsInfoLabel.IsValid())
    {
        MetricsInfoLabel->SetText(FText::FromString(FString::Printf(
            TEXT("Saut max : %.0f UU (%s)  |  Ennemis affiches : %d"),
            JumpHeight, *JumpSource, EnemiesDrawn)));
    }
}

// ── Simulateur FOV / Frustum joueur ──
// Position/Yaw/Pitch = point de vue simule. FOV = celui du vrai joueur (auto-detecte
// via GetPlayerCameraFOV) sauf si le champ "FOV manuel" est rempli. Distance/Aspect
// controlent juste la taille du wireframe dessine, purement visuel -- aucune geometrie
// n'est spawnee (contrairement aux 6 generateurs de blockout ci-dessus), donc pas
// d'entree d'historique Undo/Selectionner pour cet outil.
void SBlockoutToolPanel::DrawFovFrustumOverlay()
{
    UWorld* World = GetDrawTargetWorld();
    if (!World)
    {
        return;
    }

    const float PosX = ParseFloat(FovPosXBox, 0.f);
    const float PosY = ParseFloat(FovPosYBox, 0.f);
    const float PosZ = ParseFloat(FovPosZBox, 0.f);
    const float Yaw   = ParseFloat(FovYawBox, 0.f);
    const float Pitch = ParseFloat(FovPitchBox, 0.f);
    const float Distance = FMath::Max(10.f, ParseFloat(FovDistanceBox, 1500.f));
    const float Aspect   = FMath::Max(0.1f, ParseFloat(FovAspectBox, 1.7778f));

    FString FovSource;
    float FOV = GetPlayerCameraFOV(FovSource);
    if (FovOverrideBox.IsValid())
    {
        FString OverrideText = FovOverrideBox->GetText().ToString();
        OverrideText.TrimStartAndEndInline();
        if (!OverrideText.IsEmpty())
        {
            FOV = FCString::Atof(*OverrideText);
            FovSource = TEXT("valeur saisie manuellement");
        }
    }
    FOV = FMath::Clamp(FOV, 1.f, 170.f);

    const FVector Apex(PosX, PosY, PosZ);
    const FQuat ViewQuat = FRotator(Pitch, Yaw, 0.f).Quaternion();
    const FVector Fwd   = ViewQuat.GetForwardVector();
    const FVector Right = ViewQuat.GetRightVector();
    const FVector Up    = ViewQuat.GetUpVector();

    // FieldOfView de UCameraComponent est le FOV HORIZONTAL par defaut (AspectRatioAxisConstraint
    // = AspectRatio_MaintainYFOV la plupart du temps) -- le FOV vertical est derive de l'aspect
    // ratio par la formule standard tan(V/2) = tan(H/2) / Aspect. Non revalide contre le reglage
    // reel du personnage du projet (AspectRatioAxisConstraint non lu ici) -- approximation, a confirmer
    // visuellement si le cadrage du frustum semble decale par rapport a la camera reelle en jeu.
    const float HalfHFovRad = FMath::DegreesToRadians(FOV / 2.f);
    const float HalfVFovRad = FMath::Atan(FMath::Tan(HalfHFovRad) / Aspect);

    const FVector FarCenter = Apex + Fwd * Distance;
    const float HalfWidth  = Distance * FMath::Tan(HalfHFovRad);
    const float HalfHeight = Distance * FMath::Tan(HalfVFovRad);

    const FVector Corners[4] = {
        FarCenter + Right * HalfWidth + Up * HalfHeight, // haut-droite
        FarCenter - Right * HalfWidth + Up * HalfHeight, // haut-gauche
        FarCenter - Right * HalfWidth - Up * HalfHeight, // bas-gauche
        FarCenter + Right * HalfWidth - Up * HalfHeight, // bas-droite
    };

    constexpr float Thickness = 2.f;
    for (int32 i = 0; i < 4; ++i)
    {
        DrawDebugLine(World, Apex, Corners[i], FColor::Cyan, false, -1.f, 0, Thickness);
        DrawDebugLine(World, Corners[i], Corners[(i + 1) % 4], FColor::Cyan, false, -1.f, 0, Thickness);
    }
    DrawDebugPoint(World, Apex, 12.f, FColor::Cyan, false, -1.f, 0);

    if (FovInfoLabel.IsValid())
    {
        FovInfoLabel->SetText(FText::FromString(FString::Printf(
            TEXT("FOV %.0f° (%s) — distance %.0f UU, aspect %.2f"), FOV, *FovSource, Distance, Aspect)));
    }
}

// ── Verificateur de pente automatique ──
// Retour Thomas (session 8) : "je ne comprends pas a quoi ca sert de pouvoir rentrer
// des coordonnees... je veux seulement que lorsque je coche la case on puisse voir si
// le personnage peut marcher dessus ou pas." -- PLUS AUCUN champ de zone : la grille
// suit desormais automatiquement la camera du viewport editeur, recalculee CHAQUE
// frame (centree sur sa position XY, taille/resolution/hauteurs de trace fixes en
// constantes ci-dessous, plus exposees en UI). Colore chaque point touche selon
// l'angle entre la normale de la surface et la verticale :
//   Vert   <= angle marchable REEL du joueur (GetPlayerStepMetrics(), meme repli
//             PIE -> CDO du personnage du projet -> defaut moteur que le reste du panneau)
//   Orange <= seuil "vehicule" configurable (pas de systeme vehicule reel dans ce
//             projet -- second seuil purement designer, a ajuster a la main)
//   Rouge  au-dela du seuil vehicule
void SBlockoutToolPanel::DrawSlopeCheckOverlay()
{
    UWorld* World = GetDrawTargetWorld();
    if (!World || !GCurrentLevelEditingViewportClient)
    {
        return;
    }

    constexpr float ScanSize = 2000.f;          // 20m x 20m autour de la camera
    // Optimisation (session 11, retour Thomas "comment rendre ca le moins couteux
    // possible") : espacement remonte de 100 a 150 UU -- moins de sondes par recalcul
    // (14x14=196 au lieu de 21x21=441, ~55% de moins) pour une perte de resolution
    // negligeable a l'usage (une capsule joueur fait ~34 UU de rayon).
    constexpr float Spacing = 150.f;
    // FIX session 9 (retour Thomas -- verificateur "au-dessus de la grotte" au lieu de
    // dedans) : demarrer la trace tres au-dessus de la camera (+2000 avant) transperce
    // le plafond rocheux d'une grotte -- une trace/sonde a UN SEUL impact s'arrete au
    // PREMIER blocage rencontre, donc elle touche le sol EXTERIEUR au-dessus de la
    // grotte et ne descend jamais dedans. La camera est deja physiquement DANS la
    // grotte quand Thomas l'y deplace -- demarrer juste au-dessus d'elle (marge
    // modeste, pas 2000) suffit et evite de transpercer un plafond au-dessus.
    constexpr float TraceUpFromCam = 150.f;
    constexpr float TraceDownFromCam = 5000.f;
    constexpr int32 MaxSamplesPerAxis = 60;     // garde-fou perf
    // FIX session 9 (retour Thomas -- "bruit" == plans exactement au niveau du sol) :
    // le decalage tente en session 8 etait un NO-OP -- le FPlane etait construit a
    // partir de Hit.ImpactPoint (non decale), donc DrawDebugSolidPlane recalait le quad
    // pile sur la surface reelle independamment du Loc passe. Le decalage doit etre
    // applique AU POINT QUI DEFINIT LE PLAN lui-meme, pas seulement au Loc de dessin.
    constexpr float QuadOffset = 6.f;
    // Optimisation (session 11) : la partie COUTEUSE de cet overlay, ce sont les ~200
    // sondes physiques par recalcul (SweepSingleByObjectType), pas le dessin des quads
    // qui en resultent. Tant que la camera n'a pas bouge de plus de ce seuil depuis le
    // dernier recalcul, on saute la sonde ENTIEREMENT et on se contente de redessiner
    // les FSlopeSample deja en cache (juste des DrawDebugSolidPlane, quasi gratuit) --
    // couvre le cas frequent ou le designer regarde le resultat sans deplacer la camera.
    constexpr float RecomputeMoveThreshold = 50.f;

    const FVector CamLoc = GCurrentLevelEditingViewportClient->GetViewLocation();

    const int32 Count = FMath::Clamp(FMath::RoundToInt(ScanSize / Spacing), 1, MaxSamplesPerAxis);
    const float CellSpacing = ScanSize / Count;
    const float CellHalfSize = CellSpacing / 2.f; // pas de surplomb (voir historique bruit, session 8-10)

    // FIX session 16 (retour Thomas : "il ne se passe rien quand je modifie la valeur de
    // l'angle max") -- vrai bug introduit par le camera-gating de la session 11 : le
    // seuil n'etait relu QUE dans la branche de recalcul, donc modifier le champ ne
    // produisait aucun effet tant que la camera ne bougeait pas de 50 UU. On declenche
    // desormais un recalcul aussi quand la valeur change, et au bout de ~2s dans tous
    // les cas (pour refleter une geometrie modifiee entre-temps : nouvelle salle
    // generee, acteur deplace... que le gating masquait de la meme facon).
    // Seuil VERT -> ORANGE : valeur saisie si le champ est rempli, sinon l'angle
    // marchable reel du personnage. Champ vide => -1 => repli automatique.
    const float WalkableOverride = ParseFloat(SlopeWalkableAngleBox, -1.f);

    constexpr int32 ForceRecomputeEveryNTicks = 13;   // ~2s a 0.15s par tick
    ++SlopeTicksSinceRecompute;

    const bool bNeedsRecompute = !bSlopePrevLocValid
        || !FMath::IsNearlyEqual(WalkableOverride, SlopeLastWalkableAngle)
        || SlopeTicksSinceRecompute >= ForceRecomputeEveryNTicks
        || FVector::DistSquared(CamLoc, SlopePrevCameraLoc) > FMath::Square(RecomputeMoveThreshold);

    if (bNeedsRecompute)
    {
        SlopeTicksSinceRecompute = 0;
        SlopeLastWalkableAngle = WalkableOverride;

        float WalkableAngle = 44.76f;
        float CapsuleRadius = 34.f;
        {
            float MaxStepHeight;
            FString MetricsSource;
            GetPlayerStepMetrics(CapsuleRadius, MaxStepHeight, WalkableAngle, MetricsSource);
        }
        const float PlayerWalkableAngle = WalkableAngle;
        const bool bWalkableFromPlayer = (WalkableOverride <= 0.f);
        if (!bWalkableFromPlayer)
        {
            WalkableAngle = WalkableOverride;
        }
        // FIX session 7 (coverage manquante) : trace sur les OBJECT TYPES {WorldStatic,
        // WorldDynamic} plutot qu'un canal unique (ECC_Visibility) -- meme piege deja
        // documente pour SphereOverlapActors dans ce projet -- + sonde SPHERE (pas une
        // ligne infiniment fine) rayon = vrai rayon de capsule joueur, plus robuste sur
        // les faces inclinees et les micro-reliefs.
        const float ProbeRadius = FMath::Clamp(CapsuleRadius, 10.f, CellSpacing);

        const FVector Origin = CamLoc - FVector(ScanSize / 2.f, ScanSize / 2.f, 0.f);

        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SlopeCheckOverlay), false);
        FCollisionObjectQueryParams ObjectParams;
        ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
        const FCollisionShape ProbeShape = FCollisionShape::MakeSphere(ProbeRadius);

        SlopeCachedSamples.Reset();
        int32 GreenCount = 0, RedCount = 0;
        for (int32 ix = 0; ix <= Count; ++ix)
        {
            for (int32 iy = 0; iy <= Count; ++iy)
            {
                const float SampleX = Origin.X + ix * CellSpacing;
                const float SampleY = Origin.Y + iy * CellSpacing;
                const FVector TraceStart(SampleX, SampleY, CamLoc.Z + TraceUpFromCam);
                const FVector TraceEnd(SampleX, SampleY, CamLoc.Z - TraceDownFromCam);

                FHitResult Hit;
                if (!World->SweepSingleByObjectType(Hit, TraceStart, TraceEnd, FQuat::Identity,
                        ObjectParams, ProbeShape, QueryParams))
                {
                    continue;
                }

                const float SlopeAngleDeg = FMath::RadiansToDegrees(FMath::Acos(
                    FMath::Clamp(FVector::DotProduct(Hit.ImpactNormal, FVector::UpVector), -1.f, 1.f)));

                FSlopeSample Sample;
                Sample.Normal = Hit.ImpactNormal;
                // Le point qui DEFINIT le plan (pas seulement le Loc de dessin) doit etre
                // decale -- sinon DrawDebugSolidPlane redessine exactement sur la surface
                // reelle quel que soit le Loc fourni (bug identifie par Thomas : "les
                // planes... sont exactement au meme niveau que le sol").
                Sample.DrawCenter = Hit.ImpactPoint + Hit.ImpactNormal * QuadOffset;

                // 2 couleurs depuis la session 18 : le seuil "vehicule" a ete supprime
                // (aucun systeme de vehicule dans ce projet -- il ne servait a rien).
                if (SlopeAngleDeg <= WalkableAngle)
                {
                    Sample.Color = FColor::Green;
                    ++GreenCount;
                }
                else
                {
                    Sample.Color = FColor::Red;
                    ++RedCount;
                }
                SlopeCachedSamples.Add(Sample);
            }
        }

        SlopePrevCameraLoc = CamLoc;
        bSlopePrevLocValid = true;

        if (SlopeInfoLabel.IsValid())
        {
            SlopeInfoLabel->SetText(FText::FromString(FString::Printf(
                TEXT("%d pt — Vert %d / Rouge %d — seuil %.1f° (%s)   [joueur : %.1f°]"),
                SlopeCachedSamples.Num(), GreenCount, RedCount, WalkableAngle,
                bWalkableFromPlayer ? TEXT("auto") : TEXT("manuel"), PlayerWalkableAngle)));
        }
    }

    // Toujours redessiner les echantillons en cache (camera bougee ou non) -- LifeTime
    // explicite (pas -1) puisque cet overlay n'est redessine que toutes les ~0.15s (voir
    // TickOverlay), chaque quad doit rester visible plus longtemps que l'intervalle pour
    // eviter un scintillement entre deux passages.
    for (const FSlopeSample& Sample : SlopeCachedSamples)
    {
        const FPlane SurfacePlane(Sample.DrawCenter, Sample.Normal);
        DrawDebugSolidPlane(World, SurfacePlane, Sample.DrawCenter,
            FVector2D(CellHalfSize, CellHalfSize), Sample.Color, false, 0.3f, 0);
    }
}

// Optimisation (session 11, retour Thomas "comment rendre ca le moins couteux possible,
// et est-ce possible pour les autres outils aussi") : les 3 overlays temps reel de ce
// panneau (Metriques Gameplay, Simulateur FOV, Verificateur de pente) partagent
// desormais le MEME pattern de throttle -- chacun a son propre accumulateur de temps et
// ne se redessine que toutes les ~0.1-0.15s au lieu de chaque frame (60 fps), plutot que
// de tourner sans limite. Les 8 generateurs de blockout (Salle -> Tunnel courbe) n'ont
// PAS besoin de throttle : ce sont des actions ponctuelles (un clic = un spawn), aucun
// cout recurrent -- rien a optimiser de ce cote-la.
// ── Verificateur de hauteur libre ──
// Pendant du verificateur de pente : pour chaque point de la grille, sonde le SOL vers
// le bas, puis le PLAFOND vers le haut depuis ce sol. Colore en ROUGE la ou la hauteur
// libre est insuffisante pour la capsule du joueur (couloir trop bas, passage sous une
// arche, dalle posee trop pres du sol...) -- un classique du blockout que rien ne
// detectait jusqu'ici et qui ne se voit pas en vue de dessus.
// Meme architecture que DrawSlopeCheckOverlay (camera-gating + cache + throttle) :
// la sonde physique est la partie couteuse, pas le dessin.
void SBlockoutToolPanel::DrawClearanceOverlay()
{
    UWorld* World = GetDrawTargetWorld();
    if (!World || !GCurrentLevelEditingViewportClient)
    {
        return;
    }

    constexpr float ScanSize = 2000.f;
    constexpr float Spacing = 150.f;
    constexpr float TraceUpFromCam = 150.f;      // meme logique "grotte" que la pente
    constexpr float TraceDownFromCam = 5000.f;
    constexpr float CeilingSearchHeight = 2000.f; // hauteur max cherchee au-dessus du sol
    constexpr int32 MaxSamplesPerAxis = 60;
    constexpr float QuadOffset = 6.f;
    constexpr float RecomputeMoveThreshold = 50.f;
    constexpr int32 ForceRecomputeEveryNTicks = 13;

    const FVector CamLoc = GCurrentLevelEditingViewportClient->GetViewLocation();
    // Vide => -1 => repli sur la hauteur reelle de la capsule du joueur.
    const float MinOverride = ParseFloat(ClearanceMinBox, -1.f);

    const int32 Count = FMath::Clamp(FMath::RoundToInt(ScanSize / Spacing), 1, MaxSamplesPerAxis);
    const float CellSpacing = ScanSize / Count;
    const float CellHalfSize = CellSpacing / 2.f;

    ++ClearanceTicksSinceRecompute;
    const bool bNeedsRecompute = !bClearancePrevLocValid
        || !FMath::IsNearlyEqual(MinOverride, ClearanceLastMin)
        || ClearanceTicksSinceRecompute >= ForceRecomputeEveryNTicks
        || FVector::DistSquared(CamLoc, ClearancePrevCameraLoc) > FMath::Square(RecomputeMoveThreshold);

    if (bNeedsRecompute)
    {
        ClearanceTicksSinceRecompute = 0;
        ClearanceLastMin = MinOverride;

        FString CapsuleSource;
        const float PlayerHeight = GetPlayerCapsuleFullHeight(CapsuleSource);
        const bool bFromPlayer = (MinOverride <= 0.f);
        const float RequiredHeight = bFromPlayer ? PlayerHeight : MinOverride;

        float CapsuleRadius = 34.f;
        {
            float MaxStepHeight, WalkableAngle;
            FString MetricsSource;
            GetPlayerStepMetrics(CapsuleRadius, MaxStepHeight, WalkableAngle, MetricsSource);
        }
        const float ProbeRadius = FMath::Clamp(CapsuleRadius, 10.f, CellSpacing);

        const FVector Origin = CamLoc - FVector(ScanSize / 2.f, ScanSize / 2.f, 0.f);

        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ClearanceOverlay), false);
        FCollisionObjectQueryParams ObjectParams;
        ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
        const FCollisionShape ProbeShape = FCollisionShape::MakeSphere(ProbeRadius);

        ClearanceCachedSamples.Reset();
        int32 OkCount = 0, TooLowCount = 0;
        for (int32 ix = 0; ix <= Count; ++ix)
        {
            for (int32 iy = 0; iy <= Count; ++iy)
            {
                const float SampleX = Origin.X + ix * CellSpacing;
                const float SampleY = Origin.Y + iy * CellSpacing;

                // 1. Trouver le sol.
                FHitResult Floor;
                if (!World->SweepSingleByObjectType(Floor,
                        FVector(SampleX, SampleY, CamLoc.Z + TraceUpFromCam),
                        FVector(SampleX, SampleY, CamLoc.Z - TraceDownFromCam),
                        FQuat::Identity, ObjectParams, ProbeShape, QueryParams))
                {
                    continue;
                }

                // 2. Chercher un plafond au-dessus de ce sol. Depart legerement au-dessus
                //    du sol pour ne pas re-toucher immediatement la surface qu'on vient
                //    de trouver (la sonde a un volume : partir pile dessus la ferait
                //    demarrer en intersection).
                const FVector CeilStart = Floor.ImpactPoint + FVector(0.f, 0.f, ProbeRadius + 2.f);
                const FVector CeilEnd = Floor.ImpactPoint + FVector(0.f, 0.f, CeilingSearchHeight);

                float Clearance = CeilingSearchHeight;   // rien au-dessus = ciel ouvert
                FHitResult Ceiling;
                if (World->SweepSingleByObjectType(Ceiling, CeilStart, CeilEnd, FQuat::Identity,
                        ObjectParams, ProbeShape, QueryParams))
                {
                    Clearance = Ceiling.ImpactPoint.Z - Floor.ImpactPoint.Z;
                }

                FSlopeSample Sample;
                Sample.Normal = Floor.ImpactNormal;
                Sample.DrawCenter = Floor.ImpactPoint + Floor.ImpactNormal * QuadOffset;
                if (Clearance >= RequiredHeight)
                {
                    Sample.Color = FColor::Green;
                    ++OkCount;
                }
                else
                {
                    Sample.Color = FColor::Red;
                    ++TooLowCount;
                }
                ClearanceCachedSamples.Add(Sample);
            }
        }

        ClearancePrevCameraLoc = CamLoc;
        bClearancePrevLocValid = true;

        if (ClearanceInfoLabel.IsValid())
        {
            ClearanceInfoLabel->SetText(FText::FromString(FString::Printf(
                TEXT("%d pt — OK %d / Trop bas %d — seuil %.0f UU (%s)   [capsule joueur : %.0f UU]"),
                ClearanceCachedSamples.Num(), OkCount, TooLowCount, RequiredHeight,
                bFromPlayer ? TEXT("auto") : TEXT("manuel"), PlayerHeight)));
        }
    }

    for (const FSlopeSample& Sample : ClearanceCachedSamples)
    {
        const FPlane SurfacePlane(Sample.DrawCenter, Sample.Normal);
        DrawDebugSolidPlane(World, SurfacePlane, Sample.DrawCenter,
            FVector2D(CellHalfSize, CellHalfSize), Sample.Color, false, 0.3f, 0);
    }
}

FText SBlockoutToolPanel::GetClearanceSectionTitle() const
{
    const bool bActive = ClearanceShowCheck.IsValid() && ClearanceShowCheck->IsChecked();
    return bActive
        ? LOCTEXT("ClearanceSectionOn",  "Vérificateur de hauteur libre   ● actif")
        : LOCTEXT("ClearanceSectionOff", "Vérificateur de hauteur libre");
}

bool SBlockoutToolPanel::TickOverlay(float DeltaTime)
{
    if (OverlayEnabledCheck.IsValid() && OverlayEnabledCheck->IsChecked())
    {
        // Deja peu couteux (quelques DrawDebugCircle/Line par ennemi) -- throttle par
        // coherence/marge de securite, pas parce que c'etait mesure comme un probleme.
        constexpr float GameplayOverlayInterval = 0.1f;
        GameplayOverlayAccumTime += DeltaTime;
        if (GameplayOverlayAccumTime >= GameplayOverlayInterval)
        {
            GameplayOverlayAccumTime = 0.f;
            DrawGameplayOverlay();
        }
    }
    else
    {
        GameplayOverlayAccumTime = 0.f;
    }

    if (FovShowFrustumCheck.IsValid() && FovShowFrustumCheck->IsChecked())
    {
        // Deja tres peu couteux (9 DrawDebug, aucune trace physique) -- meme raison.
        constexpr float FovOverlayInterval = 0.1f;
        FovOverlayAccumTime += DeltaTime;
        if (FovOverlayAccumTime >= FovOverlayInterval)
        {
            FovOverlayAccumTime = 0.f;
            DrawFovFrustumOverlay();
        }
    }
    else
    {
        FovOverlayAccumTime = 0.f;
    }

    if (SlopeShowCheck.IsValid() && SlopeShowCheck->IsChecked())
    {
        // Note : le compteur SlopeTicksSinceRecompute (force un recalcul periodique) est
        // incremente dans DrawSlopeCheckOverlay lui-meme, pas ici -- il compte les
        // passages effectifs de l'overlay, pas les frames.
        // Throttle (session 10, suite au crash D3D12 "descriptor heap" signale par
        // Thomas a la fermeture de l'editeur) : cet overlay dessine le plus de quads
        // translucides des 3 -- redessine seulement toutes les SlopeOverlayInterval
        // secondes plutot qu'a chaque frame. Le vrai gain de session 11 est DANS
        // DrawSlopeCheckOverlay() (camera-gating : saute la sonde physique entierement
        // si la camera n'a pas bouge), ce throttle reste une couche de securite en plus.
        constexpr float SlopeOverlayInterval = 0.15f;
        SlopeOverlayAccumTime += DeltaTime;
        if (SlopeOverlayAccumTime >= SlopeOverlayInterval)
        {
            SlopeOverlayAccumTime = 0.f;
            DrawSlopeCheckOverlay();
        }
    }
    else
    {
        SlopeOverlayAccumTime = 0.f;
    }

    if (ClearanceShowCheck.IsValid() && ClearanceShowCheck->IsChecked())
    {
        constexpr float ClearanceOverlayInterval = 0.15f;
        ClearanceOverlayAccumTime += DeltaTime;
        if (ClearanceOverlayAccumTime >= ClearanceOverlayInterval)
        {
            ClearanceOverlayAccumTime = 0.f;
            DrawClearanceOverlay();
        }
    }
    else
    {
        ClearanceOverlayAccumTime = 0.f;
    }
    return true; // continuer a ticker indefiniment
}

// ---------------------------------------------------------------------------
// Construct
// ---------------------------------------------------------------------------


#undef LOCTEXT_NAMESPACE
