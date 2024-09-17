// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FSlateStyleSet;
struct FRefExplorerGraphNodeFactory;

//------------------------------------------------------
// FRefExplorerEditorModule
//------------------------------------------------------

class FRefExplorerEditorModule : public IModuleInterface
{
public:
	class IContentBrowserSelectionMenuExtender
	{
	public:
		virtual void Extend() = 0;
	};

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FName GetStyleSetName();
	static FName GetContextMenuRefExplorerIconName();
	
	static const TSharedPtr<FSlateStyleSet> GetStyleSet() { return StyleSet; }

protected:
	void StartupStyle();
	void ShutdownStyle();

	TSharedRef<SDockTab> OnSpawnTab(const FSpawnTabArgs& SpawnTabArgs);

protected:
	static TSharedPtr<FSlateStyleSet> StyleSet;
	TArray<TSharedPtr<IContentBrowserSelectionMenuExtender>> ContentBrowserSelectionMenuExtenders;
	TSharedPtr<FRefExplorerGraphNodeFactory> RefExplorerGraphNodeFactory;
};