#include "DTHExpressionWizardModule.h"

#include "DTHExpressionLog.h"

#include "Framework/Docking/TabManager.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UI/SDTHExpressionWizard.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "DTHExpressionWizard"

DEFINE_LOG_CATEGORY(LogDTHExpression);

const FName FDTHExpressionWizardModule::WizardTabName(TEXT("DTHExpressionWizard"));

void FDTHExpressionWizardModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(WizardTabName,
		FOnSpawnTab::CreateRaw(this, &FDTHExpressionWizardModule::SpawnWizardTab))
		.SetDisplayName(LOCTEXT("TabTitle", "DTH Expression Wizard"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Mix facial expressions for DazToHue Genesis 9 characters and key them into animation sequences."))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.AnimSequence"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDTHExpressionWizardModule::RegisterMenus));
}

void FDTHExpressionWizardModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(WizardTabName);
	}
}

FString FDTHExpressionWizardModule::ResourcesDir()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DTHExpressionWizard"));
	return Plugin.IsValid() ? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources")) : FString();
}

void FDTHExpressionWizardModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = Menu->FindOrAddSection("DTH", LOCTEXT("MenuSection", "DazToHue"));
	Section.AddMenuEntry(
		"OpenDTHExpressionWizard",
		LOCTEXT("MenuEntry", "DTH Expression Wizard"),
		LOCTEXT("MenuEntryTooltip", "Mix facial expressions for DazToHue Genesis 9 characters and key them into animation sequences."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.AnimSequence"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FDTHExpressionWizardModule::WizardTabName);
		})));
}

TSharedRef<SDockTab> FDTHExpressionWizardModule::SpawnWizardTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SDTHExpressionWizard)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDTHExpressionWizardModule, DTHExpressionWizard)
