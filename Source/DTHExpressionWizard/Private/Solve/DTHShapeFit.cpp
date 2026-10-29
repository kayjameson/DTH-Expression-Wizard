#include "Solve/DTHShapeFit.h"

#include "Async/ParallelFor.h"
#include "Library/DTHFaceRig.h"
#include "Solve/DTHFaceEvaluator.h"

namespace
{
	/** Slope step on a curve value when measuring. */
	constexpr double Step = 0.03;
	/** A pair interaction smaller than this (cm, at any point) is left out of the model. */
	constexpr float PairThreshold = 0.01f;
	/** Inactive curves whose slope is measured each iteration, besides the curves in use. */
	constexpr int32 Candidates = 32;
	constexpr int32 MaxModelSteps = 8;

	double Dot(const TArray<float>& A, const TArray<float>& B)
	{
		double S = 0.0;
		for (int32 I = 0; I < A.Num(); ++I) S += static_cast<double>(A[I]) * B[I];
		return S;
	}

	/** min 1/2 x'Hx - g'x over Lo <= x <= Hi for the free coordinates (coordinate descent; H is n x n row-major). */
	void BoundedQuadratic(const TArray<double>& H, const TArray<double>& G, const TArray<bool>& Free, TArray<double>& X, double Hi = 1.0, int32 Sweeps = 600)
	{
		const int32 N = G.Num();
		for (int32 Sweep = 0; Sweep < Sweeps; ++Sweep)
		{
			double Biggest = 0.0;
			for (int32 I = 0; I < N; ++I)
			{
				if (!Free[I]) continue;
				const double Hii = H[I * N + I];
				if (Hii <= 1e-12) continue;
				double Gradient = -G[I];
				for (int32 J = 0; J < N; ++J) Gradient += H[I * N + J] * X[J];
				const double New = FMath::Clamp(X[I] - Gradient / Hii, 0.0, Hi);
				Biggest = FMath::Max(Biggest, FMath::Abs(New - X[I]));
				X[I] = New;
			}
			if (Biggest < 1e-9) break;
		}
	}
}

bool FDTHShapeFit::Init(FDTHFaceEvaluator& InEvaluator, TConstArrayView<FBoardCurve> Board, const FDTHFaceRig& Rig, TArray<FString>& OutReport, FString& OutError)
{
	Evaluator = &InEvaluator;
	CurveNames.Reset();
	TMap<FName, int32> Index;
	for (const FBoardCurve& B : Board)
	{
		if (!Index.Contains(B.Curve)) Index.Add(B.Curve, CurveNames.Add(B.Curve));
	}
	const int32 N = CurveNames.Num();
	if (N == 0)
	{
		OutError = TEXT("The character's control rig has no face board.");
		return false;
	}
	Exponent.Init(1, N);
	for (int32 C = 0; C < N; ++C)
	{
		const int32 R = Rig.FindCurve(CurveNames[C]);
		if (R != INDEX_NONE && Rig.Exponent.IsValidIndex(R)) Exponent[C] = Rig.Exponent[R];
	}
	Opposite.Reset();
	Opposite.SetNum(N);
	TMap<TPair<FName, int32>, TArray<TPair<int32, float>>> Axes;
	for (const FBoardCurve& B : Board) Axes.FindOrAdd({B.Control, B.Axis}).Add({Index[B.Curve], B.Sign});
	for (const TPair<TPair<FName, int32>, TArray<TPair<int32, float>>>& A : Axes)
	{
		for (const TPair<int32, float>& X : A.Value)
			for (const TPair<int32, float>& Y : A.Value)
				if (X.Value * Y.Value < 0.0f) Opposite[X.Key].AddUnique(Y.Key);
	}
	// curves one slider side drives together (e.g. jawChinCompress L and R on one chin control)
	Tied.Reset();
	Tied.SetNum(N);
	for (int32 K = 0; K < N; ++K) Tied[K].Add(K);
	for (const TPair<TPair<FName, int32>, TArray<TPair<int32, float>>>& A : Axes)
	{
		for (const TPair<int32, float>& X : A.Value)
			for (const TPair<int32, float>& Y : A.Value)
				if (X.Key != Y.Key && X.Value * Y.Value > 0.0f) Tied[X.Key].AddUnique(Y.Key);
	}

	// The board's response: every curve alone, then the pairs that act together.
	TMap<FName, float> None;
	Evaluator->Evaluate(None, Rest);
	NumPoints = Rest.Num();
	Basis.Reset();
	Basis.SetNum(N);
	TArray<FVector3f> D;
	TArray<double> C;
	C.Init(0.0, N);
	for (int32 K = 0; K < N; ++K)
	{
		C[K] = 1.0;
		Measure(C, D);
		C[K] = 0.0;
		TArray<float>& Col = Basis[K];
		Col.SetNumUninitialized(3 * NumPoints);
		for (int32 P = 0; P < NumPoints; ++P) { Col[3 * P] = D[P].X; Col[3 * P + 1] = D[P].Y; Col[3 * P + 2] = D[P].Z; }
	}

	TSet<TPair<int32, int32>> Tried;
	auto AddCandidate = [&](int32 A, int32 B)
	{
		if (A == B || A == INDEX_NONE || B == INDEX_NONE) return;
		Tried.Add({FMath::Min(A, B), FMath::Max(A, B)});
	};
	for (const FDTHFaceRig::FPair& P : Rig.Pairs)
	{
		const int32* A = Index.Find(Rig.Curves[P.A]);
		const int32* B = Index.Find(Rig.Curves[P.B]);
		if (A && B) AddCandidate(*A, *B);
	}
	const FString Prefix = TEXT("CTRL_expressions_");
	TArray<int32> Tongue;
	for (int32 K = 0; K < N; ++K)
	{
		const FString Name = CurveNames[K].ToString();
		if (Name.Contains(TEXT("tongue"))) Tongue.Add(K);
	}
	for (const TCHAR* Modifier : {TEXT("eyeFaceScrunchL"), TEXT("eyeFaceScrunchR"), TEXT("jawOpen")})
	{
		if (const int32* M = Index.Find(FName(*(Prefix + Modifier))))
		{
			for (int32 K = 0; K < N; ++K) AddCandidate(*M, K);
		}
	}
	for (int32 I = 0; I < Tongue.Num(); ++I)
		for (int32 J = I + 1; J < Tongue.Num(); ++J) AddCandidate(Tongue[I], Tongue[J]);

	Pairs.Reset();
	for (const TPair<int32, int32>& P : Tried)
	{
		if (Opposite[P.Key].Contains(P.Value)) continue;   // never both ends of one slider
		C[P.Key] = 1.0; C[P.Value] = 1.0;
		Measure(C, D);
		C[P.Key] = 0.0; C[P.Value] = 0.0;
		TArray<float> Col;
		Col.SetNumUninitialized(3 * NumPoints);
		float Largest = 0.0f;
		const TArray<float>& A = Basis[P.Key];
		const TArray<float>& B = Basis[P.Value];
		for (int32 Q = 0; Q < NumPoints; ++Q)
		{
			const FVector3f I(D[Q].X - A[3 * Q] - B[3 * Q], D[Q].Y - A[3 * Q + 1] - B[3 * Q + 1], D[Q].Z - A[3 * Q + 2] - B[3 * Q + 2]);
			Col[3 * Q] = I.X; Col[3 * Q + 1] = I.Y; Col[3 * Q + 2] = I.Z;
			Largest = FMath::Max(Largest, I.Size());
		}
		if (Largest > PairThreshold)
		{
			Pairs.Add({P.Key, P.Value});
			Basis.Add(MoveTemp(Col));
		}
	}

	const int32 Fe = Basis.Num();
	Gram.SetNumZeroed(Fe * Fe);
	ParallelFor(Fe, [&](int32 I)
	{
		for (int32 J = I; J < Fe; ++J)
		{
			const double G = Dot(Basis[I], Basis[J]);
			Gram[I * Fe + J] = G;
			Gram[J * Fe + I] = G;
		}
	});
	OutReport.Add(FString::Printf(TEXT("Face board measured on %d face points: %d curves, %d of %d tried curve pairs act together."),
		NumPoints, N, Pairs.Num(), Tried.Num()));
	return true;
}

void FDTHShapeFit::Measure(TConstArrayView<double> C, TArray<FVector3f>& OutDisplacement)
{
	TMap<FName, float> Curves;
	for (int32 K = 0; K < C.Num(); ++K)
	{
		if (C[K] > 1e-6) Curves.Add(CurveNames[K], static_cast<float>(C[K]));
	}
	Evaluator->Evaluate(Curves, OutDisplacement);
	for (int32 P = 0; P < OutDisplacement.Num(); ++P) OutDisplacement[P] -= Rest[P];
}

void FDTHShapeFit::Features(TConstArrayView<double> C, TArray<double>& Out) const
{
	const int32 N = CurveNames.Num();
	Out.SetNumUninitialized(N + Pairs.Num());
	for (int32 K = 0; K < N; ++K) Out[K] = F(K, C[K]);
	for (int32 P = 0; P < Pairs.Num(); ++P) Out[N + P] = F(Pairs[P].A, C[Pairs[P].A]) * F(Pairs[P].B, C[Pairs[P].B]);
}

void FDTHShapeFit::Tie(TArray<double>& C) const
{
	for (int32 K = 0; K < C.Num(); ++K)
	{
		if (Tied[K].Num() < 2 || FMath::Min(Tied[K]) != K) continue;   // each group once, from its lowest curve
		double Mean = 0.0;
		for (const int32 T : Tied[K]) Mean += C[T];
		Mean /= Tied[K].Num();
		for (const int32 T : Tied[K]) C[T] = Mean;
	}
}

void FDTHShapeFit::OneSide(TArray<double>& C) const
{
	if (!bOneSideRule) return;
	for (int32 K = 0; K < C.Num(); ++K)
	{
		if (C[K] <= 0.0) continue;
		for (const int32 O : Opposite[K])
		{
			if (C[O] > 0.0)
			{
				if (C[O] > C[K]) { C[K] = 0.0; break; }
				C[O] = 0.0;
			}
		}
	}
}

void FDTHShapeFit::ModelFit(const TArray<double>& BT, double TT, const TArray<bool>& InFree, TArray<double>& C) const
{
	const int32 N = CurveNames.Num();
	const int32 Fe = Basis.Num();
	TArray<bool> Free = InFree;
	TArray<double> Phi, GPhi, GJ, H, G;
	for (int32 Round = 0; Round < 8; ++Round)
	{
		for (int32 Step2 = 0; Step2 < MaxModelSteps; ++Step2)
		{
			Features(C, Phi);
			// J: d(feature)/d(curve), sparse: the curve's own feature and the pairs it is in
			TArray<TArray<TPair<int32, double>>> J;
			J.SetNum(N);
			for (int32 K = 0; K < N; ++K) J[K].Add({K, DF(K, C[K])});
			for (int32 P = 0; P < Pairs.Num(); ++P)
			{
				const int32 A = Pairs[P].A, B = Pairs[P].B;
				J[A].Add({N + P, DF(A, C[A]) * F(B, C[B])});
				J[B].Add({N + P, DF(B, C[B]) * F(A, C[A])});
			}
			GPhi.Init(0.0, Fe);
			for (int32 I = 0; I < Fe; ++I) { double S = 0.0; for (int32 K = 0; K < Fe; ++K) S += Gram[I * Fe + K] * Phi[K]; GPhi[I] = S; }
			GJ.Init(0.0, Fe * N);
			for (int32 K = 0; K < N; ++K)
				for (const TPair<int32, double>& E : J[K])
					for (int32 I = 0; I < Fe; ++I) GJ[I * N + K] += Gram[I * Fe + E.Key] * E.Value;
			H.Init(0.0, N * N);
			G.Init(0.0, N);
			for (int32 K = 0; K < N; ++K)
			{
				for (const TPair<int32, double>& E : J[K])
				{
					G[K] += E.Value * (BT[E.Key] - GPhi[E.Key]);
					for (int32 L = 0; L < N; ++L) H[K * N + L] += E.Value * GJ[E.Key * N + L];
				}
			}
			for (int32 K = 0; K < N; ++K) { double S = 0.0; for (int32 L = 0; L < N; ++L) S += H[K * N + L] * C[L]; G[K] += S; }
			TArray<double> X = C;
			BoundedQuadratic(H, G, Free, X, MaxValue, 150);
			Tie(X);
			double Moved = 0.0;
			for (int32 K = 0; K < N; ++K) Moved = FMath::Max(Moved, FMath::Abs(X[K] - C[K]));
			C = X;
			if (Moved < 1e-5) break;
		}
		// one side per control axis: lock the smaller side at 0 and fit again
		bool bClash = false;
		if (!bOneSideRule) break;
		for (int32 K = 0; K < N; ++K)
		{
			for (const int32 O : Opposite[K])
			{
				if (C[K] > 1e-6 && C[O] > 1e-6)
				{
					bClash = true;
					const int32 Lose = C[K] >= C[O] ? O : K;
					Free[Lose] = false;
					C[Lose] = 0.0;
				}
			}
		}
		if (!bClash) break;
	}
	Tie(C);
	OneSide(C);
}

FDTHShapeFit::FResult FDTHShapeFit::Fit(TConstArrayView<FVector3f> Target, const FOptions& Options)
{
	const double Start = FPlatformTime::Seconds();
	const int32 EvaluationsAtStart = Evaluator->Evaluations;
	MaxValue = Options.MaxValue;
	bOneSideRule = Options.bOneSide;
	const int32 N = CurveNames.Num();
	const int32 Fe = Basis.Num();
	FResult Result;

	TArray<float> T;
	T.SetNumUninitialized(3 * NumPoints);
	for (int32 P = 0; P < NumPoints; ++P) { T[3 * P] = Target[P].X; T[3 * P + 1] = Target[P].Y; T[3 * P + 2] = Target[P].Z; }
	const double TT = Dot(T, T);
	for (const FVector3f& P : Target) Result.Largest = FMath::Max(Result.Largest, static_cast<double>(P.Size()));
	const double TNorm = FMath::Sqrt(FMath::Max(TT, 1e-12));
	TArray<double> BT;
	BT.SetNumUninitialized(Fe);
	ParallelFor(Fe, [&](int32 I) { BT[I] = Dot(Basis[I], T); });

	TArray<bool> Free;
	Free.SetNum(N);
	for (int32 K = 0; K < N; ++K) Free[K] = !Options.Fixed.Contains(CurveNames[K]);

	// Start: the measured board model. A small start value gives the squared curves and the pairs a slope.
	TArray<double> C;
	C.Init(0.0, N);
	for (int32 K = 0; K < N; ++K) if (Free[K]) C[K] = 0.02;
	if (Options.Start.Num() == N) { C = Options.Start; Tie(C); OneSide(C); }
	else ModelFit(BT, TT, Free, C);
	Result.ModelSeconds = FPlatformTime::Seconds() - Start;
	{
		TArray<double> Phi;
		Features(C, Phi);
		double E = TT;
		for (int32 I = 0; I < Fe; ++I)
		{
			E -= 2.0 * Phi[I] * BT[I];
			for (int32 K = 0; K < Fe; ++K) E += Phi[I] * Gram[I * Fe + K] * Phi[K];
		}
		Result.ModelMatch = 1.0 - FMath::Sqrt(FMath::Max(E, 0.0)) / TNorm;
	}

	// Then every step measured on the real rig.
	TArray<FVector3f> Cur, Trial;
	Measure(C, Cur);
	auto Residual = [&](const TArray<FVector3f>& U, TArray<float>& R)
	{
		R.SetNumUninitialized(3 * NumPoints);
		double S = 0.0;
		for (int32 P = 0; P < NumPoints; ++P)
		{
			R[3 * P] = T[3 * P] - U[P].X; R[3 * P + 1] = T[3 * P + 1] - U[P].Y; R[3 * P + 2] = T[3 * P + 2] - U[P].Z;
			S += static_cast<double>(R[3 * P]) * R[3 * P] + static_cast<double>(R[3 * P + 1]) * R[3 * P + 1] + static_cast<double>(R[3 * P + 2]) * R[3 * P + 2];
		}
		return FMath::Sqrt(S);
	};
	TArray<float> R, RT;
	double Err = Residual(Cur, R);

	double LastStep = 0.0;
	// a step is only started if one as long as the last still fits in the budget
	while (FPlatformTime::Seconds() - Start + LastStep < Options.TimeBudget)
	{
		const double StepStart = FPlatformTime::Seconds();
		++Result.Iterations;
		// Curves whose slope is measured: those in use, and the inactive ones the remaining error pulls on most.
		TArray<bool> Allowed;
		Allowed.SetNum(N);
		for (int32 K = 0; K < N; ++K)
		{
			bool bBlocked = !Free[K];
			if (bOneSideRule) for (const int32 O : Opposite[K]) bBlocked |= C[O] > 0.0;
			Allowed[K] = !bBlocked;
		}
		TArray<int32> S;
		TArray<TPair<double, int32>> Pull;
		for (int32 K = 0; K < N; ++K)
		{
			if (!Allowed[K] || FMath::Min(Tied[K]) != K) continue;   // a tied group moves as one, from its lowest curve
			if (C[K] > 0.0) { S.Add(K); continue; }
			double Pull2 = 0.0, Norm2 = 0.0;
			for (const int32 T2 : Tied[K]) { Pull2 += Dot(Basis[T2], R); Norm2 += Gram[T2 * Fe + T2]; }
			const double P = Pull2 / FMath::Sqrt(FMath::Max(Norm2, 1e-12));
			if (P > 0.0) Pull.Add({P, K});
		}
		Pull.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key > B.Key; });
		for (int32 I = 0; I < FMath::Min(Candidates, Pull.Num()); ++I) S.Add(Pull[I].Value);
		if (S.IsEmpty()) break;

		const int32 M = S.Num();
		TArray<TArray<float>> Jc;
		Jc.SetNum(M);
		for (int32 I = 0; I < M; ++I)
		{
			const int32 K = S[I];
			const double H = C[K] + Step <= MaxValue ? Step : -Step;
			TArray<double> CS = C;
			for (const int32 T2 : Tied[K]) CS[T2] += H;
			Measure(CS, Trial);
			Jc[I].SetNumUninitialized(3 * NumPoints);
			for (int32 P = 0; P < NumPoints; ++P)
			{
				Jc[I][3 * P] = (Trial[P].X - Cur[P].X) / H; Jc[I][3 * P + 1] = (Trial[P].Y - Cur[P].Y) / H; Jc[I][3 * P + 2] = (Trial[P].Z - Cur[P].Z) / H;
			}
		}
		TArray<double> HS, GS, XS;
		HS.SetNumZeroed(M * M);
		GS.SetNumZeroed(M);
		XS.SetNum(M);
		ParallelFor(M, [&](int32 I)
		{
			for (int32 J = I; J < M; ++J) { const double V = Dot(Jc[I], Jc[J]); HS[I * M + J] = V; HS[J * M + I] = V; }
			GS[I] = Dot(Jc[I], R);
		});
		for (int32 I = 0; I < M; ++I)
		{
			XS[I] = C[S[I]];
			for (int32 J = 0; J < M; ++J) GS[I] += HS[I * M + J] * C[S[J]];
		}
		TArray<bool> FreeS;
		FreeS.Init(true, M);
		double MeanDiagonal = 0.0;
		for (int32 I = 0; I < M; ++I) MeanDiagonal += HS[I * M + I] / M;

		// Damped steps (Levenberg-Marquardt): the undamped step first, then shorter, more cautious ones; each one measured.
		bool bAccepted = false;
		double Gain = 0.0;
		for (const double Damping : {0.0, 1e-3, 1e-2, 1e-1, 1.0})
		{
			TArray<double> HD = HS, GD = GS, XD = XS;
			const double L = Damping * MeanDiagonal;
			for (int32 I = 0; I < M; ++I) { HD[I * M + I] += L; GD[I] += L * C[S[I]]; }
			BoundedQuadratic(HD, GD, FreeS, XD, MaxValue);
			TArray<double> CT = C;
			for (int32 I = 0; I < M; ++I) for (const int32 T2 : Tied[S[I]]) CT[T2] = XD[I];
			OneSide(CT);
			Measure(CT, Trial);
			const double E = Residual(Trial, RT);
			if (E < Err - 1e-9)
			{
				Gain = (Err - E) / TNorm;
				C = CT; Cur = Trial; R = RT; Err = E;
				bAccepted = true;
				break;
			}
		}
		LastStep = FPlatformTime::Seconds() - StepStart;
		if (!bAccepted || Gain < 2e-5) break;
	}

	Result.Values.SetNum(N);
	for (int32 K = 0; K < N; ++K) Result.Values[K] = C[K] > 1e-5 ? static_cast<float>(C[K]) : 0.0f;
	Result.Match = 1.0 - Err / TNorm;
	Result.Evaluations = Evaluator->Evaluations - EvaluationsAtStart;
	Result.Seconds = FPlatformTime::Seconds() - Start;
	return Result;
}
