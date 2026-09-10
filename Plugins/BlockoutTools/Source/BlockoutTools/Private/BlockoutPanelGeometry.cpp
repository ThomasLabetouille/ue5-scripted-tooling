#include "BlockoutPanelGeometry.h"

// Portage de LevelDesignTools (Unity) -- PolygonTriangulator.cs + PanelMeshBuilder.cs.
// Aucune API editeur ici : voir l'en-tete pour la raison.

namespace
{
	using BlockoutPanelGeometry::Epsilon;

	/**
	 * Une arete du contour, prete a etre balayee par ordonnee.
	 */
	struct FBlkEdge2D
	{
		FVector2D A = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		double MinY = 0.0;
		double MaxY = 0.0;

		FBlkEdge2D() = default;

		FBlkEdge2D(const FVector2D& InA, const FVector2D& InB)
			: A(InA), B(InB)
			, MinY(FMath::Min(InA.Y, InB.Y))
			, MaxY(FMath::Max(InA.Y, InB.Y))
		{
		}

		/**
		 * Abscisse de l'arete a l'ordonnee Y. Y est RAMENE dans l'etendue de l'arete :
		 * les appelants l'evaluent parfois jusqu'a Epsilon en dehors (tolerance sur les
		 * bornes de tranche), et sur une arete presque horizontale cette extrapolation
		 * minuscule en Y se traduit par un ecart enorme en X -- assez pour poser un
		 * sommet a l'interieur d'un trou voisin (bug reellement rencontre cote Unity :
		 * un cas sur 1500). Le bornage supprime le probleme sans rien changer au cas
		 * normal, ou Y est deja dans l'etendue.
		 */
		double XAt(double Y) const
		{
			const double DY = B.Y - A.Y;
			if (FMath::Abs(DY) < 1e-9) return A.X;
			if (Y <= MinY) return A.Y < B.Y ? A.X : B.X;
			if (Y >= MaxY) return A.Y < B.Y ? B.X : A.X;
			return A.X + (Y - A.Y) / DY * (B.X - A.X);
		}
	};

	struct FBlkCrossing
	{
		double X = 0.0;
		FBlkEdge2D Edge;
	};

	void BlkAppendLoop(const TArray<FVector2D>& Loop, TArray<FBlkEdge2D>& Edges, TArray<double>& Ys)
	{
		const int32 N = Loop.Num();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& A = Loop[i];
			const FVector2D& B = Loop[(i + 1) % N];
			Ys.Add(A.Y);
			// Les aretes horizontales ne traversent aucune tranche : elles n'apportent
			// rien au balayage (leurs sommets, eux, restent des frontieres de tranche).
			if (FMath::Abs(A.Y - B.Y) > Epsilon)
			{
				Edges.Add(FBlkEdge2D(A, B));
			}
		}
	}

	/**
	 * Points de coupure sur une ligne de tranche. En subdivisant les bords des trapezes
	 * sur ces memes points, deux tranches voisines partagent exactement les memes
	 * sommets le long de leur frontiere commune -- aucune T-jonction, donc aucune
	 * fissure a l'affichage.
	 */
	TArray<double> BlkBuildSplitLine(const TArray<FBlkEdge2D>& Edges, double Y)
	{
		TArray<double> Xs;
		for (const FBlkEdge2D& Edge : Edges)
		{
			if (Edge.MinY - Epsilon <= Y && Y <= Edge.MaxY + Epsilon)
			{
				Xs.Add(Edge.XAt(Y));
			}
		}
		Xs.Sort();

		TArray<double> Unique;
		Unique.Reserve(Xs.Num());
		for (double X : Xs)
		{
			if (Unique.Num() == 0 || X - Unique.Last() > Epsilon)
			{
				Unique.Add(X);
			}
		}
		return Unique;
	}

	void BlkBuildChain(TArray<double>& Chain, const TArray<double>& Splits, double XMin, double XMax)
	{
		Chain.Reset();
		Chain.Add(XMin);
		for (double X : Splits)
		{
			if (X > XMin + Epsilon && X < XMax - Epsilon)
			{
				Chain.Add(X);
			}
		}
		Chain.Add(XMax);
	}

	/**
	 * Triangule la bande comprise entre deux chaines horizontales (bas et haut d'un
	 * trapeze, eventuellement subdivisees). Le trapeze etant convexe, un simple
	 * balayage de gauche a droite suffit.
	 */
	void BlkEmitStrip(TArray<FVector2D>& Result, const TArray<double>& Bottom, double Y0,
		const TArray<double>& Top, double Y1, double AreaEpsilon)
	{
		int32 BI = 0;
		int32 TI = 0;
		int32 Guard = 0;
		const int32 MaxSteps = Bottom.Num() + Top.Num() + 4;

		while ((BI < Bottom.Num() - 1 || TI < Top.Num() - 1) && Guard++ < MaxSteps)
		{
			FVector2D A, B, C;
			const bool bAdvanceBottom = (TI >= Top.Num() - 1) ||
				(BI < Bottom.Num() - 1 && Bottom[BI + 1] <= Top[TI + 1]);

			if (bAdvanceBottom)
			{
				A = FVector2D(Bottom[BI], Y0);
				B = FVector2D(Bottom[BI + 1], Y0);
				C = FVector2D(Top[TI], Y1);
				++BI;
			}
			else
			{
				A = FVector2D(Bottom[BI], Y0);
				B = FVector2D(Top[TI + 1], Y1);
				C = FVector2D(Top[TI], Y1);
				++TI;
			}

			const double Area2 = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
			if (FMath::Abs(Area2) <= AreaEpsilon) continue;   // triangle plat (pointe du trapeze)

			Result.Add(A);
			Result.Add(B);
			Result.Add(C);
		}
	}

	double BlkCross2D(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		return (B.X - A.X) * (P.Y - A.Y) - (B.Y - A.Y) * (P.X - A.X);
	}

	bool BlkOnSegment(const FVector2D& A, const FVector2D& B, const FVector2D& P)
	{
		return P.X >= FMath::Min(A.X, B.X) - Epsilon && P.X <= FMath::Max(A.X, B.X) + Epsilon
			&& P.Y >= FMath::Min(A.Y, B.Y) - Epsilon && P.Y <= FMath::Max(A.Y, B.Y) + Epsilon;
	}

	bool BlkSegmentsIntersect(const FVector2D& P1, const FVector2D& P2, const FVector2D& P3, const FVector2D& P4)
	{
		const double D1 = BlkCross2D(P1, P3, P4);
		const double D2 = BlkCross2D(P2, P3, P4);
		const double D3 = BlkCross2D(P3, P1, P2);
		const double D4 = BlkCross2D(P4, P1, P2);

		if (((D1 > Epsilon && D2 < -Epsilon) || (D1 < -Epsilon && D2 > Epsilon)) &&
			((D3 > Epsilon && D4 < -Epsilon) || (D3 < -Epsilon && D4 > Epsilon)))
		{
			return true;
		}

		if (FMath::Abs(D1) <= Epsilon && BlkOnSegment(P3, P4, P1)) return true;
		if (FMath::Abs(D2) <= Epsilon && BlkOnSegment(P3, P4, P2)) return true;
		if (FMath::Abs(D3) <= Epsilon && BlkOnSegment(P1, P2, P3)) return true;
		if (FMath::Abs(D4) <= Epsilon && BlkOnSegment(P1, P2, P4)) return true;

		return false;
	}

	bool BlkApproximatelyEqual(const FVector2D& P, const FVector2D& Q)
	{
		return FMath::Abs(P.X - Q.X) <= Epsilon && FMath::Abs(P.Y - Q.Y) <= Epsilon;
	}

	double BlkDistanceToSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const double DX = B.X - A.X;
		const double DY = B.Y - A.Y;
		const double Length2 = DX * DX + DY * DY;
		if (Length2 < 1e-18) return FVector2D::Distance(P, A);

		double T = ((P.X - A.X) * DX + (P.Y - A.Y) * DY) / Length2;
		T = FMath::Clamp(T, 0.0, 1.0);
		return FVector2D::Distance(P, FVector2D(A.X + T * DX, A.Y + T * DY));
	}
}

namespace BlockoutPanelGeometry
{

void GetFlatAxes(const FVector& Size, int32& OutUAxis, int32& OutVAxis, int32& OutWAxis)
{
	const double AX = FMath::Abs(Size.X);
	const double AY = FMath::Abs(Size.Y);
	const double AZ = FMath::Abs(Size.Z);

	if (AZ <= AX && AZ <= AY)      { OutWAxis = 2; OutUAxis = 0; OutVAxis = 1; }  // dalle : epaisseur sur Z
	else if (AY <= AX && AY <= AZ) { OutWAxis = 1; OutUAxis = 0; OutVAxis = 2; }  // mur : epaisseur sur Y
	else                            { OutWAxis = 0; OutUAxis = 1; OutVAxis = 2; }  // mur : epaisseur sur X
}

TArray<FVector2D> RectangleContour(const FVector& Size, int32 UAxis, int32 VAxis)
{
	const double HU = FMath::Abs(Size[UAxis]) * 0.5;
	const double HV = FMath::Abs(Size[VAxis]) * 0.5;

	TArray<FVector2D> Contour;
	Contour.Add(FVector2D(-HU, -HV));
	Contour.Add(FVector2D( HU, -HV));
	Contour.Add(FVector2D( HU,  HV));
	Contour.Add(FVector2D(-HU,  HV));
	return Contour;
}

TArray<FVector2D> TriangulateFace(const TArray<FVector2D>& Outer, const TArray<TArray<FVector2D>>& Holes)
{
	TArray<FVector2D> Result;
	if (Outer.Num() < 3) return Result;

	TArray<FBlkEdge2D> Edges;
	TArray<double> Ys;

	BlkAppendLoop(Outer, Edges, Ys);
	for (const TArray<FVector2D>& Hole : Holes)
	{
		if (Hole.Num() >= 3) BlkAppendLoop(Hole, Edges, Ys);
	}
	if (Edges.Num() < 2) return Result;

	// Seuil d'aire en dessous duquel un triangle est considere comme plat. Il est
	// PROPORTIONNEL a la taille du panneau : un seuil absolu ferait dependre le
	// maillage genere de l'unite et de la position du mur -- un meme mur decoupe a
	// 40 m de l'origine, ou modelise a une autre echelle, ne rendrait pas les memes
	// triangles. (Bug reellement trouve cote Unity par le harnais de proprietes.)
	double MinX = TNumericLimits<double>::Max(), MaxX = TNumericLimits<double>::Lowest();
	double MinY = TNumericLimits<double>::Max(), MaxY = TNumericLimits<double>::Lowest();
	for (const FVector2D& P : Outer)
	{
		MinX = FMath::Min(MinX, P.X);
		MaxX = FMath::Max(MaxX, P.X);
		MinY = FMath::Min(MinY, P.Y);
		MaxY = FMath::Max(MaxY, P.Y);
	}
	const double AreaEpsilon = FMath::Max((MaxX - MinX) * (MaxY - MinY), 1e-12) * 1e-9;

	Ys.Sort();
	TArray<double> SlabLines;
	for (double Y : Ys)
	{
		if (SlabLines.Num() == 0 || Y - SlabLines.Last() > Epsilon) SlabLines.Add(Y);
	}
	if (SlabLines.Num() < 2) return Result;

	TArray<TArray<double>> Splits;
	Splits.Reserve(SlabLines.Num());
	for (double Y : SlabLines)
	{
		Splits.Add(BlkBuildSplitLine(Edges, Y));
	}

	TArray<FBlkCrossing> Crossings;
	TArray<double> Bottom;
	TArray<double> Top;

	for (int32 K = 0; K < SlabLines.Num() - 1; ++K)
	{
		const double Y0 = SlabLines[K];
		const double Y1 = SlabLines[K + 1];
		if (Y1 - Y0 <= Epsilon) continue;
		const double YMid = (Y0 + Y1) * 0.5;

		Crossings.Reset();
		for (const FBlkEdge2D& Edge : Edges)
		{
			if (Edge.MinY <= Y0 + Epsilon && Edge.MaxY >= Y1 - Epsilon)
			{
				Crossings.Add(FBlkCrossing{ Edge.XAt(YMid), Edge });
			}
		}
		if (Crossings.Num() < 2) continue;
		Crossings.Sort([](const FBlkCrossing& A, const FBlkCrossing& B) { return A.X < B.X; });

		// Regle pair/impair : entre la 1re et la 2e arete on est dedans, entre la 2e
		// et la 3e on est dehors (dans un trou), etc.
		for (int32 i = 0; i + 1 < Crossings.Num(); i += 2)
		{
			const FBlkEdge2D& Left = Crossings[i].Edge;
			const FBlkEdge2D& Right = Crossings[i + 1].Edge;

			double XL0 = Left.XAt(Y0), XR0 = Right.XAt(Y0);
			double XL1 = Left.XAt(Y1), XR1 = Right.XAt(Y1);
			if (XR0 < XL0) Swap(XL0, XR0);
			if (XR1 < XL1) Swap(XL1, XR1);
			if (XR0 - XL0 <= Epsilon && XR1 - XL1 <= Epsilon) continue;

			BlkBuildChain(Bottom, Splits[K], XL0, XR0);
			BlkBuildChain(Top, Splits[K + 1], XL1, XR1);
			BlkEmitStrip(Result, Bottom, Y0, Top, Y1, AreaEpsilon);
		}
	}

	return Result;
}

bool IsSimplePolygon(const TArray<FVector2D>& Points)
{
	const int32 N = Points.Num();
	if (N < 3) return false;

	for (int32 i = 0; i < N; ++i)
	{
		for (int32 j = i + 1; j < N; ++j)
		{
			if (BlkApproximatelyEqual(Points[i], Points[j])) return false;
		}
	}

	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D& A0 = Points[i];
		const FVector2D& A1 = Points[(i + 1) % N];

		for (int32 j = i + 1; j < N; ++j)
		{
			// Aretes adjacentes : elles partagent un sommet, ce n'est pas un croisement.
			if ((j + 1) % N == i || (i + 1) % N == j) continue;

			if (BlkSegmentsIntersect(A0, A1, Points[j], Points[(j + 1) % N])) return false;
		}
	}

	return true;
}

bool PointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon)
{
	bool bInside = false;
	const int32 N = Polygon.Num();
	for (int32 i = 0, j = N - 1; i < N; j = i++)
	{
		// NE PAS nommer ces variables PI / PJ : PI est une MACRO d'Unreal
		// (UnrealMathUtility.h). Le preprocesseur la remplace par une constante et le
		// fichier ne compile plus, avec une erreur de syntaxe qui ne dit pas pourquoi.
		const FVector2D& Vi = Polygon[i];
		const FVector2D& Vj = Polygon[j];

		if ((Vi.Y > Point.Y) != (Vj.Y > Point.Y))
		{
			const double X = Vi.X + (Point.Y - Vi.Y) / (Vj.Y - Vi.Y) * (Vj.X - Vi.X);
			if (X > Point.X) bInside = !bInside;
		}
	}
	return bInside;
}

bool PolygonsOverlap(const TArray<FVector2D>& A, const TArray<FVector2D>& B)
{
	const int32 NA = A.Num();
	const int32 NB = B.Num();
	if (NA < 3 || NB < 3) return false;

	for (int32 i = 0; i < NA; ++i)
	{
		const FVector2D& A0 = A[i];
		const FVector2D& A1 = A[(i + 1) % NA];
		for (int32 j = 0; j < NB; ++j)
		{
			if (BlkSegmentsIntersect(A0, A1, B[j], B[(j + 1) % NB])) return true;
		}
	}

	return PointInPolygon(A[0], B) || PointInPolygon(B[0], A);
}

bool ContainsWithMargin(const TArray<FVector2D>& Outer, const TArray<FVector2D>& Inner, double Margin)
{
	const int32 NO = Outer.Num();
	const int32 NI = Inner.Num();
	if (NO < 3 || NI < 3) return false;

	for (const FVector2D& P : Inner)
	{
		if (!PointInPolygon(P, Outer)) return false;
		if (DistanceToLoop(P, Outer) <= Margin) return false;
	}

	for (int32 i = 0; i < NI; ++i)
	{
		const FVector2D& A0 = Inner[i];
		const FVector2D& A1 = Inner[(i + 1) % NI];
		for (int32 j = 0; j < NO; ++j)
		{
			if (BlkSegmentsIntersect(A0, A1, Outer[j], Outer[(j + 1) % NO])) return false;
		}
	}

	return true;
}

double DistanceToLoop(const FVector2D& P, const TArray<FVector2D>& Loop)
{
	double Best = TNumericLimits<double>::Max();
	const int32 N = Loop.Num();
	for (int32 i = 0; i < N; ++i)
	{
		Best = FMath::Min(Best, BlkDistanceToSegment(P, Loop[i], Loop[(i + 1) % N]));
	}
	return Best;
}

double SignedArea(const TArray<FVector2D>& Loop)
{
	const int32 N = Loop.Num();
	if (N < 3) return 0.0;

	double Sum = 0.0;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D& A = Loop[i];
		const FVector2D& B = Loop[(i + 1) % N];
		Sum += A.X * B.Y - B.X * A.Y;
	}
	return Sum * 0.5;
}

}   // namespace BlockoutPanelGeometry

// ═══════════════════════════════════════════════════════════════════════════
// Assemblage 3D du panneau (portage de PanelMeshBuilder.cs)
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
	/**
	 * Repartit les sommets d'un contour (U,V) sur sa boite englobante, pour donner des
	 * UV dans [0,1]. Sans cela tous les sommets partagent l'UV (0,0) et le panneau
	 * n'affiche qu'un seul texel de son materiau -- un aplat uni des qu'une texture
	 * est appliquee.
	 */
	struct FBlkFaceUVMapping
	{
		double MinU = 0.0, MinV = 0.0, SpanU = 1.0, SpanV = 1.0;

		static FBlkFaceUVMapping From(const TArray<FVector2D>& Outer)
		{
			double MinU = TNumericLimits<double>::Max(), MaxU = TNumericLimits<double>::Lowest();
			double MinV = TNumericLimits<double>::Max(), MaxV = TNumericLimits<double>::Lowest();
			for (const FVector2D& P : Outer)
			{
				MinU = FMath::Min(MinU, P.X);
				MaxU = FMath::Max(MaxU, P.X);
				MinV = FMath::Min(MinV, P.Y);
				MaxV = FMath::Max(MaxV, P.Y);
			}

			FBlkFaceUVMapping Mapping;
			Mapping.MinU = MinU;
			Mapping.MinV = MinV;
			Mapping.SpanU = FMath::Max(MaxU - MinU, 1e-6);
			Mapping.SpanV = FMath::Max(MaxV - MinV, 1e-6);
			return Mapping;
		}

		FVector2f Of(const FVector2D& P) const
		{
			return FVector2f(
				(float)((P.X - MinU) / SpanU),
				(float)((P.Y - MinV) / SpanV));
		}
	};

	FVector3f BlkMapUVW(double U, double V, double W, int32 UAxis, int32 VAxis, int32 WAxis)
	{
		FVector3f Result = FVector3f::ZeroVector;
		Result[UAxis] = (float)U;
		Result[VAxis] = (float)V;
		Result[WAxis] = (float)W;
		return Result;
	}

	/**
	 * Ajoute un triangle des deux cotes (deux copies de sommets, enroulement oppose)
	 * afin que la face soit visible quel que soit le sens de vue.
	 *
	 * Le prix est connu : six sommets et deux triangles par triangle utile. Le panneau
	 * etant un prisme ferme (deux faces + les chants), l'enroulement sortant est
	 * determinable et ce doublage pourrait etre supprime -- a condition de le verifier
	 * de visu dans l'editeur, ce qu'aucun test hors moteur ne peut faire a notre place.
	 * Cote Unity, la mesure a montre que sur 1498 panneaux AUCUN n'avait deja un
	 * enroulement coherent : supprimer le doublage demande d'orienter le maillage a
	 * l'emission, pas de retirer quatre lignes.
	 */
	void BlkAddTriangleBothSides(FBlockoutPanelMesh& Mesh,
		const FVector3f& A, const FVector3f& B, const FVector3f& C,
		const FVector2f& UA, const FVector2f& UB, const FVector2f& UC)
	{
		const int32 I0 = Mesh.Vertices.Num();
		Mesh.Vertices.Add(A); Mesh.Vertices.Add(B); Mesh.Vertices.Add(C);
		Mesh.UVs.Add(UA); Mesh.UVs.Add(UB); Mesh.UVs.Add(UC);
		Mesh.Triangles.Add(I0); Mesh.Triangles.Add(I0 + 1); Mesh.Triangles.Add(I0 + 2);

		const int32 I1 = Mesh.Vertices.Num();
		Mesh.Vertices.Add(A); Mesh.Vertices.Add(B); Mesh.Vertices.Add(C);
		Mesh.UVs.Add(UA); Mesh.UVs.Add(UB); Mesh.UVs.Add(UC);
		Mesh.Triangles.Add(I1); Mesh.Triangles.Add(I1 + 2); Mesh.Triangles.Add(I1 + 1);   // normale opposee
	}

	void BlkAddFace(FBlockoutPanelMesh& Mesh, const TArray<FVector2D>& FaceTriangles, double W,
		int32 UAxis, int32 VAxis, int32 WAxis, const FBlkFaceUVMapping& UVMapping)
	{
		for (int32 i = 0; i + 2 < FaceTriangles.Num(); i += 3)
		{
			const FVector2D& PA = FaceTriangles[i];
			const FVector2D& PB = FaceTriangles[i + 1];
			const FVector2D& PC = FaceTriangles[i + 2];

			BlkAddTriangleBothSides(Mesh,
				BlkMapUVW(PA.X, PA.Y, W, UAxis, VAxis, WAxis),
				BlkMapUVW(PB.X, PB.Y, W, UAxis, VAxis, WAxis),
				BlkMapUVW(PC.X, PC.Y, W, UAxis, VAxis, WAxis),
				UVMapping.Of(PA), UVMapping.Of(PB), UVMapping.Of(PC));
		}
	}

	void BlkAddLoopSides(FBlockoutPanelMesh& Mesh, const TArray<FVector2D>& LoopUV,
		double WMin, double WMax, int32 UAxis, int32 VAxis, int32 WAxis)
	{
		const int32 N = LoopUV.Num();
		if (N < 3) return;

		// U suit le developpe du contour (distance parcourue), V suit l'epaisseur :
		// une texture appliquee sur la tranche d'un trou reste ainsi continue.
		double Perimeter = 0.0;
		for (int32 i = 0; i < N; ++i) Perimeter += FVector2D::Distance(LoopUV[i], LoopUV[(i + 1) % N]);
		if (Perimeter < 1e-5) Perimeter = 1.0;

		double Run = 0.0;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector2D& P0 = LoopUV[i];
			const FVector2D& P1 = LoopUV[(i + 1) % N];

			const double U0 = Run / Perimeter;
			Run += FVector2D::Distance(P0, P1);
			const double U1 = Run / Perimeter;

			const FVector3f A = BlkMapUVW(P0.X, P0.Y, WMin, UAxis, VAxis, WAxis);
			const FVector3f B = BlkMapUVW(P1.X, P1.Y, WMin, UAxis, VAxis, WAxis);
			const FVector3f C = BlkMapUVW(P1.X, P1.Y, WMax, UAxis, VAxis, WAxis);
			const FVector3f D = BlkMapUVW(P0.X, P0.Y, WMax, UAxis, VAxis, WAxis);

			const FVector2f UVA((float)U0, 0.f);
			const FVector2f UVB((float)U1, 0.f);
			const FVector2f UVC((float)U1, 1.f);
			const FVector2f UVD((float)U0, 1.f);

			BlkAddTriangleBothSides(Mesh, A, B, C, UVA, UVB, UVC);
			BlkAddTriangleBothSides(Mesh, A, C, D, UVA, UVC, UVD);
		}
	}

	/**
	 * Contours du bord de la face : le contour exterieur du panneau et celui de chaque
	 * trou.
	 *
	 * Une arete du bord est une arete portee par UN SEUL triangle -- toutes les autres
	 * sont interieures et partagees par deux. On les enchaine ensuite bout a bout pour
	 * reformer des boucles fermees, ce qui permet de derouler une coordonnee de texture
	 * continue le long de chaque chant.
	 */
	TArray<TArray<FVector2D>> BlkBoundaryLoops(const TArray<FVector2D>& FaceTriangles)
	{
		TMap<FVector2D, int32> Ids;
		TArray<FVector2D> Points;
		const int32 TriangleCount = FaceTriangles.Num() / 3;
		TArray<int32> Corners;
		Corners.SetNumUninitialized(TriangleCount * 3);

		for (int32 i = 0; i < TriangleCount * 3; ++i)
		{
			int32* Found = Ids.Find(FaceTriangles[i]);
			int32 Id;
			if (Found)
			{
				Id = *Found;
			}
			else
			{
				Id = Points.Num();
				Ids.Add(FaceTriangles[i], Id);
				Points.Add(FaceTriangles[i]);
			}
			Corners[i] = Id;
		}

		// Comptage des aretes, sans tenir compte du sens.
		TMap<uint64, int32> Uses;
		TMap<uint64, int32> Directed;   // arete non orientee -> sommet de depart
		for (int32 T = 0; T < TriangleCount; ++T)
		{
			for (int32 E = 0; E < 3; ++E)
			{
				const int32 A = Corners[T * 3 + E];
				const int32 B = Corners[T * 3 + (E + 1) % 3];
				if (A == B) continue;

				const uint64 Key = (A < B)
					? (((uint64)(uint32)A << 32) | (uint32)B)
					: (((uint64)(uint32)B << 32) | (uint32)A);

				if (int32* Existing = Uses.Find(Key)) { ++(*Existing); }
				else { Uses.Add(Key, 1); }
				Directed.Add(Key, A);
			}
		}

		// Enchainement en CONSOMMANT les aretes une par une, et non en supposant un
		// seul depart par sommet. Un sommet peut en porter deux : il suffit que deux
		// contours se touchent en un point, ce qui arrive sur les tranches tres minces.
		// Consommer les aretes garantit que chacune donne exactement un quad de chant,
		// quelle que soit la facon dont les boucles se croisent.
		TMap<int32, TArray<int32>> Outgoing;
		for (const TPair<uint64, int32>& Pair : Uses)
		{
			if (Pair.Value != 1) continue;

			const int32 A = Directed.FindChecked(Pair.Key);
			const int32 High = (int32)(Pair.Key >> 32);
			const int32 Low = (int32)(Pair.Key & 0xFFFFFFFFull);
			const int32 B = (High == A) ? Low : High;

			Outgoing.FindOrAdd(A).Add(B);
		}

		TArray<TArray<FVector2D>> Loops;
		TArray<int32> Starts;
		Outgoing.GetKeys(Starts);

		for (int32 Start : Starts)
		{
			while (Outgoing.FindChecked(Start).Num() > 0)
			{
				TArray<FVector2D> Loop;
				int32 Current = Start;
				bool bClosed = false;

				while (true)
				{
					TArray<int32>* Targets = Outgoing.Find(Current);
					if (!Targets || Targets->Num() == 0) break;

					const int32 Following = Targets->Last();
					Targets->RemoveAt(Targets->Num() - 1);
					Loop.Add(Points[Current]);

					Current = Following;
					if (Current == Start) { bClosed = true; break; }
				}

				// Le bord d'une surface triangulee est toujours fait de cycles fermes ;
				// une chaine ouverte signalerait une incoherence, et la fermer d'office
				// ajouterait un quad fantome. On prefere l'ignorer et laisser les tests
				// le dire.
				if (bClosed && Loop.Num() >= 3) Loops.Add(MoveTemp(Loop));
			}
		}

		return Loops;
	}
}

namespace BlockoutPanelGeometry
{

bool BuildPanelMesh(const TArray<FVector2D>& Outer, double Thickness,
	int32 UAxis, int32 VAxis, int32 WAxis, const TArray<TArray<FVector2D>>& Holes,
	FBlockoutPanelMesh& OutMesh)
{
	OutMesh.Reset();

	if (Outer.Num() < 3) return false;

	const double HalfW = FMath::Abs(Thickness) * 0.5;
	if (HalfW <= 0.0) return false;

	TArray<TArray<FVector2D>> HoleLoops;
	for (const TArray<FVector2D>& Hole : Holes)
	{
		if (Hole.Num() >= 3) HoleLoops.Add(Hole);
	}

	const TArray<FVector2D> FaceTriangles = TriangulateFace(Outer, HoleLoops);
	if (FaceTriangles.Num() < 3) return false;

	const FBlkFaceUVMapping UVMapping = FBlkFaceUVMapping::From(Outer);

	BlkAddFace(OutMesh, FaceTriangles, -HalfW, UAxis, VAxis, WAxis, UVMapping);
	BlkAddFace(OutMesh, FaceTriangles,  HalfW, UAxis, VAxis, WAxis, UVMapping);

	// Les chants sont extrudes A PARTIR DU BORD DE LA FACE, et non recalcules depuis
	// les contours d'origine. C'est ce qui garantit qu'ils s'y raccordent : ils
	// reprennent exactement ses sommets, aux memes valeurs, au bit pres.
	//
	// La version precedente (cote Unity) redecoupait les contours en parallele et
	// comptait sur les deux calculs pour tomber d'accord. Ils divergeaient des que
	// deux sommets de trous voisins avaient des ordonnees a moins d'un epsilon : la
	// face les ramenait sur une meme ligne de tranche, le chant gardait les valeurs
	// d'origine, et il restait entre les deux une fissure de quelques microns.
	for (const TArray<FVector2D>& Loop : BlkBoundaryLoops(FaceTriangles))
	{
		BlkAddLoopSides(OutMesh, Loop, -HalfW, HalfW, UAxis, VAxis, WAxis);
	}

	return !OutMesh.IsEmpty();
}

}   // namespace BlockoutPanelGeometry

FVector2D BlockoutPanelGeometry::SnapToOrthogonalFrame(const FVector2D& Last,
	const FVector2D& Candidate, const FVector2D& FrameDir, const FVector2D& IncomingDir)
{
	const FVector2D U = FrameDir.GetSafeNormal();
	if (U.IsNearlyZero())
	{
		// Repere indefini (premier segment pas encore pose, ou de longueur nulle) :
		// il n'y a rien a quoi s'aligner. Rendre le candidat tel quel est le seul
		// comportement honnete -- inventer un axe deplacerait le point sans raison.
		return Candidate;
	}

	const FVector2D V(-U.Y, U.X);
	const FVector2D D = Candidate - Last;

	const double Du = FVector2D::DotProduct(D, U);
	const double Dv = FVector2D::DotProduct(D, V);

	bool bUseU = FMath::Abs(Du) >= FMath::Abs(Dv);

	// Interdiction du repli. Garder seulement l'axe dominant suffit a obtenir des
	// angles droits SAUF dans un cas : repartir a l'oppose du segment precedent. Le
	// nouveau mur se poserait alors SUR le precedent -- angle de 0 degre, contour
	// auto-intersectant, et la salle serait refusee a la fermeture sans que rien
	// n'ait prevenu pendant le trace. On bascule sur l'axe perpendiculaire, qui ne
	// peut pas etre un repli lui aussi (les deux axes sont orthogonaux).
	const FVector2D In = IncomingDir.GetSafeNormal();
	if (!In.IsNearlyZero())
	{
		const FVector2D Choisi = bUseU ? (Du >= 0.0 ? U : -U) : (Dv >= 0.0 ? V : -V);
		if (FVector2D::DotProduct(Choisi, In) < -0.5)
		{
			bUseU = !bUseU;
		}
	}

	return bUseU ? (Last + U * Du) : (Last + V * Dv);
}

bool BlockoutPanelGeometry::SnapLastPointToCloseFrame(const FVector2D& First, const FVector2D& FrameDir,
	const FVector2D& BeforeLast, FVector2D& OutAdjusted)
{
	const FVector2D U = FrameDir.GetSafeNormal();
	if (U.IsNearlyZero())
	{
		return false;
	}

	// V porte le segment de fermeture (perpendiculaire au premier mur), U porte le
	// dernier mur. Les deux axes sont orthogonaux par construction : les droites se
	// coupent toujours, il n'y a pas de cas "paralleles" a traiter.
	const FVector2D V(-U.Y, U.X);

	// BeforeLast + U*s = First + V*t, resolu en croisant par V.
	const FVector2D W = First - BeforeLast;
	const double Denom = U.X * V.Y - U.Y * V.X;       // U ^ V, vaut 1 (repere direct)
	const double S = (W.X * V.Y - W.Y * V.X) / Denom;

	const FVector2D Candidate = BeforeLast + U * S;

	// Un ajustement qui ramene le point sur son voisin produirait un mur de longueur
	// nulle : la triangulation en sortirait des triangles plats.
	if (FVector2D::Distance(Candidate, BeforeLast) < Epsilon
		|| FVector2D::Distance(Candidate, First) < Epsilon)
	{
		return false;
	}

	OutAdjusted = Candidate;
	return true;
}
