#pragma once

#include "Library/DTHLibraryBuild.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class IDetailsView;
class ITableRow;
class SDTHFacePreview;
class STableViewBase;
class SWidgetSwitcher;
class UAnimSequence;
class UDTHExpressionLibrary;
class USkeletalMesh;
struct FSlateDynamicImageBrush;

/** The Expression Wizard's tab: Settings (and the Daz script), Build (the library) and the Mixer. */
class SDTHExpressionWizard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDTHExpressionWizard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The open wizard, if its tab exists (for scripting). */
	static TSharedPtr<SDTHExpressionWizard> Get() { return Instance.Pin(); }
	/** Shows a page by name: Settings, Build or Mixer. False for an unknown name. */
	bool ShowPage(const FString& Name);
	/** Reads the settings again: the library asset and the preview character. */
	void Reload();
	/** Previews this character, as picking it in the Mixer does. */
	void SelectCharacter(USkeletalMesh* Mesh) { SetCharacter(Mesh); }
	/** Saves the Mixer's preview, framed as View (front, threequarter, eyes, mouth), to File. "" or "ERROR: ...". */
	FString CapturePreview(const FString& File, const FString& View);
	/** Replaces the mix with (expression index, strength) pairs. Returns a status line ("ERROR: ..." for a bad index). */
	FString ReplaceMix(const TArray<TPair<int32, float>>& Mix);

private:
	enum class EPage : int32 { Settings, Build, Mixer, Count };

	struct FMixRow
	{
		int32 Expression = INDEX_NONE;
		float Strength = 1.0f;
		bool bMuted = false;
	};
	using FMessage = FDTHBuildMessage;

	// Layout
	TSharedRef<SWidget> MakeStepBar();
	TSharedRef<SWidget> MakeSection(const FText& Title, const TSharedRef<SWidget>& Content);
	TSharedRef<SWidget> MakeSettingsPage();
	TSharedRef<SWidget> MakeBuildPage();
	TSharedRef<SWidget> MakeMixerPage();
	TSharedRef<SWidget> MakeKeyBar();
	TSharedRef<SWidget> MakeActionBar();
	TSharedRef<ITableRow> MakeMessageRow(TSharedPtr<FMessage> Message, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> MakeExpressionRow(TSharedPtr<int32> Expression, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> MakeMixRow(TSharedPtr<FMixRow> Row, const TSharedRef<STableViewBase>& Owner);

	// Actions
	FReply OnBuild();
	void LoadLibrary();
	void RefreshExpressionList();
	void AddToMix(int32 Expression);
	void MixChanged();
	void SetCharacter(USkeletalMesh* Mesh);
	FReply OnKey();
	FReply OnSaveExpression();
	FReply OnNewSequence();
	void AddMessage(FMessage::ESeverity Severity, const FString& Text);

	FText GetStatusText() const;
	FText GetLibrarySummary() const;
	const FSlateBrush* GetIcon(int32 Expression);

	EPage CurrentPage = EPage::Mixer;
	TSharedPtr<IDetailsView> SettingsView;

	TWeakObjectPtr<UDTHExpressionLibrary> Library;
	FString Status;

	TArray<TSharedPtr<FMessage>> Messages;
	TSharedPtr<SListView<TSharedPtr<FMessage>>> MessageList;

	FString Search;
	TArray<TSharedPtr<int32>> ExpressionRows;
	TSharedPtr<SListView<TSharedPtr<int32>>> ExpressionList;
	TMap<int32, TSharedPtr<FSlateDynamicImageBrush>> Icons;

	TArray<TSharedPtr<FMixRow>> MixRows;
	TSharedPtr<SListView<TSharedPtr<FMixRow>>> MixList;

	TSharedPtr<SDTHFacePreview> Preview;
	TWeakObjectPtr<UAnimSequence> TargetSequence;
	FString NewSequenceName;
	int32 KeyFrame = 0;

	static TWeakPtr<SDTHExpressionWizard> Instance;
};
