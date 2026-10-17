#include "Library/DTHExpressionSamples.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
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
	return true;
}
