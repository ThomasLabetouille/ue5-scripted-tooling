// ============================================================================
// Verification par PROPRIETES du noyau geometrique des panneaux, HORS MOTEUR.
//
// Portage de Tools/GeometryTests (projet Unity LevelDesignTools). Compile LE
// FICHIER DU PROJET (BlockoutPanelGeometry.cpp), pas une copie.
//
// Ce qui est verifie n'est pas une liste de resultats figes mais des proprietes
// qui doivent tenir pour n'importe quelle entree. Les fonctions de MESURE sont
// reecrites ici a partir de leur definition mathematique : mesurer une aire avec
// la fonction du code teste ne testerait rien.
// ============================================================================
#include "BlockoutPanelGeometry.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

namespace
{
    int gFailures = 0;
    int gChecks = 0;

    void Report(const char* Name, bool bOk, const std::string& Detail = "")
    {
        ++gChecks;
        if (!bOk) ++gFailures;
        std::printf("%s %-58s %s\n", bOk ? "[OK]  " : "[FAIL]", Name, Detail.c_str());
    }

    using Loop = TArray<FVector2D>;

    // ── Mesures independantes du code teste ─────────────────────────────────
    double Shoelace(const Loop& P)
    {
        const int32 N = P.Num();
        if (N < 3) return 0.0;
        double S = 0.0;
        for (int32 i = 0; i < N; ++i)
        {
            const FVector2D& A = P[i];
            const FVector2D& B = P[(i + 1) % N];
            S += A.X * B.Y - B.X * A.Y;
        }
        return S * 0.5;
    }

    double TriArea(const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        return std::fabs((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X)) * 0.5;
    }

    bool PointInTri(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        const double D1 = (P.X - B.X) * (A.Y - B.Y) - (A.X - B.X) * (P.Y - B.Y);
        const double D2 = (P.X - C.X) * (B.Y - C.Y) - (B.X - C.X) * (P.Y - C.Y);
        const double D3 = (P.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (P.Y - A.Y);
        const bool Neg = (D1 < 0) || (D2 < 0) || (D3 < 0);
        const bool Pos = (D1 > 0) || (D2 > 0) || (D3 > 0);
        return !(Neg && Pos);
    }

    bool PointInLoop(const FVector2D& P, const Loop& L)
    {
        bool In = false;
        const int32 N = L.Num();
        for (int32 i = 0, j = N - 1; i < N; j = i++)
        {
            const FVector2D& Vi = L[i];
            const FVector2D& Vj = L[j];
            if ((Vi.Y > P.Y) != (Vj.Y > P.Y))
            {
                const double X = Vi.X + (P.Y - Vi.Y) / (Vj.Y - Vi.Y) * (Vj.X - Vi.X);
                if (X > P.X) In = !In;
            }
        }
        return In;
    }

    double DistToSeg(const FVector2D& P, const FVector2D& A, const FVector2D& B)
    {
        const double DX = B.X - A.X, DY = B.Y - A.Y;
        const double L2 = DX * DX + DY * DY;
        if (L2 < 1e-18) return FVector2D::Distance(P, A);
        double T = ((P.X - A.X) * DX + (P.Y - A.Y) * DY) / L2;
        T = T < 0.0 ? 0.0 : (T > 1.0 ? 1.0 : T);
        return FVector2D::Distance(P, FVector2D(A.X + T * DX, A.Y + T * DY));
    }

    double DistToLoop(const FVector2D& P, const Loop& L)
    {
        double Best = 1e300;
        const int32 N = L.Num();
        for (int32 i = 0; i < N; ++i) Best = std::min(Best, DistToSeg(P, L[i], L[(i + 1) % N]));
        return Best;
    }

    // ── Generateurs ─────────────────────────────────────────────────────────
    // Contour SIMPLE par construction : un sommet par secteur angulaire, avec un
    // jitter borne a l'interieur du secteur. Les sommets font donc le tour complet du
    // centre, ce qui rend le polygone etoile par rapport a lui -- donc jamais
    // auto-intersectant.
    //
    // La version naive (N angles uniformes dans [0,2PI) puis tri) est FAUSSE et l'a
    // prouve : quand les sommets ne font pas le tour du centre, l'ordre trie n'est pas
    // l'ordre circulaire et la premiere arete traverse tout le plan. 57 cas sur 2000,
    // rapportes a tort comme une erreur d'aire du code teste. C'est exactement le
    // "harnais qui crie au loup" -- d'ou le garde-fou IsGeneratedCaseValid ci-dessous.
    Loop RandomStarPolygon(std::mt19937_64& Rng, double MinR = 150.0, double MaxR = 900.0,
                           int MinPts = 3, int MaxPts = 11, double CX = 0.0, double CY = 0.0)
    {
        std::uniform_int_distribution<int> CountD(MinPts, MaxPts);
        std::uniform_real_distribution<double> JitterD(0.15, 0.85);
        std::uniform_real_distribution<double> RadiusD(MinR, MaxR);

        const int N = CountD(Rng);
        const double Sector = 6.283185307179586 / N;

        Loop P;
        for (int i = 0; i < N; ++i)
        {
            const double A = Sector * (i + JitterD(Rng));
            const double R = RadiusD(Rng);
            P.Add(FVector2D(CX + R * std::cos(A), CY + R * std::sin(A)));
        }
        return P;
    }

    // Test d'auto-intersection ECRIT ICI, sans appeler IsSimplePolygon : un harnais qui
    // valide ses propres cas avec la fonction qu'il teste ne valide rien. Sert a
    // distinguer "mon cas est invalide" de "le code est faux" -- la confusion entre les
    // deux fait abandonner un harnais en trois jours.
    bool SegmentsCross(const FVector2D& P1, const FVector2D& P2, const FVector2D& P3, const FVector2D& P4)
    {
        auto Cross = [](const FVector2D& O, const FVector2D& A, const FVector2D& B)
        { return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X); };

        const double D1 = Cross(P3, P4, P1), D2 = Cross(P3, P4, P2);
        const double D3 = Cross(P1, P2, P3), D4 = Cross(P1, P2, P4);
        return ((D1 > 0 && D2 < 0) || (D1 < 0 && D2 > 0))
            && ((D3 > 0 && D4 < 0) || (D3 < 0 && D4 > 0));
    }

    bool IsGeneratedCaseValid(const Loop& P)
    {
        const int32 N = P.Num();
        if (N < 3) return false;
        for (int32 i = 0; i < N; ++i)
            for (int32 j = i + 1; j < N; ++j)
            {
                if ((j + 1) % N == i || (i + 1) % N == j) continue;
                if (SegmentsCross(P[i], P[(i + 1) % N], P[j], P[(j + 1) % N])) return false;
            }
        return true;
    }

    Loop Rect(double CX, double CY, double HW, double HH)
    {
        Loop P;
        P.Add(FVector2D(CX - HW, CY - HH));
        P.Add(FVector2D(CX + HW, CY - HH));
        P.Add(FVector2D(CX + HW, CY + HH));
        P.Add(FVector2D(CX - HW, CY + HH));
        return P;
    }

    TArray<Loop> GridHoles(std::mt19937_64& Rng, double HalfW, double HalfH, int Count, double Margin = 25.0)
    {
        TArray<Loop> Holes;
        const int Cols = (int)std::ceil(std::sqrt((double)Count));
        const int Rows = (int)std::ceil((double)Count / Cols);
        const double CellW = (2.0 * HalfW - 2.0 * Margin) / Cols;
        const double CellH = (2.0 * HalfH - 2.0 * Margin) / Rows;

        int Made = 0;
        for (int r = 0; r < Rows && Made < Count; ++r)
            for (int c = 0; c < Cols && Made < Count; ++c)
            {
                std::uniform_real_distribution<double> WD(CellW * 0.15, CellW * 0.35);
                std::uniform_real_distribution<double> HD(CellH * 0.15, CellH * 0.35);
                Holes.Add(Rect(-HalfW + Margin + CellW * (c + 0.5),
                               -HalfH + Margin + CellH * (r + 0.5), WD(Rng), HD(Rng)));
                ++Made;
            }
        return Holes;
    }

    // Ecart minimal entre deux ordonnees DISTINCTES du panneau. En dessous de
    // l'Epsilon du triangulateur, deux lignes de tranche fusionnent volontairement et
    // un eclat d'aire disparait : c'est le CONTRAT du code, pas un defaut. Un harnais
    // qui le compte comme un echec crie au loup, et un harnais qui crie au loup est
    // abandonne en trois jours.
    double MinDistinctYGap(const Loop& Outer, const TArray<Loop>& Holes)
    {
        std::vector<double> Ys;
        for (const FVector2D& P : Outer) Ys.push_back(P.Y);
        for (const Loop& H : Holes) for (const FVector2D& P : H) Ys.push_back(P.Y);
        std::sort(Ys.begin(), Ys.end());

        double MinGap = 1e300;
        for (size_t i = 1; i < Ys.size(); ++i)
        {
            const double Gap = Ys[i] - Ys[i - 1];
            if (Gap > 1e-12) MinGap = std::min(MinGap, Gap);
        }
        return MinGap;
    }

    double TriangulatedArea(const TArray<FVector2D>& Tris)
    {
        double Total = 0.0;
        for (int32 i = 0; i + 2 < Tris.Num(); i += 3) Total += TriArea(Tris[i], Tris[i + 1], Tris[i + 2]);
        return Total;
    }
}

// ── Propriete 1 : aire triangulee = aire du contour (sans trou) ─────────────
static void TestAreaNoHoles(int Cases)
{
    std::mt19937_64 Rng(20260825);
    double Worst = 0.0;
    int Empty = 0;

    int Rejected = 0;
    for (int i = 0; i < Cases; ++i)
    {
        const Loop Outer = RandomStarPolygon(Rng);
        if (!IsGeneratedCaseValid(Outer)) { ++Rejected; continue; }   // cas ecarte, pas un echec
        const TArray<FVector2D> Tris = BlockoutPanelGeometry::TriangulateFace(Outer, TArray<Loop>());
        if (Tris.Num() < 3) { ++Empty; continue; }

        const double Expected = std::fabs(Shoelace(Outer));
        const double Rel = std::fabs(TriangulatedArea(Tris) - Expected) / std::max(Expected, 1.0);
        Worst = std::max(Worst, Rel);
    }

    char Buf[160];
    std::snprintf(Buf, sizeof(Buf), "%d contours, pire ecart relatif %.2e, %d vides, %d ecartes",
                  Cases, Worst, Empty, Rejected);
    Report("aire triangulee = aire du contour", Worst <= 1e-9 && Empty == 0 && Rejected == 0, Buf);
}

// ── Propriete 2 : aire = contour moins trous ────────────────────────────────
static void TestAreaWithHoles(int Cases)
{
    std::mt19937_64 Rng(20260826);
    double Worst = 0.0;
    int MaxHoles = 0;
    int Rejected = 0;

    for (int i = 0; i < Cases; ++i)
    {
        std::uniform_real_distribution<double> Dim(300.0, 1500.0);
        std::uniform_int_distribution<int> Count(1, 21);
        const double HalfW = Dim(Rng), HalfH = Dim(Rng);
        const int NHoles = Count(Rng);
        MaxHoles = std::max(MaxHoles, NHoles);

        const Loop Outer = Rect(0, 0, HalfW, HalfH);
        const TArray<Loop> Holes = GridHoles(Rng, HalfW, HalfH, NHoles);

        if (MinDistinctYGap(Outer, Holes) < BlockoutPanelGeometry::Epsilon) { ++Rejected; continue; }

        const TArray<FVector2D> Tris = BlockoutPanelGeometry::TriangulateFace(Outer, Holes);
        double Expected = std::fabs(Shoelace(Outer));
        for (const Loop& H : Holes) Expected -= std::fabs(Shoelace(H));

        const double Rel = std::fabs(TriangulatedArea(Tris) - Expected) / std::max(Expected, 1.0);
        Worst = std::max(Worst, Rel);
    }

    char Buf[160];
    std::snprintf(Buf, sizeof(Buf), "%d panneaux, jusqu'a %d trous, pire ecart %.2e, %d sous tolerance",
                  Cases, MaxHoles, Worst, Rejected);
    Report("aire triangulee = contour moins trous", Worst <= 1e-9, Buf);
}

// ── Propriete 3 : couverture (1 triangle dans la matiere, 0 dans un trou) ───
static void TestCoverage(int Panels, int SamplesPerPanel)
{
    std::mt19937_64 Rng(20260827);
    int Solid = 0, InHole = 0, Skipped = 0;
    bool bOk = true;

    for (int p = 0; p < Panels && bOk; ++p)
    {
        const double HalfW = 900.0, HalfH = 650.0;
        const Loop Outer = Rect(0, 0, HalfW, HalfH);
        const TArray<Loop> Holes = GridHoles(Rng, HalfW, HalfH, 6);
        const TArray<FVector2D> Tris = BlockoutPanelGeometry::TriangulateFace(Outer, Holes);
        if (Tris.Num() < 3) { bOk = false; break; }

        std::uniform_real_distribution<double> XD(-HalfW, HalfW), YD(-HalfH, HalfH);
        for (int s = 0; s < SamplesPerPanel; ++s)
        {
            const FVector2D P(XD(Rng), YD(Rng));

            // Un point pile sur une frontiere n'est pas un bug du code, c'est la
            // limite du test : on l'ecarte plutot que de crier au loup.
            bool NearEdge = DistToLoop(P, Outer) < 1.0;
            for (const Loop& H : Holes) NearEdge = NearEdge || DistToLoop(P, H) < 1.0;
            if (NearEdge) { ++Skipped; continue; }

            bool Hole = false;
            for (const Loop& H : Holes) Hole = Hole || PointInLoop(P, H);

            int Covering = 0;
            for (int32 i = 0; i + 2 < Tris.Num(); i += 3)
                if (PointInTri(P, Tris[i], Tris[i + 1], Tris[i + 2])) ++Covering;

            if (Hole)  { if (Covering != 0) { bOk = false; break; } ++InHole; }
            else       { if (Covering != 1) { bOk = false; break; } ++Solid; }
        }
    }

    char Buf[160];
    std::snprintf(Buf, sizeof(Buf), "%d pts matiere / %d pts trou (%d ecartes)", Solid, InHole, Skipped);
    Report("couverture : 1 triangle dans la matiere, 0 dans un trou", bOk && Solid > 0 && InHole > 0, Buf);
}

// ── Propriete 4 : aucun triangle degenere (seuil RELATIF) ──────────────────
static void TestNoDegenerate(int Cases)
{
    std::mt19937_64 Rng(20260828);
    bool bOk = true;
    int Total = 0;

    for (int i = 0; i < Cases && bOk; ++i)
    {
        const Loop Outer = RandomStarPolygon(Rng);
        const TArray<FVector2D> Tris = BlockoutPanelGeometry::TriangulateFace(Outer, TArray<Loop>());

        double MinX = 1e300, MaxX = -1e300, MinY = 1e300, MaxY = -1e300;
        for (const FVector2D& P : Outer)
        {
            MinX = std::min(MinX, P.X); MaxX = std::max(MaxX, P.X);
            MinY = std::min(MinY, P.Y); MaxY = std::max(MaxY, P.Y);
        }
        // Contrat du code : il ecarte un triangle quand |2 * Aire| <= bbox * 1e-9,
        // donc tout triangle EMIS a une aire > bbox * 5e-10. Mesurer avec un seuil plus
        // strict que le contrat, c'est reprocher au code de faire ce qu'il annonce --
        // mon 1e-8 initial rejetait 1 triangle sur 329 289, tous conformes.
        const double Floor = std::max((MaxX - MinX) * (MaxY - MinY), 1e-12) * 5e-10;

        for (int32 t = 0; t + 2 < Tris.Num(); t += 3)
        {
            ++Total;
            if (TriArea(Tris[t], Tris[t + 1], Tris[t + 2]) <= Floor) { bOk = false; break; }
        }
    }

    char Buf[160];
    std::snprintf(Buf, sizeof(Buf), "%d triangles inspectes sur %d contours", Total, Cases);
    Report("aucun triangle degenere (seuil relatif)", bOk, Buf);
}

// ── Propriete 5 : le maillage 3D est un prisme recto-verso d'epaisseur exacte
static void TestPrism(int Cases)
{
    std::mt19937_64 Rng(20260829);
    bool bOk = true;
    std::string Why;

    for (int i = 0; i < Cases && bOk; ++i)
    {
        std::uniform_real_distribution<double> TD(5.0, 80.0);
        const Loop Outer = RandomStarPolygon(Rng);
        const double Thickness = TD(Rng);

        FBlockoutPanelMesh Mesh;
        if (!BlockoutPanelGeometry::BuildPanelMesh(Outer, Thickness, 0, 1, 2, TArray<Loop>(), Mesh))
        { bOk = false; Why = "BuildPanelMesh a echoue"; break; }

        if (Mesh.Vertices.Num() != Mesh.Triangles.Num()) { bOk = false; Why = "sommets partages inattendus"; break; }
        if (Mesh.UVs.Num() != Mesh.Vertices.Num())       { bOk = false; Why = "UV manquants"; break; }
        if (Mesh.TriangleCount() % 2 != 0)               { bOk = false; Why = "doublage recto/verso absent"; break; }

        const double Half = Thickness * 0.5;
        double MinW = 1e300, MaxW = -1e300;
        for (const FVector3f& V : Mesh.Vertices) { MinW = std::min(MinW, (double)V.Z); MaxW = std::max(MaxW, (double)V.Z); }
        if (std::fabs(MinW + Half) > 1e-3 || std::fabs(MaxW - Half) > 1e-3)
        { bOk = false; Why = "epaisseur incorrecte"; break; }

        // UV dans [0,1] sur la face : un sommet hors bornes = materiau qui deborde.
        for (const FVector2f& UV : Mesh.UVs)
            if (UV.X < -1e-4f || UV.X > 1.0001f || UV.Y < -1e-4f || UV.Y > 1.0001f)
            { bOk = false; Why = "UV hors de [0,1]"; break; }
    }

    char Buf[160];
    std::snprintf(Buf, sizeof(Buf), "%d panneaux%s%s", Cases, Why.empty() ? "" : " -- ", Why.c_str());
    Report("maillage recto-verso d'epaisseur exacte, UV bornes", bOk, Buf);
}

// ── Propriete 6 : pas de T-jonction sur la face ────────────────────────────
// Un sommet pose au MILIEU de l'arete d'un triangle voisin (au lieu d'a son
// extremite) laisse une fissure d'un pixel a l'affichage. C'est cette propriete
// qui a rattrape un defaut injecte cote Unity.
static void TestNoTJunctions(int Cases)
{
    std::mt19937_64 Rng(20260830);
    bool bOk = true;
    int Inspected = 0;

    for (int i = 0; i < Cases && bOk; ++i)
    {
        const double HalfW = 800.0, HalfH = 500.0;
        const Loop Outer = Rect(0, 0, HalfW, HalfH);
        const TArray<Loop> Holes = GridHoles(Rng, HalfW, HalfH, 4);
        const TArray<FVector2D> Tris = BlockoutPanelGeometry::TriangulateFace(Outer, Holes);

        // Sommets uniques
        std::vector<FVector2D> Verts;
        for (const FVector2D& P : Tris)
        {
            bool Seen = false;
            for (const FVector2D& Q : Verts) if (std::fabs(P.X - Q.X) < 1e-6 && std::fabs(P.Y - Q.Y) < 1e-6) { Seen = true; break; }
            if (!Seen) Verts.push_back(P);
        }

        const double Tol = 1e-4;   // plus fin que l'Epsilon du code (1e-3 UU)
        for (int32 t = 0; t + 2 < Tris.Num() && bOk; t += 3)
            for (int e = 0; e < 3 && bOk; ++e)
            {
                const FVector2D& A = Tris[t + e];
                const FVector2D& B = Tris[t + (e + 1) % 3];
                const double SegLen = FVector2D::Distance(A, B);
                if (SegLen < 1e-3) continue;

                for (const FVector2D& V : Verts)
                {
                    if (FVector2D::Distance(V, A) < Tol || FVector2D::Distance(V, B) < Tol) continue;
                    ++Inspected;
                    if (DistToSeg(V, A, B) < Tol) { bOk = false; break; }
                }
            }
    }

    char Buf[160];
    std::snprintf(Buf, sizeof(Buf), "%d couples sommet/arete inspectes", Inspected);
    Report("aucune T-jonction sur la face", bOk, Buf);
}

// ── Propriete 7 : determinisme ─────────────────────────────────────────────
static void TestDeterminism(int Cases)
{
    std::mt19937_64 Rng(20260831);
    bool bOk = true;

    for (int i = 0; i < Cases && bOk; ++i)
    {
        const Loop Outer = RandomStarPolygon(Rng);
        const TArray<FVector2D> A = BlockoutPanelGeometry::TriangulateFace(Outer, TArray<Loop>());
        const TArray<FVector2D> B = BlockoutPanelGeometry::TriangulateFace(Outer, TArray<Loop>());
        if (A.Num() != B.Num()) { bOk = false; break; }
        for (int32 k = 0; k < A.Num(); ++k)
            if (A[k].X != B[k].X || A[k].Y != B[k].Y) { bOk = false; break; }
    }

    Report("triangulation deterministe (bit a bit)", bOk, std::to_string(Cases) + " contours");
}

// ── Regressions figees ─────────────────────────────────────────────────────
static void TestRegressionScaleInvariance()
{
    const Loop Base = Rect(0, 0, 400, 300);
    Loop Far;
    for (const FVector2D& P : Base) Far.Add(FVector2D(P.X + 4000.0, P.Y + 4000.0));

    const int32 NBase = BlockoutPanelGeometry::TriangulateFace(Base, TArray<Loop>()).Num();
    const int32 NFar  = BlockoutPanelGeometry::TriangulateFace(Far,  TArray<Loop>()).Num();

    Report("regression : invariance a la position dans le niveau",
           NBase == NFar && NBase >= 3,
           std::to_string(NBase) + " vs " + std::to_string(NFar) + " sommets");
}

static void TestRegressionNearHorizontalEdge()
{
    Loop Outer;
    Outer.Add(FVector2D(-500.0, -300.0));
    Outer.Add(FVector2D( 500.0, -300.0));
    Outer.Add(FVector2D( 500.0,  300.0));
    Outer.Add(FVector2D(-220.0,  300.028));
    Outer.Add(FVector2D(-500.0,  300.0));

    TArray<Loop> Holes;
    Holes.Add(Rect(-200.0, 200.0, 60.0, 40.0));

    const TArray<FVector2D> Tris = BlockoutPanelGeometry::TriangulateFace(Outer, Holes);
    const double Expected = std::fabs(Shoelace(Outer)) - std::fabs(Shoelace(Holes[0]));
    const double Rel = std::fabs(TriangulatedArea(Tris) - Expected) / std::max(Expected, 1.0);

    bool CoversHole = false;
    const FVector2D Center(-200.0, 200.0);
    for (int32 i = 0; i + 2 < Tris.Num(); i += 3)
        if (PointInTri(Center, Tris[i], Tris[i + 1], Tris[i + 2])) { CoversHole = true; break; }

    char Buf[160];
    std::snprintf(Buf, sizeof(Buf), "ecart relatif %.2e, trou %s", Rel, CoversHole ? "RECOUVERT" : "libre");
    Report("regression : arete quasi-horizontale pres d'un trou", Rel <= 1e-9 && !CoversHole, Buf);
}

// ── Validation de contours ─────────────────────────────────────────────────
static void TestContourValidation()
{
    const Loop Square = Rect(0, 0, 100, 100);
    Loop Bowtie;
    Bowtie.Add(FVector2D(-100, -100)); Bowtie.Add(FVector2D(100, 100));
    Bowtie.Add(FVector2D(100, -100));  Bowtie.Add(FVector2D(-100, 100));
    Loop Dup;
    Dup.Add(FVector2D(0, 0)); Dup.Add(FVector2D(100, 0));
    Dup.Add(FVector2D(100, 0)); Dup.Add(FVector2D(0, 100));

    Report("validation : contour auto-intersectant refuse",
           BlockoutPanelGeometry::IsSimplePolygon(Square)
           && !BlockoutPanelGeometry::IsSimplePolygon(Bowtie)
           && !BlockoutPanelGeometry::IsSimplePolygon(Dup));

    const Loop Outer = Rect(0, 0, 500, 500);
    Report("validation : trou strictement interieur avec marge",
           BlockoutPanelGeometry::ContainsWithMargin(Outer, Rect(0, 0, 100, 100), 10.0)
           && !BlockoutPanelGeometry::ContainsWithMargin(Outer, Rect(0, 0, 500, 100), 10.0)
           && !BlockoutPanelGeometry::ContainsWithMargin(Outer, Rect(0, 0, 495, 100), 10.0));

    Report("validation : chevauchement de deux contours",
           !BlockoutPanelGeometry::PolygonsOverlap(Rect(0, 0, 100, 100), Rect(150, 0, 40, 40))
           && BlockoutPanelGeometry::PolygonsOverlap(Rect(0, 0, 100, 100), Rect(50, 0, 100, 100))
           && BlockoutPanelGeometry::PolygonsOverlap(Rect(0, 0, 100, 100), Rect(0, 0, 20, 20)));
}

int main(int argc, char** argv)
{
    const int Scale = (argc > 1) ? std::atoi(argv[1]) : 1;

    std::printf("============================================================\n");
    std::printf("  BlockoutPanelGeometry -- proprietes, hors moteur (x%d)\n", Scale);
    std::printf("============================================================\n");

    TestAreaNoHoles(2000 * Scale);
    TestAreaWithHoles(600 * Scale);
    TestCoverage(6 * Scale, 400);
    TestNoDegenerate(1500 * Scale);
    TestPrism(400 * Scale);
    TestNoTJunctions(8 * Scale);
    TestDeterminism(300 * Scale);
    TestRegressionScaleInvariance();
    TestRegressionNearHorizontalEdge();
    TestContourValidation();

    std::printf("------------------------------------------------------------\n");
    std::printf("  %d/%d proprietes tenues\n", gChecks - gFailures, gChecks);
    std::printf("============================================================\n");
    return gFailures == 0 ? 0 : 1;
}
