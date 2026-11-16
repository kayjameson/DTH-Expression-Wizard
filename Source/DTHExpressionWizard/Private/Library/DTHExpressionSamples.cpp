#include "Library/DTHExpressionSamples.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ReadJson(const FString& Path, TSharedPtr<FJsonValue>& Out)
	{
		FString Text;
		return FFileHelper::LoadFileToString(Text, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
	}

	/** A field that is either the object itself or the name of a JSON file next to the manifest holding it. */
	TSharedPtr<FJsonValue> FieldOrFile(const TSharedPtr<FJsonObject>& Root, const TCHAR* Name, const FString& Folder)
	{
		TSharedPtr<FJsonValue> Value = Root->TryGetField(Name);
		FString FileName;
		if (Value.IsValid() && Value->TryGetString(FileName))
		{
			if (FileName.IsEmpty() || !ReadJson(Folder / FileName, Value)) Value.Reset();
		}
		return Value;
	}

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

	FDTHDazExpression ReadExpression(const TSharedPtr<FJsonObject>& O)
	{
		FDTHDazExpression E;
		if (!O.IsValid()) return E;
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
		return E;
	}

}

bool FDTHExpressionSamples::Load(const FString& InFile, FString& OutError)
{
	*this = FDTHExpressionSamples();
	TSharedPtr<FJsonValue> RootValue;
	const TSharedPtr<FJsonObject> Root = ReadJson(InFile, RootValue) ? RootValue->AsObject() : nullptr;
	if (!Root.IsValid() || Root->GetStringField(TEXT("format")) != TEXT("DTH Expression Samples"))
	{
		OutError = FString::Printf(TEXT("%s is not a DTH expression samples file (written by DTH_SampleExpressions.dsa)."), *InFile);
		return false;
	}
	File = InFile;
	const FString Folder = FPaths::GetPath(File);
	Version = static_cast<int32>(Root->GetNumberField(TEXT("version")));
	SampledAt = Root->GetStringField(TEXT("sampledAt"));
	DazVersion = Root->GetStringField(TEXT("dazVersion"));
	ReadState(Root->GetObjectField(TEXT("rest")), Rest);

	const TSharedPtr<FJsonValue> Rom = FieldOrFile(Root, TEXT("rom"), Folder);
	const TSharedPtr<FJsonObject>* Frames = nullptr;
	if (Rom.IsValid() && Rom->AsObject().IsValid() && Rom->AsObject()->TryGetObjectField(TEXT("frames"), Frames))
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

	const TSharedPtr<FJsonValue> FaceValue = FieldOrFile(Root, TEXT("face"), Folder);
	if (FaceValue.IsValid() && FaceValue->AsObject().IsValid())
	{
		DTHFaceRegion::ReadFace(FaceValue->AsObject(), Face, GeoUnit);
	}

	if (Version >= 3)
	{
		for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("chunks")))
		{
			const TSharedPtr<FJsonObject>& C = V->AsObject();
			Chunks.Add({C->GetStringField(TEXT("samples")), C->GetStringField(TEXT("geometry"))});
		}
		NumExpressions = static_cast<int32>(Root->GetNumberField(TEXT("presets")));
	}
	else
	{
		NumExpressions = Root->GetArrayField(TEXT("expressions")).Num();
	}
	return true;
}

bool FDTHExpressionSamples::ReadExpressions(TFunctionRef<bool(FDTHDazExpression&&, TArray<TArray<FVector3f>>&&)> Visit, FString& OutError) const
{
	const FString Folder = FPaths::GetPath(File);
	if (Version >= 3)
	{
		for (const FChunk& Chunk : Chunks)
		{
			TSharedPtr<FJsonValue> Value;
			if (!ReadJson(Folder / Chunk.Samples, Value))
			{
				OutError = FString::Printf(TEXT("Cannot read the samples %s."), *(Folder / Chunk.Samples));
				return false;
			}
			const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
			TArray<TArray<TArray<FVector3f>>> Displacement;
			Displacement.SetNum(Items.Num());
			if (HasFace() && !DTHFaceRegion::ReadGeometry(Folder / Chunk.Geometry, Face, GeoUnit, 0, Displacement, OutError)) return false;
			for (int32 I = 0; I < Items.Num(); ++I)
			{
				if (!Visit(ReadExpression(Items[I]->AsObject()), MoveTemp(Displacement[I]))) return true;
			}
		}
		return true;
	}

	// Version 2 and earlier: every expression in the manifest, the face geometry in files indexed over all of them.
	TSharedPtr<FJsonValue> RootValue;
	if (!ReadJson(File, RootValue) || !RootValue->AsObject().IsValid())
	{
		OutError = FString::Printf(TEXT("Cannot read %s."), *File);
		return false;
	}
	const TSharedPtr<FJsonObject> Root = RootValue->AsObject();
	const TArray<TSharedPtr<FJsonValue>>& Items = Root->GetArrayField(TEXT("expressions"));
	TArray<TArray<TArray<FVector3f>>> Displacement;
	Displacement.SetNum(Items.Num());
	const TArray<TSharedPtr<FJsonValue>>* GeometryFiles = nullptr;
	if (HasFace() && Root->TryGetArrayField(TEXT("geometryFiles"), GeometryFiles))
	{
		for (const TSharedPtr<FJsonValue>& G : *GeometryFiles)
		{
			if (!DTHFaceRegion::ReadGeometry(Folder / G->AsString(), Face, GeoUnit, 0, Displacement, OutError)) return false;
		}
	}
	for (int32 I = 0; I < Items.Num(); ++I)
	{
		if (!Visit(ReadExpression(Items[I]->AsObject()), MoveTemp(Displacement[I]))) return true;
	}
	return true;
}
