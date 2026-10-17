#include "UI/SDTHExpressionWizard.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetData.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "DTHExpressionWizardModule.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDetailsView.h"
#include "Library/DTHExpressionLibrary.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Mixer/DTHMixer.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "Settings/DTHExpressionSettings.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "UI/SDTHFacePreview.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SDTHExpressionWizard"

TWeakPtr<SDTHExpressionWizard> SDTHExpressionWizard::Instance;

namespace
{
	FString SamplerScript()
	{
		return FDTHExpressionWizardModule::ResourcesDir() / TEXT("Daz") / TEXT("DTH_SampleExpressions.dsa");
	}

	/** "People/Genesis 9/Expressions/<Vendor>/<Pack>" -> "<Vendor>/<Pack>". */
	FString ShortPack(const FString& Pack)
	{
		FString Out = Pack;
		Out.RemoveFromStart(TEXT("People/Genesis 9/Expressions/"));
		Out.RemoveFromStart(TEXT("People/Genesis 9/Poses/"));
		return Out;
	}

	bool SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetPackage();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, Asset, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}
}

bool SDTHExpressionWizard::ShowPage(const FString& Name)
{
	static const TPair<const TCHAR*, EPage> PageNames[] = {
		{ TEXT("Settings"), EPage::Settings }, { TEXT("Build"), EPage::Build }, { TEXT("Mixer"), EPage::Mixer } };
	for (const auto& Page : PageNames)
	{
		if (Name.Equals(Page.Key, ESearchCase::IgnoreCase))
		{
			CurrentPage = Page.Value;
			return true;
		}
	}
	return false;
}

void SDTHExpressionWizard::Construct(const FArguments& InArgs)
{
	Instance = SharedThis(this);
	LoadLibrary();
	if (!Library.IsValid())
	{
		CurrentPage = EPage::Settings;
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(0.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 8.f, 8.f, 4.f)
			[
				MakeStepBar()
			]

			+ SVerticalBox::Slot().FillHeight(1.f).Padding(8.f, 4.f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return static_cast<int32>(CurrentPage); })
				+ SWidgetSwitcher::Slot()[MakeSettingsPage()]
				+ SWidgetSwitcher::Slot()[MakeBuildPage()]
				+ SWidgetSwitcher::Slot()[MakeMixerPage()]
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSeparator).Thickness(1.f)
			]

			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeActionBar()
			]
		]
	];

	Reload();
}

void SDTHExpressionWizard::Reload()
{
	LoadLibrary();
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(GetDefault<UDTHExpressionSettings>()->PreviewMesh.TryLoad()))
	{
		SetCharacter(Mesh);
	}
}

TSharedRef<SWidget> SDTHExpressionWizard::MakeStepBar()
{
	TSharedRef<SSegmentedControl<int32>> Bar = SNew(SSegmentedControl<int32>)
		.Value_Lambda([this]() { return static_cast<int32>(CurrentPage); })
		.OnValueChanged_Lambda([this](int32 NewValue) { CurrentPage = static_cast<EPage>(NewValue); });
	const FText Labels[] = { LOCTEXT("StepSettings", "Settings"), LOCTEXT("StepBuild", "Build Library"), LOCTEXT("StepMixer", "Mixer") };
	static_assert(UE_ARRAY_COUNT(Labels) == static_cast<int32>(EPage::Count), "One label per page");
	for (int32 Index = 0; Index < static_cast<int32>(EPage::Count); ++Index)
	{
		Bar->AddSlot(Index).Text(Labels[Index]);
	}
	return Bar;
}

TSharedRef<SWidget> SDTHExpressionWizard::MakeSection(const FText& Title, const TSharedRef<SWidget>& Content)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
			.Padding(FMargin(8.f, 4.f))
			[
				SNew(STextBlock).Font(FAppStyle::GetFontStyle("NormalFontBold")).Text(Title)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(FMargin(8.f, 6.f))
			[
				Content
			]
		];
}

// --- Settings ---------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SDTHExpressionWizard::MakeSettingsPage()
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Args.bAllowSearch = false;
	SettingsView = PropertyEditor.CreateDetailView(Args);
	SettingsView->SetObject(GetMutableDefault<UDTHExpressionSettings>());
	SettingsView->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&)
	{
		GetMutableDefault<UDTHExpressionSettings>()->SaveConfig();
		LoadLibrary();
	});

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			MakeSection(LOCTEXT("DazSection", "Daz Studio Sampler"),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT("DazHelp",
						"The library is built from what Daz Studio does with your expression presets. In Daz Studio, open this script "
						"(File > Open, or double-click it). It loads a fresh Genesis 9, records DTH's face ROM and every Genesis 9 expression "
						"preset in your content library (any Expressions folder under People/), and writes DTH_ExpressionSamples.json to "
						"Documents/DTH Expression Wizard. Nothing is saved in Daz. Then set Daz Samples File and Character below and build the library."))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(SamplerScript()))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("CopyScript", "Copy Path"))
						.OnClicked_Lambda([]() { FPlatformApplicationMisc::ClipboardCopy(*SamplerScript()); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ShowScript", "Show in Explorer"))
						.OnClicked_Lambda([]() { FPlatformProcess::ExploreFolder(*SamplerScript()); return FReply::Handled(); })
					]
				])
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(4.f)
			[
				SettingsView.ToSharedRef()
			]
		];
}

// --- Build --------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SDTHExpressionWizard::MakeBuildPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			MakeSection(LOCTEXT("LibrarySection", "Library"),
				SNew(STextBlock).AutoWrapText(true).Text(this, &SDTHExpressionWizard::GetLibrarySummary))
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			MakeSection(LOCTEXT("ReportSection", "Report"),
				SAssignNew(MessageList, SListView<TSharedPtr<FMessage>>)
				.ListItemsSource(&Messages)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SDTHExpressionWizard::MakeMessageRow))
		];
}

TSharedRef<ITableRow> SDTHExpressionWizard::MakeMessageRow(TSharedPtr<FMessage> Message, const TSharedRef<STableViewBase>& Owner)
{
	const TCHAR* IconName = TEXT("Icons.Info");
	if (Message->Severity == FMessage::ESeverity::Warning) IconName = TEXT("Icons.WarningWithColor");
	else if (Message->Severity == FMessage::ESeverity::Error) IconName = TEXT("Icons.ErrorWithColor");
	return SNew(STableRow<TSharedPtr<FMessage>>, Owner)
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 1.f, 6.f, 0.f)
			[
				SNew(SImage).Image(FAppStyle::GetBrush(IconName))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Message->Text))
			]
		];
}

FText SDTHExpressionWizard::GetLibrarySummary() const
{
	const UDTHExpressionSettings* Settings = GetDefault<UDTHExpressionSettings>();
	const UDTHExpressionLibrary* Lib = Library.Get();
	FString Text = FString::Printf(TEXT("Daz samples: %s\nLibrary asset: %s\n"),
		Settings->SamplesFile.FilePath.IsEmpty() ? TEXT("(not set: see Settings)") : *Settings->SamplesFile.FilePath, *Settings->LibraryAsset);
	if (Lib)
	{
		int32 Partial = 0;
		for (const FDTHExpression& E : Lib->Expressions) Partial += E.Missing.IsEmpty() ? 0 : 1;
		Text += FString::Printf(TEXT("Built from samples taken %s: %d expressions (%d without some shapes DTH's face cannot make), %d skipped. Gaze %s."),
			*Lib->SampledAt, Lib->Expressions.Num(), Partial, Lib->Skipped.Num(), Lib->bGazeStripped ? TEXT("stripped") : TEXT("kept"));
	}
	else
	{
		Text += TEXT("Not built yet.");
	}
	return FText::FromString(Text);
}

FReply SDTHExpressionWizard::OnBuild()
{
	const UDTHExpressionSettings* Settings = GetDefault<UDTHExpressionSettings>();
	Messages.Reset();
	TArray<FDTHBuildMessage> Report;
	{
		FScopedSlowTask Task(1.f, LOCTEXT("Building", "Building the expression library..."));
		Task.MakeDialog();
		FDTHLibraryBuild::Build(Settings->SamplesFile.FilePath, Settings->LibraryAsset, Cast<USkeletalMesh>(Settings->PreviewMesh.TryLoad()),
			Settings->bStripGaze, Report);
	}
	for (const FDTHBuildMessage& M : Report)
	{
		Messages.Add(MakeShared<FMessage>(M));
	}
	if (MessageList.IsValid()) MessageList->RequestListRefresh();
	LoadLibrary();
	Status = Library.IsValid() ? TEXT("Library built.") : TEXT("The build failed: see the report.");
	return FReply::Handled();
}

void SDTHExpressionWizard::AddMessage(FMessage::ESeverity Severity, const FString& Text)
{
	Messages.Add(MakeShared<FMessage>(FMessage{Severity, Text}));
	if (MessageList.IsValid()) MessageList->RequestListRefresh();
}

void SDTHExpressionWizard::LoadLibrary()
{
	const FString& Path = GetDefault<UDTHExpressionSettings>()->LibraryAsset;
	Library = Path.IsEmpty() ? nullptr : LoadObject<UDTHExpressionLibrary>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	MixRows.Reset();
	Icons.Reset();
	RefreshExpressionList();
	if (MixList.IsValid()) MixList->RequestListRefresh();
	MixChanged();
}

// --- Mixer ---------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SDTHExpressionWizard::MakeMixerPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.32f)
			[
				MakeSection(LOCTEXT("ExpressionsSection", "Expressions (double-click to add)"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(SSearchBox)
						.HintText(LOCTEXT("SearchHint", "Search expressions or packs"))
						.OnTextChanged_Lambda([this](const FText& Text) { Search = Text.ToString(); RefreshExpressionList(); })
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SAssignNew(ExpressionList, SListView<TSharedPtr<int32>>)
						.ListItemsSource(&ExpressionRows)
						.SelectionMode(ESelectionMode::Single)
						.OnGenerateRow(this, &SDTHExpressionWizard::MakeExpressionRow)
						.OnMouseButtonDoubleClick_Lambda([this](TSharedPtr<int32> Item) { if (Item.IsValid()) AddToMix(*Item); })
					])
			]
			+ SSplitter::Slot().Value(0.30f)
			[
				MakeSection(LOCTEXT("MixSection", "Mix"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SAssignNew(MixList, SListView<TSharedPtr<FMixRow>>)
						.ListItemsSource(&MixRows)
						.SelectionMode(ESelectionMode::None)
						.OnGenerateRow(this, &SDTHExpressionWizard::MakeMixRow)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ClearMix", "Clear Mix"))
						.IsEnabled_Lambda([this]() { return !MixRows.IsEmpty(); })
						.OnClicked_Lambda([this]() { MixRows.Reset(); MixList->RequestListRefresh(); MixChanged(); return FReply::Handled(); })
					])
			]
			+ SSplitter::Slot().Value(0.38f)
			[
				MakeSection(LOCTEXT("PreviewSection", "Preview"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
						[
							SNew(STextBlock).Text(LOCTEXT("CharacterLabel", "Character"))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(SObjectPropertyEntryBox)
							.AllowedClass(USkeletalMesh::StaticClass())
							.DisplayThumbnail(false)
							.ObjectPath_Lambda([this]() { const USkeletalMesh* M = Preview.IsValid() ? Preview->GetCharacter() : nullptr; return M ? M->GetPathName() : FString(); })
							.OnObjectChanged_Lambda([this](const FAssetData& Asset) { SetCharacter(Cast<USkeletalMesh>(Asset.GetAsset())); })
						]
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SAssignNew(Preview, SDTHFacePreview)
					])
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			MakeKeyBar()
		];
}

TSharedRef<SWidget> SDTHExpressionWizard::MakeKeyBar()
{
	return MakeSection(LOCTEXT("KeySection", "Key into a Sequence"),
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(STextBlock).Text(LOCTEXT("SequenceLabel", "Sequence"))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UAnimSequence::StaticClass())
			.DisplayThumbnail(false)
			.ObjectPath_Lambda([this]() { return TargetSequence.IsValid() ? TargetSequence->GetPathName() : FString(); })
			.OnObjectChanged_Lambda([this](const FAssetData& Asset) { TargetSequence = Cast<UAnimSequence>(Asset.GetAsset()); })
			.OnShouldFilterAsset_Lambda([this](const FAssetData& Asset)
			{
				// only sequences on the previewed character's skeleton
				const USkeletalMesh* Mesh = Preview.IsValid() ? Preview->GetCharacter() : nullptr;
				const FString Skeleton = Asset.GetTagValueRef<FString>(TEXT("Skeleton"));
				return Mesh && Mesh->GetSkeleton() && !Skeleton.Contains(Mesh->GetSkeleton()->GetPathName());
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 4.f, 0.f)
		[
			SNew(SBox).WidthOverride(160.f)
			[
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("NewSequenceHint", "New sequence name"))
				.Text_Lambda([this]() { return FText::FromString(NewSequenceName); })
				.OnTextChanged_Lambda([this](const FText& Text) { NewSequenceName = Text.ToString(); })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("NewSequence", "Create"))
			.ToolTipText(LOCTEXT("NewSequenceTip", "Create an empty face sequence for the character's skeleton in the Sequence Folder (Settings) and key into it"))
			.IsEnabled_Lambda([this]() { return !NewSequenceName.IsEmpty() && Preview.IsValid() && Preview->GetCharacter(); })
			.OnClicked(this, &SDTHExpressionWizard::OnNewSequence)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 4.f, 0.f)
		[
			SNew(STextBlock).Text(LOCTEXT("FrameLabel", "Frame"))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(70.f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(0)
				.MaxValue(100000)
				.Value_Lambda([this]() { return KeyFrame; })
				.OnValueChanged_Lambda([this](int32 V) { KeyFrame = V; })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("SaveExpression", "Save Mix as Sequence"))
			.ToolTipText(LOCTEXT("SaveExpressionTip", "Write the current mix as its own one-frame face sequence in the Sequence Folder"))
			.IsEnabled_Lambda([this]() { return !MixRows.IsEmpty() && Preview.IsValid() && Preview->GetCharacter(); })
			.OnClicked(this, &SDTHExpressionWizard::OnSaveExpression)
		]);
}

void SDTHExpressionWizard::RefreshExpressionList()
{
	ExpressionRows.Reset();
	if (const UDTHExpressionLibrary* Lib = Library.Get())
	{
		for (int32 I = 0; I < Lib->Expressions.Num(); ++I)
		{
			const FDTHExpression& E = Lib->Expressions[I];
			if (Search.IsEmpty() || E.Label.Contains(Search) || E.Pack.Contains(Search))
			{
				ExpressionRows.Add(MakeShared<int32>(I));
			}
		}
	}
	if (ExpressionList.IsValid()) ExpressionList->RequestListRefresh();
}

const FSlateBrush* SDTHExpressionWizard::GetIcon(int32 Expression)
{
	if (const TSharedPtr<FSlateDynamicImageBrush>* Found = Icons.Find(Expression))
	{
		return Found->IsValid() ? Found->Get() : FAppStyle::GetBrush("ClassIcon.AnimSequence");
	}
	TSharedPtr<FSlateDynamicImageBrush> Brush;
	const UDTHExpressionLibrary* Lib = Library.Get();
	if (Lib && Lib->Expressions.IsValidIndex(Expression) && FPaths::FileExists(Lib->Expressions[Expression].IconFile))
	{
		Brush = MakeShared<FSlateDynamicImageBrush>(FName(*Lib->Expressions[Expression].IconFile), FVector2D(40.0, 40.0));
	}
	Icons.Add(Expression, Brush);
	return Brush.IsValid() ? Brush.Get() : FAppStyle::GetBrush("ClassIcon.AnimSequence");
}

TSharedRef<ITableRow> SDTHExpressionWizard::MakeExpressionRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner)
{
	const FDTHExpression& E = Library->Expressions[*Item];
	FString Tip = FString::Printf(TEXT("%s\n%d%% of the Daz expression reproduced; up to %.0f%% strength."), *E.SourceFile,
		FMath::RoundToInt(E.Match * 100.0f), E.MaxStrength * 100.0f);
	if (!E.Missing.IsEmpty())
	{
		Tip += TEXT("\nNot reproduced:\n") + FString::Join(E.Missing, TEXT("\n"));
	}
	const int32 Index = *Item;
	return SNew(STableRow<TSharedPtr<int32>>, Owner)
		.Padding(FMargin(2.f))
		.ToolTipText(FText::FromString(Tip))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SBox).WidthOverride(40.f).HeightOverride(40.f)
				[
					SNew(SImage).Image(GetIcon(Index))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(E.Label))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::FromString(FString::Printf(TEXT("%s  |  %d%%"), *ShortPack(E.Pack), FMath::RoundToInt(E.Match * 100.0f))))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("AddTip", "Add to the mix"))
				.OnClicked_Lambda([this, Index]() { AddToMix(Index); return FReply::Handled(); })
				[
					SNew(SImage).Image(FAppStyle::GetBrush("Icons.Plus")).ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		];
}

TSharedRef<ITableRow> SDTHExpressionWizard::MakeMixRow(TSharedPtr<FMixRow> Row, const TSharedRef<STableViewBase>& Owner)
{
	const FDTHExpression& E = Library->Expressions[Row->Expression];
	const float Max = FMath::Max(1.0f, E.MaxStrength);
	return SNew(STableRow<TSharedPtr<FMixRow>>, Owner)
		.Padding(FMargin(2.f, 3.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(E.Label))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("MuteTip", "Mute: leave it in the mix but switched off"))
					.IsChecked_Lambda([Row]() { return Row->bMuted ? ECheckBoxState::Unchecked : ECheckBoxState::Checked; })
					.OnCheckStateChanged_Lambda([this, Row](ECheckBoxState S) { Row->bMuted = S != ECheckBoxState::Checked; MixChanged(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("RemoveTip", "Remove from the mix"))
					.OnClicked_Lambda([this, Row]() { MixRows.Remove(Row); MixList->RequestListRefresh(); MixChanged(); return FReply::Handled(); })
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.Delete")).ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSpinBox<float>)
				.MinValue(0.0f)
				.MaxValue(Max)
				.MinSliderValue(0.0f)
				.MaxSliderValue(Max)
				.Delta(0.01f)
				.ToolTipText(FText::Format(LOCTEXT("StrengthTip", "Strength: 1 is the Daz expression; up to {0} overdrives it as far as every curve stays in range"), FText::AsNumber(Max)))
				.Value_Lambda([Row]() { return Row->Strength; })
				.OnValueChanged_Lambda([this, Row](float V) { Row->Strength = V; MixChanged(); })
			]
		];
}

FString SDTHExpressionWizard::CapturePreview(const FString& File, const FString& View)
{
	FString Error;
	if (!Preview.IsValid() || !Preview->Capture(File, View, Error))
	{
		return TEXT("ERROR: ") + (Error.IsEmpty() ? FString(TEXT("No preview.")) : Error);
	}
	Preview->FrameFace();
	return FString::Printf(TEXT("Saved %s."), *File);
}

FString SDTHExpressionWizard::ReplaceMix(const TArray<TPair<int32, float>>& Mix)
{
	const UDTHExpressionLibrary* Lib = Library.Get();
	if (!Lib)
	{
		return TEXT("ERROR: No expression library loaded.");
	}
	MixRows.Reset();
	for (const TPair<int32, float>& Entry : Mix)
	{
		if (!Lib->Expressions.IsValidIndex(Entry.Key))
		{
			return FString::Printf(TEXT("ERROR: No expression %d (the library has %d)."), Entry.Key, Lib->Expressions.Num());
		}
		TSharedPtr<FMixRow> Row = MakeShared<FMixRow>();
		Row->Expression = Entry.Key;
		Row->Strength = Entry.Value;
		MixRows.Add(Row);
	}
	if (MixList.IsValid()) MixList->RequestListRefresh();
	MixChanged();
	return FString::Printf(TEXT("Mix set: %d expressions."), MixRows.Num());
}

void SDTHExpressionWizard::AddToMix(int32 Expression)
{
	for (const TSharedPtr<FMixRow>& Row : MixRows)
	{
		if (Row->Expression == Expression) return;
	}
	TSharedPtr<FMixRow> Row = MakeShared<FMixRow>();
	Row->Expression = Expression;
	MixRows.Add(Row);
	if (MixList.IsValid()) MixList->RequestListRefresh();
	MixChanged();
}

void SDTHExpressionWizard::MixChanged()
{
	const UDTHExpressionLibrary* Lib = Library.Get();
	if (!Lib || !Preview.IsValid())
	{
		return;
	}
	TArray<FDTHMixEntry> Entries;
	for (const TSharedPtr<FMixRow>& Row : MixRows)
	{
		if (!Row->bMuted) Entries.Add({Row->Expression, Row->Strength});
	}
	TMap<FName, float> Values;
	FDTHMixer::Mix(*Lib, Entries, Values);
	Preview->SetValues(Values);
}

void SDTHExpressionWizard::SetCharacter(USkeletalMesh* Mesh)
{
	if (!Preview.IsValid())
	{
		return;
	}
	Preview->SetCharacter(Mesh);
	UDTHExpressionSettings* Settings = GetMutableDefault<UDTHExpressionSettings>();
	Settings->PreviewMesh = Mesh ? FSoftObjectPath(Mesh) : FSoftObjectPath();
	Settings->SaveConfig();
	if (TargetSequence.IsValid() && Mesh && TargetSequence->GetSkeleton() != Mesh->GetSkeleton())
	{
		TargetSequence.Reset();
	}
	MixChanged();
}

FReply SDTHExpressionWizard::OnNewSequence()
{
	USkeletalMesh* Mesh = Preview->GetCharacter();
	FString Error;
	UAnimSequence* Sequence = FDTHMixer::CreateSequence(GetDefault<UDTHExpressionSettings>()->SequenceFolder.Path, NewSequenceName, Mesh ? Mesh->GetSkeleton() : nullptr, Error);
	if (!Sequence)
	{
		Status = Error;
		return FReply::Handled();
	}
	TargetSequence = Sequence;
	NewSequenceName.Reset();
	Status = FString::Printf(TEXT("Created %s."), *Sequence->GetPackage()->GetName());
	return FReply::Handled();
}

FReply SDTHExpressionWizard::OnKey()
{
	const UDTHExpressionLibrary* Lib = Library.Get();
	if (!Lib || !TargetSequence.IsValid())
	{
		Status = TEXT("Pick or create a sequence to key into.");
		return FReply::Handled();
	}
	TArray<FDTHMixEntry> Entries;
	for (const TSharedPtr<FMixRow>& Row : MixRows)
	{
		if (!Row->bMuted) Entries.Add({Row->Expression, Row->Strength});
	}
	TMap<FName, float> Values;
	FDTHMixer::Mix(*Lib, Entries, Values);
	FString Error;
	if (!FDTHMixer::Key(TargetSequence.Get(), KeyFrame, Values, Error))
	{
		Status = Error;
		return FReply::Handled();
	}
	Status = FString::Printf(TEXT("Keyed the mix at frame %d of %s (not saved yet)."), KeyFrame, *TargetSequence->GetName());
	return FReply::Handled();
}

FReply SDTHExpressionWizard::OnSaveExpression()
{
	const UDTHExpressionLibrary* Lib = Library.Get();
	USkeletalMesh* Mesh = Preview->GetCharacter();
	if (!Lib || !Mesh)
	{
		return FReply::Handled();
	}
	TArray<FDTHMixEntry> Entries;
	FString Name = TEXT("A_Face");
	for (const TSharedPtr<FMixRow>& Row : MixRows)
	{
		if (Row->bMuted) continue;
		Entries.Add({Row->Expression, Row->Strength});
		FString Part = Lib->Expressions[Row->Expression].Label;
		Part = FPaths::MakeValidFileName(Part.Replace(TEXT(" "), TEXT("")), TCHAR('_')).Left(32);
		Name += TEXT("_") + Part;
	}
	const FString Folder = GetDefault<UDTHExpressionSettings>()->SequenceFolder.Path;
	FString Unique = Name.Left(120);
	for (int32 N = 2; FPackageName::DoesPackageExist(Folder / Unique); ++N) Unique = FString::Printf(TEXT("%s_%d"), *Name.Left(120), N);
	FString Error;
	UAnimSequence* Sequence = FDTHMixer::CreateSequence(Folder, Unique, Mesh->GetSkeleton(), Error);
	TMap<FName, float> Values;
	FDTHMixer::Mix(*Lib, Entries, Values);
	if (!Sequence || !FDTHMixer::Key(Sequence, 0, Values, Error))
	{
		Status = Error;
		return FReply::Handled();
	}
	FDTHMixer::Key(Sequence, 1, Values, Error);
	SaveAsset(Sequence);
	Status = FString::Printf(TEXT("Saved %s."), *Sequence->GetPackage()->GetName());
	return FReply::Handled();
}

// --- Action bar ---------------------------------------------------------------------------------------------------

FText SDTHExpressionWizard::GetStatusText() const
{
	if (!Status.IsEmpty())
	{
		return FText::FromString(Status);
	}
	if (!Library.IsValid())
	{
		return LOCTEXT("NoLibrary", "No expression library yet: run the Daz sampler (Settings), then Build Library.");
	}
	return FText::Format(LOCTEXT("Ready", "{0} expressions; {1} in the mix."), FText::AsNumber(Library->Expressions.Num()), FText::AsNumber(MixRows.Num()));
}

TSharedRef<SWidget> SDTHExpressionWizard::MakeActionBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
		.Padding(FMargin(8.f, 6.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).ColorAndOpacity(FStyleColors::Foreground).Text(this, &SDTHExpressionWizard::GetStatusText)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.Visibility_Lambda([this]() { return CurrentPage == EPage::Build ? EVisibility::Visible : EVisibility::Collapsed; })
				.IsEnabled_Lambda([]() { return !GetDefault<UDTHExpressionSettings>()->SamplesFile.FilePath.IsEmpty(); })
				.OnClicked(this, &SDTHExpressionWizard::OnBuild)
				.ToolTipText(LOCTEXT("BuildTip", "Fit every sampled expression to DTH's face rig and write the library"))
				[
					SNew(STextBlock).TextStyle(FAppStyle::Get(), "SmallButtonText").Text(LOCTEXT("BuildButton", "Build Library"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.Visibility_Lambda([this]() { return CurrentPage == EPage::Mixer ? EVisibility::Visible : EVisibility::Collapsed; })
				.IsEnabled_Lambda([this]() { return TargetSequence.IsValid() && Library.IsValid(); })
				.OnClicked(this, &SDTHExpressionWizard::OnKey)
				.ToolTipText(LOCTEXT("KeyTip", "Key the mix at the frame into the sequence (every face-board curve is keyed, so the key fully sets the face)"))
				[
					SNew(STextBlock).TextStyle(FAppStyle::Get(), "SmallButtonText").Text(LOCTEXT("KeyButton", "Key"))
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
