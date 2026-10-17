#include "Solve/DTHExpressionSolver.h"

#include "Library/DTHExpressionSamples.h"
#include "Library/DTHFaceRig.h"

namespace
{
	const TCHAR* Figure = TEXT("Genesis9|");
	/** Leaves smaller than this are not reported as missing (morph value, or bone degrees / 30). */
	constexpr double ReportThreshold = 0.05;
	constexpr double Epsilon = 1e-4;
	constexpr int32 MaxOuterSteps = 30;
	constexpr int32 MaxSweeps = 400;

	bool IsLeafMorph(const FString& Key)
	{
		return Key.StartsWith(Figure) && (Key.Contains(TEXT("|facs_bs_")) || Key.Contains(TEXT("|facs_cbs_")));
	}

	bool IsLeafBone(const FString& Key)
	{
		return Key.StartsWith(Figure) && Key.EndsWith(TEXT("Rotate"));
	}
}

FDTHExpressionSolver::FDTHExpressionSolver(const FDTHFaceRig& InRig, const FDTHExpressionSamples& Samples, bool bInStripGaze)
	: Rig(InRig)
	, bStripGaze(bInStripGaze)
{
	Rest = &Samples.Rest;
	const FDTHDazState* Frame0 = Samples.RomFrames.Find(0);
	if (!Frame0)
	{
		Error = TEXT("The samples have no ROM frame 0.");
		return;
	}

	// The face poses are the ROM's face frames, in order: the last frames that key a FACS property to a value other than
	// its rest value, as many as the face pose asset has poses.
	auto ChangesFace = [&Samples, Frame0](int32 F)
	{
		const FDTHDazState& S = Samples.RomFrames[F];
		for (const FString& K : S.Keyed)
		{
			const double* V = S.Morphs.Find(K);
			const double* R = Frame0->Morphs.Find(K);
			if (K.Contains(TEXT("|facs_")) && FMath::Abs((V ? *V : 0.0) - (R ? *R : 0.0)) > 1e-4) return true;
		}
		return false;
	};
	TArray<int32> Frames;
	Samples.RomFrames.GetKeys(Frames);
	Frames.Sort();
	int32 LastFace = INDEX_NONE;
	for (const int32 F : Frames)
	{
		if (F > 0 && ChangesFace(F)) LastFace = F;
	}
	FirstFrame = LastFace == INDEX_NONE ? INDEX_NONE : LastFace - Rig.Poses.Num() + 1;
	if (FirstFrame <= 0 || !Samples.RomFrames.Contains(FirstFrame) || !ChangesFace(FirstFrame))
	{
		Error = FString::Printf(TEXT("The samples' ROM does not line up with DTH's face pose asset (%d poses): sample it again with the current Daz script."), Rig.Poses.Num());
		return;
	}

	// Each pose against frame 0, or against the frame it was posed on top of (the ROM holds that frame's value).
	Basis.SetNum(Rig.Poses.Num());
	for (int32 P = 0; P < Rig.Poses.Num(); ++P)
	{
		const FDTHDazState* State = Samples.RomFrames.Find(FirstFrame + P);
		const FDTHDazState* Ref = State && State->Base > 0 ? Samples.RomFrames.Find(State->Base) : Frame0;
		if (!State || !Ref)
		{
			Error = FString::Printf(TEXT("The samples' ROM ends before the face pose %s (frame %d): DTH's ROM and pose asset do not match."),
				*Rig.Poses[P].ToString(), FirstFrame + P);
			return;
		}
		TMap<FString, double> Delta;
		Leaves(*State, *Ref, Delta);
		for (const TPair<FString, double>& D : Delta)
		{
			Basis[P].Emplace(LeafIndex(D.Key), D.Value);
		}
	}

	TArray<bool> bPoseDriven;
	bPoseDriven.Init(true, Rig.Poses.Num());
	for (const int32 U : Rig.Undriven) bPoseDriven[U] = false;
	bLeafDriven.Init(false, LeafKeys.Num());
	for (int32 P = 0; P < Basis.Num(); ++P)
	{
		for (const TPair<int32, double>& E : Basis[P])
		{
			bLeafDriven[E.Key] |= bPoseDriven[P];
		}
	}

	TSet<int32> Free;
	for (const FDTHFaceRig::FLink& L : Rig.Linear) Free.Add(L.Curve);
	for (const FDTHFaceRig::FPair& P : Rig.Pairs) { Free.Add(P.A); Free.Add(P.B); }
	for (const int32 C : Free)
	{
		const FString Name = Rig.Curves[C].ToString();
		const bool bGazeCurve = Name.Contains(TEXT("_eyeLook")) && !Name.Contains(TEXT("Parallel"));
		if (!(bStripGaze && bGazeCurve))
		{
			FreeCurves.Add(C);
		}
	}
	FreeCurves.Sort();
}

bool FDTHExpressionSolver::IsValid(FString* OutError) const
{
	if (OutError) *OutError = Error;
	return Error.IsEmpty();
}

int32 FDTHExpressionSolver::LeafIndex(const FString& Key)
{
	if (const int32* Found = LeafIndices.Find(Key))
	{
		return *Found;
	}
	return LeafIndices.Add(Key, LeafKeys.Add(Key));
}

bool FDTHExpressionSolver::IsGazeLeaf(const FString& Key) const
{
	return Key.Contains(TEXT("|l_eye|")) || Key.Contains(TEXT("|r_eye|")) || Key.Contains(TEXT("|facs_bs_EyeLook"));
}

void FDTHExpressionSolver::Leaves(const FDTHDazState& State, const FDTHDazState& Reference, TMap<FString, double>& Out) const
{
	auto Add = [&Out](const TMap<FString, double>& A, const TMap<FString, double>& B, bool bBone)
	{
		for (const TPair<FString, double>& E : A)
		{
			if (bBone ? IsLeafBone(E.Key) : IsLeafMorph(E.Key))
			{
				const double* R = B.Find(E.Key);
				const double V = (E.Value - (R ? *R : 0.0)) / (bBone ? 30.0 : 1.0);
				if (FMath::Abs(V) > 1e-6) Out.Add(E.Key, V);
			}
		}
		for (const TPair<FString, double>& E : B)
		{
			if (!A.Contains(E.Key) && (bBone ? IsLeafBone(E.Key) : IsLeafMorph(E.Key)))
			{
				const double V = -E.Value / (bBone ? 30.0 : 1.0);
				if (FMath::Abs(V) > 1e-6) Out.Add(E.Key, V);
			}
		}
	};
	Add(State.Morphs, Reference.Morphs, false);
	Add(State.Bones, Reference.Bones, true);
}

FDTHExpressionFit FDTHExpressionSolver::Fit(const FDTHDazState& Expression) const
{
	FDTHExpressionFit Result;
	Result.Values.Init(0.0, Rig.Curves.Num());
	if (!Error.IsEmpty())
	{
		Result.Missing.Add(Error);
		return Result;
	}

	const int32 NumLeaves = LeafKeys.Num();
	const int32 NumPoses = Rig.Poses.Num();
	const int32 NumFree = FreeCurves.Num();

	// Target, and the leaves no ROM pose makes at all.
	TMap<FString, double> TargetMap;
	Leaves(Expression, *Rest, TargetMap);
	TArray<double> Target;
	Target.Init(0.0, NumLeaves);
	double OutsideSq = 0.0, TargetSq = 0.0;
	for (const TPair<FString, double>& T : TargetMap)
	{
		if (bStripGaze && IsGazeLeaf(T.Key))
		{
			continue;
		}
		TargetSq += T.Value * T.Value;
		if (const int32* I = LeafIndices.Find(T.Key))
		{
			Target[*I] = T.Value;
		}
		else
		{
			OutsideSq += T.Value * T.Value;
			if (FMath::Abs(T.Value) > ReportThreshold)
			{
				Result.Missing.Add(FString::Printf(TEXT("%s (%.2f): no pose in DTH's face ROM makes this shape."), *T.Key.Mid(9), T.Value));
			}
		}
	}

	TArray<double>& C = Result.Values;
	TArray<double> W, W1, Model, A, H, G, Y;
	A.SetNumUninitialized(NumLeaves * NumFree);
	H.SetNumUninitialized(NumFree * NumFree);
	G.SetNumUninitialized(NumFree);
	Y.SetNumUninitialized(NumFree);

	auto ModelLeaves = [&](const TArray<double>& Weights, TArray<double>& Out)
	{
		Out.Init(0.0, NumLeaves);
		for (int32 P = 0; P < NumPoses; ++P)
		{
			if (Weights[P] != 0.0)
			{
				for (const TPair<int32, double>& E : Basis[P]) Out[E.Key] += E.Value * Weights[P];
			}
		}
	};

	for (int32 Step = 0; Step < MaxOuterSteps; ++Step)
	{
		Rig.Evaluate(C, W);
		ModelLeaves(W, Model);

		// A = basis * d(weights)/d(free curves), column by column.
		FMemory::Memzero(A.GetData(), A.Num() * sizeof(double));
		for (int32 K = 0; K < NumFree; ++K)
		{
			const int32 Curve = FreeCurves[K];
			const double Saved = C[Curve];
			C[Curve] = Saved + Epsilon;
			Rig.Evaluate(C, W1);
			C[Curve] = Saved;
			for (int32 P = 0; P < NumPoses; ++P)
			{
				const double D = (W1[P] - W[P]) / Epsilon;
				if (D != 0.0)
				{
					for (const TPair<int32, double>& E : Basis[P]) A[E.Key * NumFree + K] += E.Value * D;
				}
			}
		}

		// Bounded least squares for the free values Y: min |A Y - B| with B = target - model + A * current.
		TArray<double> B;
		B.SetNumUninitialized(NumLeaves);
		for (int32 L = 0; L < NumLeaves; ++L)
		{
			double V = Target[L] - Model[L];
			for (int32 K = 0; K < NumFree; ++K) V += A[L * NumFree + K] * C[FreeCurves[K]];
			B[L] = V;
		}
		for (int32 I = 0; I < NumFree; ++I)
		{
			double Gi = 0.0;
			for (int32 L = 0; L < NumLeaves; ++L) Gi += A[L * NumFree + I] * B[L];
			G[I] = Gi;
			for (int32 J = I; J < NumFree; ++J)
			{
				double Hij = 0.0;
				for (int32 L = 0; L < NumLeaves; ++L) Hij += A[L * NumFree + I] * A[L * NumFree + J];
				H[I * NumFree + J] = H[J * NumFree + I] = Hij;
			}
			Y[I] = C[FreeCurves[I]];
		}
		for (int32 Sweep = 0; Sweep < MaxSweeps; ++Sweep)
		{
			double Biggest = 0.0;
			for (int32 I = 0; I < NumFree; ++I)
			{
				const double Hii = H[I * NumFree + I];
				if (Hii <= 1e-12) continue;
				double Gradient = -G[I];
				for (int32 J = 0; J < NumFree; ++J) Gradient += H[I * NumFree + J] * Y[J];
				const double New = FMath::Clamp(Y[I] - Gradient / Hii, 0.0, 1.0);
				Biggest = FMath::Max(Biggest, FMath::Abs(New - Y[I]));
				Y[I] = New;
			}
			if (Biggest < 1e-9) break;
		}

		double Moved = 0.0;
		for (int32 K = 0; K < NumFree; ++K)
		{
			Moved = FMath::Max(Moved, FMath::Abs(Y[K] - C[FreeCurves[K]]));
			C[FreeCurves[K]] = Y[K];
		}
		if (Moved < 1e-6) break;
	}

	Rig.Evaluate(C, W);
	ModelLeaves(W, Model);
	double ResidualSq = OutsideSq;
	for (int32 L = 0; L < NumLeaves; ++L)
	{
		const double R = Target[L] - Model[L];
		ResidualSq += R * R;
		if (FMath::Abs(Target[L]) > ReportThreshold && !bLeafDriven[L])
		{
			Result.Missing.Add(FString::Printf(TEXT("%s (%.2f): only DTH poses the face rig does not drive make this shape."), *LeafKeys[L].Mid(9), Target[L]));
		}
	}
	for (double& V : C) if (V < 1e-5) V = 0.0;
	Result.TargetSize = FMath::Sqrt(TargetSq);
	Result.Residual = FMath::Sqrt(ResidualSq);
	return Result;
}
