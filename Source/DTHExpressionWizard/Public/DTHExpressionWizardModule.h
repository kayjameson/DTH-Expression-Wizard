#pragma once

#include "Modules/ModuleInterface.h"

/** Registers the Expression Wizard's dockable tab and its entry in the level editor's Tools menu. */
class FDTHExpressionWizardModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static const FName WizardTabName;

	/** The plugin's Resources folder on disk (the Daz sampler script and the face rig description live there). */
	static FString ResourcesDir();

private:
	void RegisterMenus();
	TSharedRef<class SDockTab> SpawnWizardTab(const class FSpawnTabArgs& Args);
};
