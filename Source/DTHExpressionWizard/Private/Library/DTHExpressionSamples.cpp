#include "Library/DTHExpressionSamples.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	void ReadNumbers(const TSharedPtr<FJsonObject>& Object, TMap<FString, double>& Out)
	{
		if (!Object.IsValid())
		{
			return;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
		{
			Out.Add(Pair.Key, Pair.Value->AsNumber());
		}
	}

	void ReadState(const TSharedPtr<FJsonObject>& Object, FDTHDazState& Out)
	{
		if (!Object.IsValid())
		{
			return;
		}
		const TSharedPtr<FJsonObject>* Morphs = nullptr;
		const TSharedPtr<FJsonObject>* Bones = nullptr;
		if (Object->TryGetObjectField(TEXT("morphs"), Morphs)) ReadNumbers(*Morphs, Out.Morphs);
		if (Object->TryGetObjectField(TEXT("bones"), Bones)) ReadNumbers(*Bones, Out.Bones);
		const TArray<TSharedPtr<FJsonValue>>* Keyed = nullptr;
		if (Object->TryGetArrayField(TEXT("keyed"), Keyed))
		{
			for (const TSharedPtr<FJsonValue>& K : *Keyed) Out.Keyed.Add(K->AsString());
		}
		Object->TryGetNumberField(TEXT("base"), Out.Base);
		const TArray<TSharedPtr<FJsonValue>>* Sculpted = nullptr;
		if (Object->TryGetArrayField(TEXT("sculpted"), Sculpted))
		{
			for (const TSharedPtr<FJsonValue>& S : *Sculpted) Out.Sculpted.Add(S->AsString());
		}
	}

	/** Next whitespace-separated integer of a geometry line; false at its end. */
	bool NextInt(const TCHAR*& P, int64& Out)
	{
		while (*P == ' ' || *P == '\t' || *P == '\r') ++P;
		if (!*P || *P == '\n') return false;
		bool bNegative = false;
		if (*P == '-') { bNegative = true; ++P; }
		int64 V = 0;
		while (*P >= '0' && *P <= '9') V = V * 10 + (*P++ - '0');
		Out = bNegative ? -V : V;
		return true;
	}

	/** Reads the face region and the per-preset face displacements (geo_*.txt next to the samples file). */
	bool ReadFace(const TSharedPtr<FJsonObject>& Root, const FString& Folder, FDTHExpressionSamples& Out, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Face = nullptr;
		if (!Root->TryGetObjectField(TEXT("face"), Face)) return true;
		const double Unit = (*Face)->GetNumberField(TEXT("unit"));
		for (const TSharedPtr<FJsonValue>& V : (*Face)->GetArrayField(TEXT("figures")))
		{
			const TSharedPtr<FJsonObject>& F = V->AsObject();
			FDTHFaceFigure& Figure = Out.Face.AddDefaulted_GetRef();
			Figure.Name = F->GetStringField(TEXT("name"));
			for (const TSharedPtr<FJsonValue>& I : F->GetArrayField(TEXT("vertices"))) Figure.Vertices.Add(static_cast<int32>(I->AsNumber()));
			for (const TSharedPtr<FJsonValue>& Q : F->GetArrayField(TEXT("facets")))
			{
				const TArray<TSharedPtr<FJsonValue>>& A = Q->AsArray();
				Figure.Facets.Add(FIntVector4(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber(), A[3]->AsNumber()));
			}
			const TArray<TSharedPtr<FJsonValue>>& R = F->GetArrayField(TEXT("rest"));
			for (int32 I = 0; I + 2 < R.Num(); I += 3) Figure.Rest.Add(FVector3f(R[I]->AsNumber(), R[I + 1]->AsNumber(), R[I + 2]->AsNumber()));
		}
		Out.FaceDisplacement.SetNum(Out.Expressions.Num());
		for (const TSharedPtr<FJsonValue>& FileValue : Root->GetArrayField(TEXT("geometryFiles")))
		{
			FString Text;
			const FString Path = Folder / FileValue->AsString();
			if (!FFileHelper::LoadFileToString(Text, *Path))
			{
				OutError = FString::Printf(TEXT("Cannot read the face geometry %s."), *Path);
				return false;
			}
			const TCHAR* P = *Text;
			while (*P)
			{
				int64 Index = -1, FigureIndex = -1, Count = 0;
				if (NextInt(P, Index) && NextInt(P, FigureIndex) && NextInt(P, Count)
					&& Out.FaceDisplacement.IsValidIndex(Index) && Out.Face.IsValidIndex(FigureIndex))
				{
					TArray<TArray<FVector3f>>& PerFigure = Out.FaceDisplacement[Index];
					PerFigure.SetNum(Out.Face.Num());
					TArray<FVector3f>& D = PerFigure[FigureIndex];
					D.Init(FVector3f::ZeroVector, Out.Face[FigureIndex].Vertices.Num());
					for (int64 K = 0; K < Count; ++K)
					{
						int64 Vi, X, Y, Z;
						if (!NextInt(P, Vi) || !NextInt(P, X) || !NextInt(P, Y) || !NextInt(P, Z)) break;
						if (D.IsValidIndex(Vi)) D[Vi] = FVector3f(X * Unit, Y * Unit, Z * Unit);
					}
				}
				while (*P && *P != '\n') ++P;
				if (*P == '\n') ++P;
			}
		}
		return true;
	}
}

bool FDTHExpressionSamples::Load(const FString& File, FString& OutError)
{
	*this = FDTHExpressionSamples();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *File))
	{
		OutError = FString::Printf(TEXT("Cannot read %s."), *File);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()
		|| Root->GetStringField(TEXT("format")) != TEXT("DTH Expression Samples"))
	{
		OutError = FString::Printf(TEXT("%s is not a DTH expression samples file (written by DTH_SampleExpressions.dsa)."), *File);
		return false;
	}

	SampledAt = Root->GetStringField(TEXT("sampledAt"));
	DazVersion = Root->GetStringField(TEXT("dazVersion"));
	ReadState(Root->GetObjectField(TEXT("rest")), Rest);

	const TSharedPtr<FJsonObject>* Rom = nullptr;
	const TSharedPtr<FJsonObject>* Frames = nullptr;
	if (Root->TryGetObjectField(TEXT("rom"), Rom) && (*Rom)->TryGetObjectField(TEXT("frames"), Frames))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Frames)->Values)
		{
			ReadState(Pair.Value->AsObject(), RomFrames.Add(FCString::Atoi(*Pair.Key)));
		}
	}
	if (RomFrames.IsEmpty())
	{
		OutError = FString::Printf(TEXT("%s has no ROM frames: the sampler's ROM step did not run."), *File);
		return false;
	}

	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("expressions")))
	{
		const TSharedPtr<FJsonObject>& O = V->AsObject();
		FDTHDazExpression& E = Expressions.AddDefaulted_GetRef();
		E.Pack = O->GetStringField(TEXT("pack"));
		E.Label = O->GetStringField(TEXT("label"));
		E.File = O->GetStringField(TEXT("file"));
		O->TryGetStringField(TEXT("icon"), E.Icon);
		O->TryGetStringField(TEXT("reason"), E.Reason);
		E.bLoaded = O->GetBoolField(TEXT("loaded"));
		const TSharedPtr<FJsonObject>* State = nullptr;
		if (E.bLoaded && O->TryGetObjectField(TEXT("state"), State))
		{
			ReadState(*State, E.State);
		}
	}
	return ReadFace(Root, FPaths::GetPath(File), *this, OutError);
}
