// Copyright Epic Games, Inc. All Rights Reserved.

#include "RefExplorerEditorModule.h"
#include "RefExplorerEditorModule_private.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Dialogs/Dialogs.h"
#include "GraphEditor.h"
#include "Selection.h"
#include "ObjectTools.h"
#include "Filters/SFilterBar.h"
#include "ToolMenus.h"
#include "Toolkits/GlobalEditorCommonCommands.h"
#include "ConnectionDrawingPolicy.h"
#include "EditorWidgetsModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/AssetManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "SCommentBubble.h"
#include "UObject/UnrealTypePrivate.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "FRefExplorerEditorModule"

//--------------------------------------------------------------------
// COMMON
//--------------------------------------------------------------------

namespace FRefExplorerEditorModule_PRIVATE
{
	enum class EDependencyPinCategory
	{
		LinkEndPassive = 0,
		LinkEndActive = 1,
		LinkEndMask = LinkEndActive,

		LinkTypeNone = 0,
		LinkTypeUsedInGame = 2,
		LinkTypeHard = 4,
		LinkTypeMask = LinkTypeHard | LinkTypeUsedInGame,
	};
	ENUM_CLASS_FLAGS(EDependencyPinCategory);

	namespace DependencyPinCategory
	{
		FName NamePassive(TEXT("Passive"));
		FName NameHardUsedInGame(TEXT("Hard"));
		FName NameHardEditorOnly(TEXT("HardEditorOnly"));
		FName NameSoftUsedInGame(TEXT("Soft"));
		FName NameSoftEditorOnly(TEXT("SoftEditorOnly"));
		const FLinearColor ColorPassive = FLinearColor(128, 128, 128);
		const FLinearColor ColorHardUsedInGame = FLinearColor(FColor(236, 252, 227)); // RiceFlower
		const FLinearColor ColorHardEditorOnly = FLinearColor(FColor(118, 126, 114));
		const FLinearColor ColorSoftUsedInGame = FLinearColor(FColor(145, 66, 117)); // CannonPink
		const FLinearColor ColorSoftEditorOnly = FLinearColor(FColor(73, 33, 58));
	}

	EDependencyPinCategory ParseDependencyPinCategory(FName PinCategory)
	{
		if (PinCategory == DependencyPinCategory::NameHardUsedInGame)
		{
			return EDependencyPinCategory::LinkEndActive | EDependencyPinCategory::LinkTypeHard | EDependencyPinCategory::LinkTypeUsedInGame;
		}
		else if (PinCategory == DependencyPinCategory::NameHardEditorOnly)
		{
			return EDependencyPinCategory::LinkEndActive | EDependencyPinCategory::LinkTypeHard;
		}
		else if (PinCategory == DependencyPinCategory::NameSoftUsedInGame)
		{
			return EDependencyPinCategory::LinkEndActive | EDependencyPinCategory::LinkTypeUsedInGame;
		}
		else if (PinCategory == DependencyPinCategory::NameSoftEditorOnly)
		{
			return EDependencyPinCategory::LinkEndActive;
		}
		else
		{
			return EDependencyPinCategory::LinkEndPassive;
		}
	}

	FName GetName(EDependencyPinCategory Category)
	{
		if ((Category & EDependencyPinCategory::LinkEndMask) == EDependencyPinCategory::LinkEndPassive)
		{
			return DependencyPinCategory::NamePassive;
		}
		else
		{
			switch (Category & EDependencyPinCategory::LinkTypeMask)
			{
			case EDependencyPinCategory::LinkTypeHard | EDependencyPinCategory::LinkTypeUsedInGame:
				return DependencyPinCategory::NameHardUsedInGame;
			case EDependencyPinCategory::LinkTypeHard:
				return DependencyPinCategory::NameHardEditorOnly;
			case EDependencyPinCategory::LinkTypeUsedInGame:
				return DependencyPinCategory::NameSoftUsedInGame;
			default:
				return DependencyPinCategory::NameSoftEditorOnly;
			}
		}
	}

	FLinearColor GetColor(EDependencyPinCategory Category)
	{
		if ((Category & EDependencyPinCategory::LinkEndMask) == EDependencyPinCategory::LinkEndPassive)
		{
			return DependencyPinCategory::ColorPassive;
		}
		else
		{
			switch (Category & EDependencyPinCategory::LinkTypeMask)
			{
			case EDependencyPinCategory::LinkTypeHard | EDependencyPinCategory::LinkTypeUsedInGame:
				return DependencyPinCategory::ColorHardUsedInGame;
			case EDependencyPinCategory::LinkTypeHard:
				return DependencyPinCategory::ColorHardEditorOnly;
			case EDependencyPinCategory::LinkTypeUsedInGame:
				return DependencyPinCategory::ColorSoftUsedInGame;
			default:
				return DependencyPinCategory::ColorSoftEditorOnly;
			}
		}
	}
}
//--------------------------------------------------------------------
// FReferenceExplorerCommands
//--------------------------------------------------------------------

class FReferenceExplorerCommands : public TCommands<FReferenceExplorerCommands>
{
public:
	FReferenceExplorerCommands() : TCommands<FReferenceExplorerCommands>(
		"ReferenceExplorerCommands", NSLOCTEXT("Contexts", "ReferenceExplorerCommands", "Reference Explorer"),
		NAME_None, FAppStyle::GetAppStyleSetName())
	{}

	// TCommands<> interface
	virtual void RegisterCommands() override
	{
		UI_COMMAND(ViewReferences, "Reference Viewer...", "Launches the reference viewer showing the selected assets' references", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift | EModifierKey::Alt, EKeys::R));

		UI_COMMAND(OpenSelectedInAssetEditor, "Edit...", "Opens the selected asset in the relevant editor.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::E));
		UI_COMMAND(ZoomToFit, "Zoom to Fit", "Zoom in and center the view on the selected item", EUserInterfaceActionType::Button, FInputChord(EKeys::F));

		UI_COMMAND(CopyReferencedObjects, "Copy Referenced Objects List", "Copies the list of objects that the selected asset references to the clipboard.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(CopyReferencingObjects, "Copy Referencing Objects List", "Copies the list of objects that reference the selected asset to the clipboard.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ShowReferencedObjects, "Show Referenced Objects List", "Shows a list of objects that the selected asset references.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ShowReferencingObjects, "Show Referencing Objects List", "Shows a list of objects that reference the selected asset.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ShowReferenceTree, "Show Reference Tree", "Shows a reference tree for the selected asset.", EUserInterfaceActionType::Button, FInputChord());
	}
	// End of TCommands<> interface

	// Shows the reference viewer for the selected assets
	TSharedPtr<FUICommandInfo> ViewReferences;

	// Opens the selected asset in the asset editor
	TSharedPtr<FUICommandInfo> OpenSelectedInAssetEditor;

	// Copies the list of objects that the selected asset references
	TSharedPtr<FUICommandInfo> CopyReferencedObjects;

	// Copies the list of objects that reference the selected asset
	TSharedPtr<FUICommandInfo> CopyReferencingObjects;

	// Shows a list of objects that the selected asset references
	TSharedPtr<FUICommandInfo> ShowReferencedObjects;

	// Shows a list of objects that reference the selected asset
	TSharedPtr<FUICommandInfo> ShowReferencingObjects;

	// Shows a reference tree for the selected asset
	TSharedPtr<FUICommandInfo> ShowReferenceTree;

	/** Zoom in to fit the selected objects in the window */
	TSharedPtr<FUICommandInfo> ZoomToFit;
};

//--------------------------------------------------------------------
// FReferenceExplorerConnectionDrawingPolicy
//--------------------------------------------------------------------

class FReferenceExplorerConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
public:
	FReferenceExplorerConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements)
		: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
	{}

	virtual FVector2D ComputeSplineTangent(const FVector2D& Start, const FVector2D& End) const override
	{
		const int32 Tension = FMath::Abs<int32>(Start.X - End.X);
		return Tension * FVector2D(1.0f, 0);
	}

	virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params) override
	{
		FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory OutputCategory = FRefExplorerEditorModule_PRIVATE::ParseDependencyPinCategory(OutputPin->PinType.PinCategory);
		FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory InputCategory = FRefExplorerEditorModule_PRIVATE::ParseDependencyPinCategory(InputPin->PinType.PinCategory);

		FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory Category = !!(OutputCategory & FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory::LinkEndActive) ? OutputCategory : InputCategory;
		Params.WireColor = GetColor(Category);
	}
};

//--------------------------------------------------------------------
// UReferenceExplorerSchema
//--------------------------------------------------------------------

UReferenceExplorerSchema::UReferenceExplorerSchema(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

void UReferenceExplorerSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	{
		FToolMenuSection& Section = Menu->AddSection(TEXT("Asset"), NSLOCTEXT("ReferenceExplorerSchema", "AssetSectionLabel", "Asset"));
		Section.AddMenuEntry(FGlobalEditorCommonCommands::Get().FindInContentBrowser);
		Section.AddMenuEntry(FReferenceExplorerCommands::Get().OpenSelectedInAssetEditor);
	}

	{
		FToolMenuSection& Section = Menu->AddSection(TEXT("Misc"), NSLOCTEXT("ReferenceExplorerSchema", "MiscSectionLabel", "Misc"));
		Section.AddMenuEntry(FReferenceExplorerCommands::Get().ZoomToFit);
	}

	{
		FToolMenuSection& Section = Menu->AddSection(TEXT("References"), NSLOCTEXT("ReferenceExplorerSchema", "ReferencesSectionLabel", "References"));
		Section.AddMenuEntry(FReferenceExplorerCommands::Get().CopyReferencedObjects);
		Section.AddMenuEntry(FReferenceExplorerCommands::Get().CopyReferencingObjects);
		Section.AddMenuEntry(FReferenceExplorerCommands::Get().ShowReferencedObjects);
		Section.AddMenuEntry(FReferenceExplorerCommands::Get().ShowReferencingObjects);
		Section.AddMenuEntry(FReferenceExplorerCommands::Get().ShowReferenceTree);
	}
}

FLinearColor UReferenceExplorerSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return FRefExplorerEditorModule_PRIVATE::GetColor(FRefExplorerEditorModule_PRIVATE::ParseDependencyPinCategory(PinType.PinCategory));
}

FConnectionDrawingPolicy* UReferenceExplorerSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) const
{
	return new FReferenceExplorerConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements);
}

//--------------------------------------------------------------------
// UEdGraphNode_ReferenceExplorer
//--------------------------------------------------------------------

UEdGraphNode_ReferenceExplorer::UEdGraphNode_ReferenceExplorer(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	bUsesThumbnail = false;
	bIsPackage = false;
	bIsPrimaryAsset = false;

	AssetTypeColor = FLinearColor(0.55f, 0.55f, 0.55f);

	DependencyPin = NULL;
	ReferencerPin = NULL;
}

void UEdGraphNode_ReferenceExplorer::SetupReferenceNode(const FIntPoint& NodeLoc, const FAssetIdentifier& NewIdentifier, const FAssetData& InAssetData)
{
	NodePosX = NodeLoc.X;
	NodePosY = NodeLoc.Y;

	Identifier = NewIdentifier;

	FString MainAssetName = InAssetData.AssetName.ToString();
	FString AssetTypeName = InAssetData.AssetClassPath.GetAssetName().ToString();

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	if (UClass* AssetClass = InAssetData.GetClass())
	{
		TWeakPtr<IAssetTypeActions> AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(InAssetData.GetClass());
		if (AssetTypeActions.IsValid())
		{
			AssetTypeColor = AssetTypeActions.Pin()->GetTypeColor();
		}
	}
	AssetBrush = FSlateIcon("EditorStyle", FName(*("ClassIcon." + AssetTypeName)));

	bIsPackage = true;

	FPrimaryAssetId PrimaryAssetID = Identifier.GetPrimaryAssetId();
	if (PrimaryAssetID.IsValid())  // Management References (PrimaryAssetIDs)
	{
		static FText ManagerText = LOCTEXT("ReferenceManager", "Manager");
		MainAssetName = PrimaryAssetID.PrimaryAssetType.ToString() + TEXT(":") + PrimaryAssetID.PrimaryAssetName.ToString();
		AssetTypeName = ManagerText.ToString();
		
		bIsPackage = false;
		bIsPrimaryAsset = true;
	}
	else if (Identifier.IsValue()) // Searchable Names (GamePlay Tags, Data Table Row Handle)
	{
		MainAssetName = Identifier.ValueName.ToString();
		AssetTypeName = Identifier.ObjectName.ToString();
		static const FName NAME_DataTable(TEXT("DataTable"));
		static const FText InDataTableText = LOCTEXT("InDataTable", "In DataTable");
		if (InAssetData.AssetClassPath.GetAssetName() == NAME_DataTable)
		{
			AssetTypeName = InDataTableText.ToString() + TEXT(" ") + AssetTypeName;
		}

		bIsPackage = false;
	}
	else if (Identifier.IsPackage() && !InAssetData.IsValid())
	{
		const FString PackageNameStr = Identifier.PackageName.ToString();
		if (PackageNameStr.StartsWith(TEXT("/Script")))// C++ Packages (/Script Code)
		{
			MainAssetName = PackageNameStr.RightChop(8);
			AssetTypeName = TEXT("Script");
		}
	}

	static const FName NAME_ActorLabel(TEXT("ActorLabel"));
	InAssetData.GetTagValue(NAME_ActorLabel, MainAssetName);

	// append the type so it shows up on the extra line
	NodeTitle = FText::FromString(FString::Printf(TEXT("%s\n%s"), *MainAssetName, *AssetTypeName));

	if (bIsPackage)
	{
		NodeComment = Identifier.PackageName.ToString();
	}

	if (InAssetData.IsValid() && IsPackage())
	{
		bUsesThumbnail = true;
		CachedAssetData = InAssetData;
	}
	else
	{
		bUsesThumbnail = false;
		CachedAssetData = FAssetData();

		const FString PackageNameStr = Identifier.PackageName.ToString();
		if (FPackageName::IsValidLongPackageName(PackageNameStr, true))
		{
			if (PackageNameStr.StartsWith(TEXT("/Script")))
			{
				// Used Only in the UI for the Thumbnail
				CachedAssetData.AssetClassPath = FTopLevelAssetPath(TEXT("/EdGraphNode_Reference"), TEXT("Code"));
			}
			else
			{
				const FString PotentiallyMapFilename = FPackageName::LongPackageNameToFilename(PackageNameStr, FPackageName::GetMapPackageExtension());
				const bool bIsMapPackage = FPlatformFileManager::Get().GetPlatformFile().FileExists(*PotentiallyMapFilename);
				if (bIsMapPackage)
				{
					// Used Only in the UI for the Thumbnail
					CachedAssetData.AssetClassPath = TEXT("/Script/Engine.World");
				}
			}
		}
	}

	AllocateDefaultPins();
}

void UEdGraphNode_ReferenceExplorer::AddReferencer(UEdGraphNode_ReferenceExplorer* ReferencerNode)
{
	UEdGraphPin* ReferencerDependencyPin = ReferencerNode->GetDependencyPin();

	if (ensure(ReferencerDependencyPin))
	{
		ReferencerDependencyPin->bHidden = false;
		ReferencerPin->bHidden = false;
		ReferencerPin->MakeLinkTo(ReferencerDependencyPin);
	}
}

UEdGraph_ReferenceExplorer* UEdGraphNode_ReferenceExplorer::GetReferenceViewerGraph() const { return Cast<UEdGraph_ReferenceExplorer>(GetGraph()); }

FLinearColor UEdGraphNode_ReferenceExplorer::GetNodeTitleColor() const
{
	if (bIsPrimaryAsset)
	{
		return FLinearColor(0.2f, 0.8f, 0.2f);
	}
	else if (bIsPackage)
	{
		return AssetTypeColor;
	}
	else
	{
		return FLinearColor(0.0f, 0.55f, 0.62f);
	}
}

FText UEdGraphNode_ReferenceExplorer::GetTooltipText() const { return FText::FromString(Identifier.ToString()); }

void UEdGraphNode_ReferenceExplorer::AllocateDefaultPins()
{
	FName PassiveName = FRefExplorerEditorModule_PRIVATE::GetName(FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory::LinkEndPassive);

	ReferencerPin = CreatePin(EEdGraphPinDirection::EGPD_Input, NAME_None, NAME_None);
	ReferencerPin->bHidden = true;
	ReferencerPin->PinType.PinCategory = PassiveName;

	DependencyPin = CreatePin(EEdGraphPinDirection::EGPD_Output, NAME_None, NAME_None);
	DependencyPin->bHidden = true;
	DependencyPin->PinType.PinCategory = PassiveName;
}

//--------------------------------------------------------------------
// UEdGraph_ReferenceExplorer
//--------------------------------------------------------------------

UEdGraph_ReferenceExplorer::UEdGraph_ReferenceExplorer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (!IsTemplate())
	{
		AssetThumbnailPool = MakeShareable(new FAssetThumbnailPool(1024));
	}
}

void UEdGraph_ReferenceExplorer::BeginDestroy()
{
	AssetThumbnailPool.Reset();

	Super::BeginDestroy();
}

void UEdGraph_ReferenceExplorer::SetGraphRoot(const FAssetIdentifier& GraphRootIdentifier, const FIntPoint& GraphRootOrigin)
{
	CurrentGraphRootIdentifier = GraphRootIdentifier;
	CurrentGraphRootOrigin = GraphRootOrigin;
	UAssetManager::Get().UpdateManagementDatabase();
}

UEdGraphNode_ReferenceExplorer* UEdGraph_ReferenceExplorer::RebuildGraph()
{
	RemoveAllNodes();

	ReferencerNodeInfos.Reset();
	;
	ReferencerNodeInfos.FindOrAdd(CurrentGraphRootIdentifier, FReferenceExplorerNodeInfo(CurrentGraphRootIdentifier));

	TMap<FAssetIdentifier, FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory> ReferenceLinks;
	GetSortedLinks(CurrentGraphRootIdentifier, ReferenceLinks);

	ReferencerNodeInfos[CurrentGraphRootIdentifier].Children.Reserve(ReferenceLinks.Num());

	for (const TPair<FAssetIdentifier, FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory>& Pair : ReferenceLinks)
	{
		const FAssetIdentifier ChildId = Pair.Key;

		if (!ReferencerNodeInfos.Contains(ChildId))
		{
			FReferenceExplorerNodeInfo& NewNodeInfo = ReferencerNodeInfos.FindOrAdd(ChildId, FReferenceExplorerNodeInfo(ChildId));
			ReferencerNodeInfos[ChildId].Parents.FindOrAdd(CurrentGraphRootIdentifier);
			ReferencerNodeInfos[CurrentGraphRootIdentifier].Children.Emplace(Pair);
		}
		else if (!ReferencerNodeInfos[ChildId].Parents.Contains(CurrentGraphRootIdentifier))
		{
			ReferencerNodeInfos[ChildId].Parents.FindOrAdd(CurrentGraphRootIdentifier);
			ReferencerNodeInfos[CurrentGraphRootIdentifier].Children.Emplace(Pair);
		}
	}

	TSet<FName> AllPackageNames;

	for (TPair<FAssetIdentifier, FReferenceExplorerNodeInfo>& InfoPair : ReferencerNodeInfos)
	{
		if (!InfoPair.Key.IsValue() && !InfoPair.Key.PackageName.IsNone())
		{
			AllPackageNames.Add(InfoPair.Key.PackageName);
		}
	}

	TMap<FName, FAssetData> PackagesToAssetDataMap;
	UE::AssetRegistry::GetAssetForPackages(AllPackageNames.Array(), PackagesToAssetDataMap);

	TSet<FTopLevelAssetPath> AllClasses;
	for (TPair<FAssetIdentifier, FReferenceExplorerNodeInfo>& InfoPair : ReferencerNodeInfos)
	{
		InfoPair.Value.AssetData = PackagesToAssetDataMap.FindRef(InfoPair.Key.PackageName);
		if (InfoPair.Value.AssetData.IsValid())
		{
			AllClasses.Add(InfoPair.Value.AssetData.AssetClassPath);
		}
	}

	UEdGraphNode_ReferenceExplorer* RootNode = nullptr;

	if (!ReferencerNodeInfos.IsEmpty())
	{
		const FReferenceExplorerNodeInfo& NodeInfo = ReferencerNodeInfos[CurrentGraphRootIdentifier];
		RootNode = Cast<UEdGraphNode_ReferenceExplorer>(CreateNode(UEdGraphNode_ReferenceExplorer::StaticClass(), false));
		RootNode->SetupReferenceNode(CurrentGraphRootOrigin, CurrentGraphRootIdentifier, NodeInfo.AssetData);

		// References
		RecursivelyCreateNodes(CurrentGraphRootIdentifier, CurrentGraphRootOrigin, CurrentGraphRootIdentifier, RootNode, ReferencerNodeInfos, /*bIsRoot*/ true);
	}

	NotifyGraphChanged();

	return RootNode;
}

void UEdGraph_ReferenceExplorer::GetSortedLinks(const FAssetIdentifier& GraphRootIdentifier, TMap<FAssetIdentifier, FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory>& OutLinks) const
{
	using namespace UE::AssetRegistry;
	auto CategoryOrder = [](EDependencyCategory InCategory)
		{
			switch (InCategory)
			{
			case EDependencyCategory::Package:
			{
				return 0;
			}
			case EDependencyCategory::Manage:
			{
				return 1;
			}
			case EDependencyCategory::SearchableName:
			{
				return 2;
			}
			default:
			{
				check(false);
				return 3;
			}
			}
		};
	auto IsHard = [](EDependencyProperty Properties)
		{
			return static_cast<bool>(((Properties & EDependencyProperty::Hard) != EDependencyProperty::None) | ((Properties & EDependencyProperty::Direct) != EDependencyProperty::None));
		};

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetDependency> LinksToAsset;

	LinksToAsset.Reset();

	using namespace UE::AssetRegistry;
	EDependencyCategory Categories = EDependencyCategory::Package | EDependencyCategory::Manage;
	EDependencyQuery Flags = EDependencyQuery::NoRequirements;

	AssetRegistry.GetReferencers(GraphRootIdentifier, LinksToAsset, Categories, Flags);

	// Sort the links from most important kind of link to least important kind of link, so that if we can't display them all in an ExceedsMaxSearchBreadth test, we
	// show the most important links.
	Algo::Sort(LinksToAsset, [&CategoryOrder, &IsHard](const FAssetDependency& A, const FAssetDependency& B)
		{
			if (A.Category != B.Category)
			{
				return CategoryOrder(A.Category) < CategoryOrder(B.Category);
			}
			if (A.Properties != B.Properties)
			{
				bool bAIsHard = IsHard(A.Properties);
				bool bBIsHard = IsHard(B.Properties);
				if (bAIsHard != bBIsHard)
				{
					return bAIsHard;
				}
			}
			return A.AssetId.PackageName.LexicalLess(B.AssetId.PackageName);
		});
	for (FAssetDependency LinkToAsset : LinksToAsset)
	{
		FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory& Category = OutLinks.FindOrAdd(LinkToAsset.AssetId, FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory::LinkEndActive);
		bool bIsHard = IsHard(LinkToAsset.Properties);
		bool bIsUsedInGame = (LinkToAsset.Category != EDependencyCategory::Package) || ((LinkToAsset.Properties & EDependencyProperty::Game) != EDependencyProperty::None);
		Category |= FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory::LinkEndActive;
		Category |= bIsHard ? FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory::LinkTypeHard : FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory::LinkTypeNone;
		Category |= bIsUsedInGame ? FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory::LinkTypeUsedInGame : FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory::LinkTypeNone;
	}

	// Check filters and Filter for our registry source
	
	TArray<FAssetIdentifier> ReferenceIds;
	OutLinks.GenerateKeyArray(ReferenceIds);
	
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	for (int32 Index = 0; Index < ReferenceIds.Num(); Index++)
	{
		FName PackageName = ReferenceIds[Index].PackageName;

		if (PackageName != NAME_None)
		{
			TOptional<FAssetPackageData> assetPackageData = AssetRegistryModule.Get().GetAssetPackageDataCopy(PackageName);

			if (!assetPackageData.GetPtrOrNull() || assetPackageData.GetPtrOrNull()->DiskSize < 0)
			{
				// Remove bad package
				ReferenceIds.RemoveAt(Index);

				// If this is a redirector replace with references
				TArray<FAssetData> Assets;
				AssetRegistryModule.Get().GetAssetsByPackageName(PackageName, Assets, true);

				for (const FAssetData& Asset : Assets)
				{
					if (Asset.IsRedirector())
					{
						TArray<FAssetIdentifier> FoundReferences;

						AssetRegistryModule.Get().GetReferencers(PackageName, FoundReferences, Categories, Flags);

						ReferenceIds.Insert(FoundReferences, Index);
						break;
					}
				}

				// Need to redo this index, it was either removed or replaced
				Index--;
			}
		}
	}

	for (TMap<FAssetIdentifier, FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory>::TIterator It(OutLinks); It; ++It)
	{
		////// Here was scripts filtering like InAssetIdentifier.PackageName.ToString().StartsWith(TEXT("/Script"))
		//////if (!IsPackageIdentifierPassingFilter(It.Key()))
		//////{
		//////	It.RemoveCurrent();
		//////}
		//////else if (!ReferenceIds.Contains(It.Key()))
		if (!ReferenceIds.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}

void UEdGraph_ReferenceExplorer::GatherAssetData(TMap<FAssetIdentifier, FReferenceExplorerNodeInfo>& InNodeInfos)
{
	// Grab the list of packages
	TSet<FName> PackageNames;
	for (TPair<FAssetIdentifier, FReferenceExplorerNodeInfo>& InfoPair : InNodeInfos)
	{
		FAssetIdentifier& AssetId = InfoPair.Key;
		if (!AssetId.IsValue() && !AssetId.PackageName.IsNone())
		{
			PackageNames.Add(AssetId.PackageName);
		}
	}

	// Retrieve the AssetData from the Registry
	TMap<FName, FAssetData> PackagesToAssetDataMap;
	UE::AssetRegistry::GetAssetForPackages(PackageNames.Array(), PackagesToAssetDataMap);


	// Populate the AssetData back into the NodeInfos
	for (TPair<FAssetIdentifier, FReferenceExplorerNodeInfo>& InfoPair : InNodeInfos)
	{
		InfoPair.Value.AssetData = PackagesToAssetDataMap.FindRef(InfoPair.Key.PackageName);
	}
}

UEdGraphNode_ReferenceExplorer* UEdGraph_ReferenceExplorer::RecursivelyCreateNodes(const FAssetIdentifier& InAssetId, const FIntPoint& InNodeLoc, const FAssetIdentifier& InParentId, UEdGraphNode_ReferenceExplorer* InParentNode, TMap<FAssetIdentifier, FReferenceExplorerNodeInfo>& InNodeInfos, bool bIsRoot)
{
	check(InNodeInfos.Contains(InAssetId));

	const FReferenceExplorerNodeInfo& NodeInfo = InNodeInfos[InAssetId];

	UEdGraphNode_ReferenceExplorer* NewNode = nullptr;
	
	if (bIsRoot)
	{
		NewNode = InParentNode;
	}
	else
	{
		NewNode = Cast<UEdGraphNode_ReferenceExplorer>(CreateNode(UEdGraphNode_ReferenceExplorer::StaticClass(), false));
		NewNode->SetupReferenceNode(InNodeLoc, { InAssetId }, NodeInfo.AssetData);
	}

	FIntPoint ChildLoc = InNodeLoc;

	const int32 NodeRadius = 400;
	
	if (InNodeInfos[InAssetId].Children.Num() > 0)
	{
		const float DeltaAngle = UE_PI / InNodeInfos[InAssetId].Children.Num();

		const float LastAngle = DeltaAngle * (InNodeInfos[InAssetId].Children.Num() == 1 ? 0 : (InNodeInfos[InAssetId].Children.Num() - 1));
		
		const float Radius = NodeRadius / FMath::Max(FMath::Abs(1 - FMath::Cos(DeltaAngle)), FMath::Abs(FMath::Sin(DeltaAngle)));

		for (int32 ChildIdx = 0; ChildIdx < InNodeInfos[InAssetId].Children.Num(); ChildIdx++)
		{
			const TPair<FAssetIdentifier, FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory>& Pair = InNodeInfos[InAssetId].Children[ChildIdx];

			FAssetIdentifier ChildId = Pair.Key;

			const float AccumAngle = ChildIdx * DeltaAngle;

			ChildLoc.X = InNodeLoc.X + Radius * FMath::Cos(AccumAngle + (UE_PI - LastAngle / 2));
			ChildLoc.Y = InNodeLoc.Y - Radius * FMath::Sin(AccumAngle + (UE_PI - LastAngle / 2));

			UEdGraphNode_ReferenceExplorer* ChildNode = RecursivelyCreateNodes(ChildId, ChildLoc, InAssetId, NewNode, InNodeInfos);

			ChildNode->GetDependencyPin()->PinType.PinCategory = FRefExplorerEditorModule_PRIVATE::GetName(Pair.Value);
			NewNode->AddReferencer(ChildNode);
		}
	}

	return NewNode;
}

const TSharedPtr<FAssetThumbnailPool>& UEdGraph_ReferenceExplorer::GetAssetThumbnailPool() const
{
	return AssetThumbnailPool;
}

void UEdGraph_ReferenceExplorer::RemoveAllNodes()
{
	TArray<UEdGraphNode*> NodesToRemove = Nodes;
	for (int32 NodeIndex = 0; NodeIndex < NodesToRemove.Num(); ++NodeIndex)
	{
		RemoveNode(NodesToRemove[NodeIndex]);
	}
}

//------------------------------------------------------
// SGraphNode_ReferenceExplorer
//------------------------------------------------------

class SGraphNode_ReferenceExplorer : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_ReferenceExplorer) {}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, UEdGraphNode_ReferenceExplorer* InNode);

	// SGraphNode implementation
	virtual void UpdateGraphNode() override;
	virtual bool IsNodeEditable() const override { return false; }
	// End SGraphNode implementation

private:
	TSharedPtr<class FAssetThumbnail> AssetThumbnail;
};

void SGraphNode_ReferenceExplorer::Construct(const FArguments& InArgs, UEdGraphNode_ReferenceExplorer* InNode)
{
	const int32 ThumbnailSize = 128;

	if (InNode->UsesThumbnail())
	{
		// Create a thumbnail from the graph's thumbnail pool
		TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool = InNode->GetReferenceViewerGraph()->GetAssetThumbnailPool();
		AssetThumbnail = MakeShareable(new FAssetThumbnail(InNode->GetAssetData(), ThumbnailSize, ThumbnailSize, AssetThumbnailPool));
	}
	else if (InNode->IsPackage())
	{
		// Just make a generic thumbnail
		AssetThumbnail = MakeShareable(new FAssetThumbnail(InNode->GetAssetData(), ThumbnailSize, ThumbnailSize, NULL));
	}

	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

// UpdateGraphNode is similar to the base, but adds the option to hide the thumbnail */
void SGraphNode_ReferenceExplorer::UpdateGraphNode()
{
	OutputPins.Empty();

	// Reset variables that are going to be exposed, in case we are refreshing an already setup node.
	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	UpdateErrorInfo();

	//
	//             ______________________
	//            |      TITLE AREA      |
	//            +-------+------+-------+
	//            | (>) L |      | R (>) |
	//            | (>) E |      | I (>) |
	//            | (>) F |      | G (>) |
	//            | (>) T |      | H (>) |
	//            |       |      | T (>) |
	//            |_______|______|_______|
	//
	TSharedPtr<SVerticalBox> MainVerticalBox;
	TSharedPtr<SErrorText> ErrorText;
	TSharedPtr<SNodeTitle> NodeTitle = SNew(SNodeTitle, GraphNode);

	// Get node icon
	IconColor = FLinearColor::White;
	const FSlateBrush* IconBrush = nullptr;
	if (GraphNode != NULL && GraphNode->ShowPaletteIconOnNode())
	{
		IconBrush = GraphNode->GetIconAndTint(IconColor).GetOptionalIcon();
	}

	TSharedRef<SWidget> ThumbnailWidget = SNullWidget::NullWidget;
	UEdGraphNode_ReferenceExplorer* RefGraphNode = CastChecked<UEdGraphNode_ReferenceExplorer>(GraphNode);

	FLinearColor OpacityColor = FLinearColor::White;

	if (AssetThumbnail.IsValid())
	{

		FAssetThumbnailConfig ThumbnailConfig;
		ThumbnailConfig.bAllowFadeIn = RefGraphNode->UsesThumbnail();
		ThumbnailConfig.bForceGenericThumbnail = !RefGraphNode->UsesThumbnail();
		ThumbnailConfig.AssetTypeColorOverride = FLinearColor::Transparent;

		ThumbnailWidget =
			SNew(SBox)
			.WidthOverride(AssetThumbnail->GetSize().X)
			.HeightOverride(AssetThumbnail->GetSize().Y)
			[
				AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig)
			];
	}

	TArray<FString> referencerProperties;

	if (UEdGraphNode_ReferenceExplorer* referenceExplorerNode = Cast<UEdGraphNode_ReferenceExplorer>(GraphNode))
	{
		UEdGraph_ReferenceExplorer* referenceExplorerGraph = referenceExplorerNode->GetReferenceViewerGraph();

		if (UObject* rootAsset = referenceExplorerGraph->GetGraphRootNodeInfo().AssetData.GetAsset())
		{
			if (UObject* referencerAsset = referenceExplorerNode->GetAssetData().GetAsset())
			{
				if (rootAsset != referencerAsset)
				{
					if (UClass* referencerAssetClass = referencerAsset->GetClass())
					{
						for (TFieldIterator<FObjectPropertyBase> It(referencerAssetClass); It; ++It)
						{
							if (FObjectPropertyBase* objectProperty = *It)
							{
								UObject* value = objectProperty->GetObjectPropertyValue(objectProperty->ContainerPtrToValuePtr<void>(referencerAsset));
								if (value == rootAsset)
								{
									referencerProperties.Add(objectProperty->GetName());
								}
							}
						}
					}
				}
			}
		}
	}

	TSharedRef<SWidget> referencerPropertiesWidget = SNullWidget::NullWidget;

	if (referencerProperties.Num())
	{
		bool isFirst = true;

		FString referencerPropertiesList = "";

		for (const FString& referencerProperty : referencerProperties)
		{
			if (!referencerProperty.IsEmpty())
			{
				if (!isFirst)
				{
					referencerPropertiesList += ", ";
				}
				referencerPropertiesList += referencerProperty;
				isFirst = false;
			}
		}

		if (!referencerPropertiesList.IsEmpty())
		{
			referencerPropertiesWidget = SNew(STextBlock).Text(FText::FromString(referencerPropertiesList)).AutoWrapText(true);
		}
	}

	ContentScale.Bind(this, &SGraphNode_ReferenceExplorer::GetContentScale);
	GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SAssignNew(MainVerticalBox, SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder) // Background
						.ColorAndOpacity(OpacityColor)
						.BorderImage(FRefExplorerEditorModule::GetStyleSet()->GetBrush("Graph.Node.BodyBackground"))
						.Padding(0)
						[
							SNew(SBorder) // Outline
								.BorderBackgroundColor(this, &SGraphNode_ReferenceExplorer::GetNodeTitleColor)
								.Padding(0)
								[
									SNew(SVerticalBox)
										.ToolTipText(this, &SGraphNode_ReferenceExplorer::GetNodeTooltip)

										+ SVerticalBox::Slot()
										.AutoHeight()
										.HAlign(HAlign_Fill)
										.VAlign(VAlign_Top)
										.Padding(0)
										[
											SNew(SBorder)
												.Padding(FMargin(10.0f, 4.0f, 6.0f, 4.0f))
												.BorderImage(FRefExplorerEditorModule::GetStyleSet()->GetBrush("Graph.Node.ColorSpill"))
												.BorderBackgroundColor(this, &SGraphNode_ReferenceExplorer::GetNodeTitleColor)
												[
													SNew(SHorizontalBox)
														+ SHorizontalBox::Slot()
														.VAlign(VAlign_Center)
														.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
														.AutoWidth()
														[
															SNew(SImage)
																.Image(IconBrush)
																.DesiredSizeOverride(FVector2D(24.0, 24.0))
																.ColorAndOpacity(this, &SGraphNode::GetNodeTitleIconColor)
														]
														+ SHorizontalBox::Slot()
														.FillWidth(1.0)
														.VAlign(VAlign_Center)
														[
															SNew(SVerticalBox)
																+ SVerticalBox::Slot()
																.AutoHeight()
																.Padding(FMargin(0.f))
																.VAlign(VAlign_Center)
																[
																	SAssignNew(InlineEditableText, SInlineEditableTextBlock)
																		.Text(NodeTitle.Get(), &SNodeTitle::GetHeadTitle)
																		.OnVerifyTextChanged(this, &SGraphNode_ReferenceExplorer::OnVerifyNameTextChanged)
																		.OnTextCommitted(this, &SGraphNode_ReferenceExplorer::OnNameTextCommited)
																		.IsReadOnly(this, &SGraphNode_ReferenceExplorer::IsNameReadOnly)
																		.IsSelected(this, &SGraphNode_ReferenceExplorer::IsSelectedExclusively)
																]
																+ SVerticalBox::Slot()
																.AutoHeight()
																.Padding(FMargin(0.f))
																[
																	NodeTitle.ToSharedRef()
																]
														]
												]
										]
										+ SVerticalBox::Slot()
										.AutoHeight()
										.Padding(1.0f)
										[
											// POPUP ERROR MESSAGE
											SAssignNew(ErrorText, SErrorText)
												.BackgroundColor(this, &SGraphNode_ReferenceExplorer::GetErrorColor)
												.ToolTipText(this, &SGraphNode_ReferenceExplorer::GetErrorMsgToolTip)
										]
										+ SVerticalBox::Slot()
										.AutoHeight()
										.HAlign(HAlign_Fill)
										.VAlign(VAlign_Top)
										[
											// NODE CONTENT AREA
											SNew(SBorder)
												.BorderImage(FAppStyle::GetBrush("NoBorder"))
												.HAlign(HAlign_Fill)
												.VAlign(VAlign_Fill)
												.Padding(FMargin(0, 3))
												[
													SNew(SHorizontalBox)
														+ SHorizontalBox::Slot()
														.AutoWidth()
														.VAlign(VAlign_Center)
														[
															// LEFT
															SNew(SBox)
																.WidthOverride(40)
																[
																	SAssignNew(LeftNodeBox, SVerticalBox)
																]
														]

														+ SHorizontalBox::Slot()
														.VAlign(VAlign_Center)
														.HAlign(HAlign_Center)
														.FillWidth(1.0f)
														[
															SNew(SVerticalBox)

																+SVerticalBox::Slot().AutoHeight()
																[
																	// Thumbnail
																	ThumbnailWidget
																]

																+SVerticalBox::Slot().AutoHeight()
																[
																	referencerPropertiesWidget
																]
														]

														+ SHorizontalBox::Slot()
														.AutoWidth()
														.VAlign(VAlign_Center)
														[
															// RIGHT
															SNew(SBox)
																.WidthOverride(40)
																[
																	SAssignNew(RightNodeBox, SVerticalBox)
																]
														]
												]
										]
								] // Outline Border
						] // Background 
				]
		];

	// Create comment bubble if comment text is valid
	GetNodeObj()->bCommentBubbleVisible = !GetNodeObj()->NodeComment.IsEmpty();
	if (GetNodeObj()->ShouldMakeCommentBubbleVisible() && GetNodeObj()->bCommentBubbleVisible)
	{
		TSharedPtr<SCommentBubble> CommentBubble;

		SAssignNew(CommentBubble, SCommentBubble)
			.GraphNode(GraphNode)
			.Text(this, &SGraphNode::GetNodeComment);

		GetOrAddSlot(ENodeZone::TopCenter)
			.SlotOffset(TAttribute<FVector2D>(CommentBubble.Get(), &SCommentBubble::GetOffset))
			.SlotSize(TAttribute<FVector2D>(CommentBubble.Get(), &SCommentBubble::GetSize))
			.AllowScaling(TAttribute<bool>(CommentBubble.Get(), &SCommentBubble::IsScalingAllowed))
			.VAlign(VAlign_Top)
			[
				CommentBubble.ToSharedRef()
			];
	}

	ErrorReporting = ErrorText;
	ErrorReporting->SetError(ErrorMsg);
	CreateBelowWidgetControls(MainVerticalBox);

	CreatePinWidgets();
}

//------------------------------------------------------
// FReferenceExplorerGraphNodeFactory
//------------------------------------------------------

TSharedPtr<SGraphNode> FReferenceExplorerGraphNodeFactory::CreateNode(UEdGraphNode* InNode) const
{
	if (UEdGraphNode_ReferenceExplorer* referenceExplorerNode = Cast<UEdGraphNode_ReferenceExplorer>(InNode))
	{
		return SNew(SGraphNode_ReferenceExplorer, referenceExplorerNode);
	}

	return nullptr;
}

//--------------------------------------------------------------------
// SReferenceExplorer
//--------------------------------------------------------------------

bool IsAssetIdentifierPassingSearchTextFilter(const FAssetIdentifier& InNode, const TArray<FString>& InSearchWords)
{
	FString NodeString = InNode.ToString();
	for (const FString& Word : InSearchWords)
	{
		if (!NodeString.Contains(Word))
		{
			return false;
		}
	}

	return true;
}

SReferenceExplorer::~SReferenceExplorer()
{
	if (!GExitPurge)
	{
		if (ensure(GraphObj))
		{
			GraphObj->RemoveFromRoot();
		}
	}
}

void SReferenceExplorer::Construct(const FArguments& InArgs)
{
	// Create an action list and register commands
	RegisterActions();

	// Create the graph
	GraphObj = NewObject<UEdGraph_ReferenceExplorer>();
	GraphObj->Schema = UReferenceExplorerSchema::StaticClass();
	GraphObj->AddToRoot();
	GraphObj->SetReferenceViewer(StaticCastSharedRef<SReferenceExplorer>(AsShared()));

	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &SReferenceExplorer::OnNodeDoubleClicked);
	GraphEvents.OnCreateActionMenu = SGraphEditor::FOnCreateActionMenu::CreateSP(this, &SReferenceExplorer::OnCreateGraphActionMenu);

	// Create the graph editor
	GraphEditorPtr = SNew(SGraphEditor)
		.AdditionalCommands(ReferenceViewerActions)
		.GraphToEdit(GraphObj)
		.GraphEvents(GraphEvents)
		.ShowGraphStateOverlay(false);

	FEditorWidgetsModule& EditorWidgetsModule = FModuleManager::LoadModuleChecked<FEditorWidgetsModule>("EditorWidgets");
	TSharedRef<SWidget> AssetDiscoveryIndicator = EditorWidgetsModule.CreateAssetDiscoveryIndicator(EAssetDiscoveryIndicatorScaleMode::Scale_None, FMargin(16, 8), false);

	const FReferenceExplorerCommands& UICommands = FReferenceExplorerCommands::Get();

	static const FName DefaultForegroundName("DefaultForeground");

	// Visual options visibility
	bDirtyResults = false;

	ChildSlot
		[

			SNew(SVerticalBox)

				// Path and history
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
						[
							SNew(SHorizontalBox)

								+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(4, 0)
								[
									MakeToolBar()
								]

								+ SHorizontalBox::Slot()
								.Padding(0, 7, 4, 8)
								.FillWidth(1.0)
								.VAlign(VAlign_Fill)
								[
									SAssignNew(FindPathAssetPicker, SComboButton)
										.OnGetMenuContent(this, &SReferenceExplorer::GenerateFindPathAssetPickerMenu)
										.ButtonContent()
										[
											SNew(STextBlock)
												.Text_Lambda([this] { return FindPathAssetId.IsValid() ? FText::FromString(FindPathAssetId.ToString()) : LOCTEXT("ChooseTargetAsset", "Choose a target asset ... "); })
										]
								]
						]
				]

				// Graph
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				.HAlign(HAlign_Fill)
				[
					SNew(SOverlay)

						+ SOverlay::Slot()
						[
							GraphEditorPtr.ToSharedRef()
						]

						+ SOverlay::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						[
							SNew(SImage)
								.Image(FAppStyle::GetBrush("Brushes.Recessed"))
								.ColorAndOpacity_Lambda([this]() { return FLinearColor::Transparent; })
								.Visibility(EVisibility::HitTestInvisible)
						]

						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Padding(FMargin(24, 0, 24, 0))
						[
							AssetDiscoveryIndicator
						]

						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Bottom)
						.Padding(FMargin(0, 0, 0, 16))
						[
							SNew(STextBlock)
								.Text(this, &SReferenceExplorer::GetStatusText)
						]
				]
		];

		SetCanTick(true);
}

FReply SReferenceExplorer::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return ReferenceViewerActions->ProcessCommandBindings(InKeyEvent) ? FReply::Handled() : FReply::Unhandled();
}

void SReferenceExplorer::SetGraphRootIdentifier(const FAssetIdentifier& NewGraphRootIdentifier, const FReferenceViewerParams& ReferenceViewerParams)
{
	GraphObj->SetGraphRoot(NewGraphRootIdentifier);
	RebuildGraph();

	// Zoom once this frame to make sure widgets are visible, then zoom again so size is correct
	TriggerZoomToFit(0, 0);
	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SReferenceExplorer::TriggerZoomToFit));
}

EActiveTimerReturnType SReferenceExplorer::TriggerZoomToFit(double InCurrentTime, float InDeltaTime)
{
	if (GraphEditorPtr.IsValid())
	{
		GraphEditorPtr->ZoomToFit(false);
	}
	return EActiveTimerReturnType::Stop;
}

void SReferenceExplorer::RebuildGraph()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	if (AssetRegistryModule.Get().IsLoadingAssets())
	{
		// We are still discovering assets, listen for the completion delegate before building the graph
		if (!AssetRegistryModule.Get().OnFilesLoaded().IsBoundToObject(this))
		{
			AssetRegistryModule.Get().OnFilesLoaded().AddSP(this, &SReferenceExplorer::OnInitialAssetRegistrySearchComplete);
		}
	}
	else
	{
		// All assets are already discovered, build the graph now, if we have one
		if (GraphObj)
		{
			GraphObj->RebuildGraph();
		}

		bDirtyResults = false;
		if (!AssetRefreshHandle.IsValid())
		{
			// Listen for updates
			AssetRefreshHandle = AssetRegistryModule.Get().OnAssetUpdated().AddSP(this, &SReferenceExplorer::OnAssetRegistryChanged);
			AssetRegistryModule.Get().OnAssetAdded().AddSP(this, &SReferenceExplorer::OnAssetRegistryChanged);
			AssetRegistryModule.Get().OnAssetRemoved().AddSP(this, &SReferenceExplorer::OnAssetRegistryChanged);
		}
	}
}

void SReferenceExplorer::OnNodeDoubleClicked(UEdGraphNode* Node)
{
	if (GraphObj)
	{
		if (UEdGraphNode_ReferenceExplorer* referenceExplorerNode = Cast<UEdGraphNode_ReferenceExplorer>(Node))
		{
			GraphObj->SetGraphRoot(referenceExplorerNode->GetIdentifier());
			GraphObj->RebuildGraph();

			RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SReferenceExplorer::TriggerZoomToFit));
		}
	}
}

FActionMenuContent SReferenceExplorer::OnCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed)
{
	// no context menu when not over a node
	return FActionMenuContent();
}

void SReferenceExplorer::RefreshClicked()
{
	RebuildGraph();

	TriggerZoomToFit(0, 0);
	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SReferenceExplorer::TriggerZoomToFit));
}

FText SReferenceExplorer::GetStatusText() const
{
	FString DirtyPackages;

	if (GraphObj && GraphObj->CurrentGraphRootIdentifier.IsPackage())
	{
		FString PackageString = GraphObj->CurrentGraphRootIdentifier.PackageName.ToString();
		UPackage* InMemoryPackage = FindPackage(nullptr, *PackageString);
		if (InMemoryPackage && InMemoryPackage->IsDirty())
		{
			DirtyPackages += FPackageName::GetShortName(*PackageString);
		}
	}

	if (DirtyPackages.Len() > 0)
	{
		return FText::Format(LOCTEXT("ModifiedWarning", "Showing old saved references for edited asset {0}"), FText::FromString(DirtyPackages));
	}

	if (bDirtyResults)
	{
		return LOCTEXT("DirtyWarning", "Saved references changed, refresh for update");
	}

	return FText();
}

void SReferenceExplorer::RegisterActions()
{
	ReferenceViewerActions = MakeShareable(new FUICommandList);
	FReferenceExplorerCommands::Register();

	ReferenceViewerActions->MapAction(
		FReferenceExplorerCommands::Get().ZoomToFit,
		FExecuteAction::CreateSP(this, &SReferenceExplorer::ZoomToFit),
		FCanExecuteAction::CreateSP(this, &SReferenceExplorer::CanZoomToFit));

	ReferenceViewerActions->MapAction(
		FGlobalEditorCommonCommands::Get().FindInContentBrowser,
		FExecuteAction::CreateSP(this, &SReferenceExplorer::ShowSelectionInContentBrowser),
		FCanExecuteAction::CreateSP(this, &SReferenceExplorer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FReferenceExplorerCommands::Get().OpenSelectedInAssetEditor,
		FExecuteAction::CreateSP(this, &SReferenceExplorer::OpenSelectedInAssetEditor),
		FCanExecuteAction::CreateSP(this, &SReferenceExplorer::HasAtLeastOneRealNodeSelected));

	ReferenceViewerActions->MapAction(
		FReferenceExplorerCommands::Get().CopyReferencedObjects,
		FExecuteAction::CreateSP(this, &SReferenceExplorer::CopyReferencedObjects),
		FCanExecuteAction::CreateSP(this, &SReferenceExplorer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FReferenceExplorerCommands::Get().CopyReferencingObjects,
		FExecuteAction::CreateSP(this, &SReferenceExplorer::CopyReferencingObjects),
		FCanExecuteAction::CreateSP(this, &SReferenceExplorer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FReferenceExplorerCommands::Get().ShowReferencedObjects,
		FExecuteAction::CreateSP(this, &SReferenceExplorer::ShowReferencedObjects),
		FCanExecuteAction::CreateSP(this, &SReferenceExplorer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FReferenceExplorerCommands::Get().ShowReferencingObjects,
		FExecuteAction::CreateSP(this, &SReferenceExplorer::ShowReferencingObjects),
		FCanExecuteAction::CreateSP(this, &SReferenceExplorer::HasAtLeastOnePackageNodeSelected));

	ReferenceViewerActions->MapAction(
		FReferenceExplorerCommands::Get().ShowReferenceTree,
		FExecuteAction::CreateSP(this, &SReferenceExplorer::ShowReferenceTree),
		FCanExecuteAction::CreateSP(this, &SReferenceExplorer::HasExactlyOnePackageNodeSelected));
}

void SReferenceExplorer::ShowSelectionInContentBrowser()
{
	TArray<FAssetData> AssetList;

	// Build up a list of selected assets from the graph selection set
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator It(SelectedNodes); It; ++It)
	{
		if (UEdGraphNode_ReferenceExplorer* ReferenceNode = Cast<UEdGraphNode_ReferenceExplorer>(*It))
		{
			if (ReferenceNode->GetAssetData().IsValid())
			{
				AssetList.Add(ReferenceNode->GetAssetData());
			}
		}
	}

	if (AssetList.Num() > 0)
	{
		GEditor->SyncBrowserToObjects(AssetList);
	}
}

void SReferenceExplorer::OpenSelectedInAssetEditor()
{
	TArray<FAssetIdentifier> IdentifiersToEdit;
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator It(SelectedNodes); It; ++It)
	{
		if (UEdGraphNode_ReferenceExplorer* ReferenceNode = Cast<UEdGraphNode_ReferenceExplorer>(*It))
		{
			IdentifiersToEdit.Add(ReferenceNode->GetIdentifier());
		}
	}

	// This will handle packages as well as searchable names if other systems register
	FEditorDelegates::OnEditAssetIdentifiers.Broadcast(IdentifiersToEdit);
}

FString SReferenceExplorer::GetReferencedObjectsList() const
{
	FString ReferencedObjectsList;

	TSet<FName> AllSelectedPackageNames;
	GetPackageNamesFromSelectedNodes(AllSelectedPackageNames);

	if (AllSelectedPackageNames.Num() > 0)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		for (const FName& SelectedPackageName : AllSelectedPackageNames)
		{
			TArray<FName> HardDependencies;
			AssetRegistryModule.Get().GetDependencies(SelectedPackageName, HardDependencies, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);

			TArray<FName> SoftDependencies;
			AssetRegistryModule.Get().GetDependencies(SelectedPackageName, SoftDependencies, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Soft);

			ReferencedObjectsList += FString::Printf(TEXT("[%s - Dependencies]\n"), *SelectedPackageName.ToString());
			if (HardDependencies.Num() > 0)
			{
				ReferencedObjectsList += TEXT("  [HARD]\n");
				for (const FName& HardDependency : HardDependencies)
				{
					const FString PackageString = HardDependency.ToString();
					ReferencedObjectsList += FString::Printf(TEXT("    %s.%s\n"), *PackageString, *FPackageName::GetLongPackageAssetName(PackageString));
				}
			}
			if (SoftDependencies.Num() > 0)
			{
				ReferencedObjectsList += TEXT("  [SOFT]\n");
				for (const FName& SoftDependency : SoftDependencies)
				{
					const FString PackageString = SoftDependency.ToString();
					ReferencedObjectsList += FString::Printf(TEXT("    %s.%s\n"), *PackageString, *FPackageName::GetLongPackageAssetName(PackageString));
				}
			}
		}
	}

	return ReferencedObjectsList;
}

FString SReferenceExplorer::GetReferencingObjectsList() const
{
	FString ReferencingObjectsList;

	TSet<FName> AllSelectedPackageNames;
	GetPackageNamesFromSelectedNodes(AllSelectedPackageNames);

	if (AllSelectedPackageNames.Num() > 0)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		for (const FName& SelectedPackageName : AllSelectedPackageNames)
		{
			TArray<FName> HardDependencies;
			AssetRegistryModule.Get().GetReferencers(SelectedPackageName, HardDependencies, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);

			TArray<FName> SoftDependencies;
			AssetRegistryModule.Get().GetReferencers(SelectedPackageName, SoftDependencies, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Soft);

			ReferencingObjectsList += FString::Printf(TEXT("[%s - Referencers]\n"), *SelectedPackageName.ToString());
			if (HardDependencies.Num() > 0)
			{
				ReferencingObjectsList += TEXT("  [HARD]\n");
				for (const FName& HardDependency : HardDependencies)
				{
					const FString PackageString = HardDependency.ToString();
					ReferencingObjectsList += FString::Printf(TEXT("    %s.%s\n"), *PackageString, *FPackageName::GetLongPackageAssetName(PackageString));
				}
			}
			if (SoftDependencies.Num() > 0)
			{
				ReferencingObjectsList += TEXT("  [SOFT]\n");
				for (const FName& SoftDependency : SoftDependencies)
				{
					const FString PackageString = SoftDependency.ToString();
					ReferencingObjectsList += FString::Printf(TEXT("    %s.%s\n"), *PackageString, *FPackageName::GetLongPackageAssetName(PackageString));
				}
			}
		}
	}

	return ReferencingObjectsList;
}

void SReferenceExplorer::CopyReferencedObjects()
{
	const FString ReferencedObjectsList = GetReferencedObjectsList();
	FPlatformApplicationMisc::ClipboardCopy(*ReferencedObjectsList);
}

void SReferenceExplorer::CopyReferencingObjects()
{
	const FString ReferencingObjectsList = GetReferencingObjectsList();
	FPlatformApplicationMisc::ClipboardCopy(*ReferencingObjectsList);
}

void SReferenceExplorer::ShowReferencedObjects()
{
	const FString ReferencedObjectsList = GetReferencedObjectsList();
	SGenericDialogWidget::OpenDialog(LOCTEXT("ReferencedObjectsDlgTitle", "Referenced Objects"), SNew(STextBlock).Text(FText::FromString(ReferencedObjectsList)));
}

void SReferenceExplorer::ShowReferencingObjects()
{
	const FString ReferencingObjectsList = GetReferencingObjectsList();
	SGenericDialogWidget::OpenDialog(LOCTEXT("ReferencingObjectsDlgTitle", "Referencing Objects"), SNew(STextBlock).Text(FText::FromString(ReferencingObjectsList)));
}

void SReferenceExplorer::ShowReferenceTree()
{
	UObject* SelectedObject = GetObjectFromSingleSelectedNode();

	if (SelectedObject)
	{
		bool bObjectWasSelected = false;
		for (FSelectionIterator It(*GEditor->GetSelectedObjects()); It; ++It)
		{
			if ((*It) == SelectedObject)
			{
				GEditor->GetSelectedObjects()->Deselect(SelectedObject);
				bObjectWasSelected = true;
			}
		}

		ObjectTools::ShowReferenceGraph(SelectedObject);

		if (bObjectWasSelected)
		{
			GEditor->GetSelectedObjects()->Select(SelectedObject);
		}
	}
}

UObject* SReferenceExplorer::GetObjectFromSingleSelectedNode() const
{
	UObject* ReturnObject = nullptr;

	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	if (ensure(SelectedNodes.Num()) == 1)
	{
		UEdGraphNode_ReferenceExplorer* ReferenceNode = Cast<UEdGraphNode_ReferenceExplorer>(SelectedNodes.Array()[0]);
		if (ReferenceNode)
		{
			const FAssetData& AssetData = ReferenceNode->GetAssetData();
			if (AssetData.IsAssetLoaded())
			{
				ReturnObject = AssetData.GetAsset();
			}
			else
			{
				FScopedSlowTask SlowTask(0, LOCTEXT("LoadingSelectedObject", "Loading selection..."));
				SlowTask.MakeDialog();
				ReturnObject = AssetData.GetAsset();
			}
		}
	}

	return ReturnObject;
}

void SReferenceExplorer::GetPackageNamesFromSelectedNodes(TSet<FName>& OutNames) const
{
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	
	for (UObject* Node : SelectedNodes)
	{
		if (UEdGraphNode_ReferenceExplorer* ReferenceNode = Cast<UEdGraphNode_ReferenceExplorer>(Node))
		{
			if (ReferenceNode->GetIdentifier().IsPackage())
			{
				OutNames.Add(ReferenceNode->GetIdentifier().PackageName);
			}
		}
	}
}

bool SReferenceExplorer::HasExactlyOneNodeSelected() const
{
	if (GraphEditorPtr.IsValid())
	{
		return GraphEditorPtr->GetSelectedNodes().Num() == 1;
	}

	return false;
}

bool SReferenceExplorer::HasExactlyOnePackageNodeSelected() const
{
	if (GraphEditorPtr.IsValid())
	{
		if (GraphEditorPtr->GetSelectedNodes().Num() != 1)
		{
			return false;
		}

		TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
		for (UObject* Node : SelectedNodes)
		{
			UEdGraphNode_ReferenceExplorer* ReferenceNode = Cast<UEdGraphNode_ReferenceExplorer>(Node);
			if (ReferenceNode)
			{
				if (ReferenceNode->IsPackage())
				{
					return true;
				}
			}
			return false;
		}
	}

	return false;
}

bool SReferenceExplorer::HasAtLeastOnePackageNodeSelected() const
{
	if (GraphEditorPtr.IsValid())
	{
		TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
		for (UObject* Node : SelectedNodes)
		{
			UEdGraphNode_ReferenceExplorer* ReferenceNode = Cast<UEdGraphNode_ReferenceExplorer>(Node);
			if (ReferenceNode)
			{
				if (ReferenceNode->IsPackage())
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool SReferenceExplorer::HasAtLeastOneRealNodeSelected() const
{
	if (GraphEditorPtr.IsValid())
	{
		TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
		for (UObject* Node : SelectedNodes)
		{
			UEdGraphNode_ReferenceExplorer* ReferenceNode = Cast<UEdGraphNode_ReferenceExplorer>(Node);
			if (ReferenceNode)
			{
				return true;
			}
		}
	}

	return false;
}

void SReferenceExplorer::OnAssetRegistryChanged(const FAssetData& AssetData)
{
	// We don't do more specific checking because that data is not exposed, and it wouldn't handle newly added references anyway
	bDirtyResults = true;
}

void SReferenceExplorer::OnInitialAssetRegistrySearchComplete()
{
	if (GraphObj)
	{
		GraphObj->RebuildGraph();
	}
}

void SReferenceExplorer::ZoomToFit()
{
	if (GraphEditorPtr.IsValid())
	{
		GraphEditorPtr->ZoomToFit(true);
	}
}

bool SReferenceExplorer::CanZoomToFit() const
{
	if (GraphEditorPtr.IsValid())
	{
		return true;
	}

	return false;
}

TSharedRef<SWidget> SReferenceExplorer::GetShowMenuContent()
{
	FMenuBuilder MenuBuilder(true, ReferenceViewerActions);
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SReferenceExplorer::MakeToolBar()
{
	FToolBarBuilder ToolBarBuilder(ReferenceViewerActions, FMultiBoxCustomization::None, TSharedPtr<FExtender>(), true);
	
	//////ToolBarBuilder.SetStyle(&FReferenceViewerStyle::Get(), "AssetEditorToolbar");
	//////ToolBarBuilder.BeginSection("Test");

	//////ToolBarBuilder.AddToolBarButton(
	//////	FUIAction(FExecuteAction::CreateSP(this, &SReferenceExplorer::RefreshClicked)),
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>(),
	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.Refresh"));

	//////ToolBarBuilder.AddToolBarButton(
	//////	FUIAction(
	//////		FExecuteAction::CreateSP(this, &SReferenceExplorer::BackClicked),
	//////		FCanExecuteAction::CreateSP(this, &SReferenceExplorer::IsBackEnabled)
	//////	),
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>::CreateSP(this, &SReferenceExplorer::GetHistoryBackTooltip),
	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.ArrowLeft"));

	//////ToolBarBuilder.AddToolBarButton(
	//////	FUIAction(
	//////		FExecuteAction::CreateSP(this, &SReferenceExplorer::ForwardClicked),
	//////		FCanExecuteAction::CreateSP(this, &SReferenceExplorer::IsForwardEnabled)
	//////	),
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>::CreateSP(this, &SReferenceExplorer::GetHistoryForwardTooltip),
	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.ArrowRight"));

	//////ToolBarBuilder.AddToolBarButton(FReferenceExplorerCommands::Get().FindPath,
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>(),
	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "BlueprintEditor.FindInBlueprint"));

	//////ToolBarBuilder.AddSeparator();

	//////ToolBarBuilder.AddComboButton(
	//////	FUIAction(),
	//////	FOnGetContent::CreateSP(this, &SReferenceExplorer::GetShowMenuContent),
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>(),
	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.Visibility"),
	//////	/*bInSimpleComboBox*/ false);

	//////ToolBarBuilder.AddToolBarButton(FReferenceExplorerCommands::Get().ShowDuplicates,
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>::CreateLambda([this]() -> FText
	//////		{
	//////			if (Settings->GetFindPathEnabled())
	//////			{
	//////				return LOCTEXT("DuplicatesDisabledTooltip", "Duplicates are always shown when using the Find Path tool.");
	//////			}

	//////			return FReferenceExplorerCommands::Get().ShowDuplicates->GetDescription();
	//////		}),

	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.Duplicate"));

	//////ToolBarBuilder.AddSeparator();

	//////ToolBarBuilder.AddToolBarButton(FReferenceExplorerCommands::Get().Filters,
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>::CreateLambda([this]() -> FText
	//////		{
	//////			if (Settings->GetFindPathEnabled())
	//////			{
	//////				return LOCTEXT("FiltersDisabledTooltip", "Filtering is disabled when using the Find Path tool.");
	//////			}

	//////			return FReferenceExplorerCommands::Get().Filters->GetDescription();
	//////		}),

	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.Filters"));

	//////ToolBarBuilder.AddToolBarButton(FReferenceExplorerCommands::Get().AutoFilters,
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>::CreateLambda([this]() -> FText
	//////		{
	//////			if (Settings->GetFindPathEnabled())
	//////			{
	//////				return LOCTEXT("AutoFiltersDisabledTooltip", "AutoFiltering is disabled when using the Find Path tool.");
	//////			}

	//////			return FReferenceExplorerCommands::Get().AutoFilters->GetDescription();
	//////		}),

	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.AutoFilters"));

	//////ToolBarBuilder.EndSection();


	return ToolBarBuilder.MakeWidget();
}

TSharedRef<SWidget> SReferenceExplorer::GenerateFindPathAssetPickerMenu()
{
	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.Filter.bRecursiveClasses = true;
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SReferenceExplorer::OnFindPathAssetSelected);
	AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateSP(this, &SReferenceExplorer::OnFindPathAssetEnterPressed);
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.bAllowNullSelection = true;
	AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
	AssetPickerConfig.bAllowDragging = false;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));


	return SNew(SBox)
		.HeightOverride(500)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Menu.Background"))
				[
					ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
				]
		];
}

void SReferenceExplorer::OnFindPathAssetSelected(const FAssetData& AssetData)
{
	FindPathAssetPicker->SetIsOpen(false);

	GraphObj->SetGraphRoot(FAssetIdentifier(AssetData.PackageName));
	GraphObj->RebuildGraph();

	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SReferenceExplorer::TriggerZoomToFit));
}

void SReferenceExplorer::OnFindPathAssetEnterPressed(const TArray<FAssetData>& AssetData)
{
	FindPathAssetPicker->SetIsOpen(false);

	if (!AssetData.IsEmpty())
	{
		GraphObj->SetGraphRoot(FAssetIdentifier(AssetData[0].PackageName));
		GraphObj->RebuildGraph();
	}

	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SReferenceExplorer::TriggerZoomToFit));
}

namespace FRefExplorerEditorModule_PRIVATE
{
	const FName ReferenceExplorer("FRefExplorerEditorModule_Tabs_ReferenceExplorer");

	//--------------------------------------------------------------------
	// FContentBrowserSelectionMenuExtender
	//--------------------------------------------------------------------

	template<class T>
	class FContentBrowserSelectionMenuExtender : public FRefExplorerEditorModule::IContentBrowserSelectionMenuExtender, public TSharedFromThis<FContentBrowserSelectionMenuExtender<T>>
	{
	public:
		FContentBrowserSelectionMenuExtender(const FText& label, const FText& toolTip, const FName styleSetName, const FName iconName)
			: Label(label), ToolTip(toolTip), StyleSetName(styleSetName), IconName(iconName)
		{}

		virtual ~FContentBrowserSelectionMenuExtender() = default;

		virtual void Extend() override
		{
			FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
			ContentBrowserModule.GetAllAssetViewContextMenuExtenders().Add(FContentBrowserMenuExtender_SelectedAssets::CreateSP(this, &FContentBrowserSelectionMenuExtender::CreateExtender));
		}

	protected:
		virtual void Execute(FAssetIdentifier assetIdentifier) const = 0;

	private:
		TSharedRef<FExtender> CreateExtender(const TArray<FAssetData>& SelectedAssets)
		{
			TSharedRef<FExtender> Extender = MakeShared<FExtender>();

			Extender->AddMenuExtension(
				"GetAssetActions",
				EExtensionHook::After,
				nullptr,
				FMenuExtensionDelegate::CreateSP(this, &FContentBrowserSelectionMenuExtender::AddMenuExtension, SelectedAssets)
			);

			return Extender;
		}

		void AddMenuExtension(FMenuBuilder& MenuBuilder, TArray<FAssetData> SelectedAssets)
		{
			if (SelectedAssets.Num() != 1) return;

			UObject* asset = SelectedAssets[0].GetAsset();

			if (!asset) return;

			T* typedAsset = Cast<T>(asset);

			if (!typedAsset) return;

			MenuBuilder.AddMenuEntry(
				Label,
				ToolTip,
				FSlateIcon(StyleSetName, IconName),
				FUIAction(FExecuteAction::CreateSP(this, &FContentBrowserSelectionMenuExtender::Execute, FAssetIdentifier(SelectedAssets[0].PackageName)), FCanExecuteAction())
			);
		}

	protected:
		const FText Label;
		const FText ToolTip;
		const FName StyleSetName;
		const FName IconName;
	};

	//--------------------------------------------------------------------
	// FContentBrowserSelectionMenuExtender_ReferenceExplorer
	//--------------------------------------------------------------------

	class FContentBrowserSelectionMenuExtender_ReferenceExplorer : public FContentBrowserSelectionMenuExtender<UObject>
	{
	public:
		FContentBrowserSelectionMenuExtender_ReferenceExplorer(const FText& label, const FText& toolTip, const FName styleSetName, const FName iconName)
			: FContentBrowserSelectionMenuExtender(label, toolTip, styleSetName, iconName)
		{}

	protected:
		virtual void Execute(FAssetIdentifier assetIdentifier) const override
		{
			if (TSharedPtr<SDockTab> NewTab = FGlobalTabmanager::Get()->TryInvokeTab(ReferenceExplorer))
			{
				TSharedRef<SReferenceExplorer> ReferenceViewer = StaticCastSharedRef<SReferenceExplorer>(NewTab->GetContent());
				ReferenceViewer->SetGraphRootIdentifier(assetIdentifier, FReferenceViewerParams());
			}
		}
	};
}

//------------------------------------------------------
// FRefExplorerEditorModule
//------------------------------------------------------

TSharedPtr<FSlateStyleSet> FRefExplorerEditorModule::StyleSet;

void FRefExplorerEditorModule::StartupModule()
{
	StartupStyle();

	ContentBrowserSelectionMenuExtenders.Add(MakeShareable(new FRefExplorerEditorModule_PRIVATE::FContentBrowserSelectionMenuExtender_ReferenceExplorer(
		LOCTEXT("FContentBrowserSelectionMenuExtender_ReferenceExplorer_Label", "Reference Explorer"),
		LOCTEXT("FContentBrowserSelectionMenuExtender_ReferenceExplorer_ToolTip", "Explore and edit properties that are referencing selected asset"),
		GetStyleSetName(),
		GetContextMenuReferenceExplorerIconName()
	)));

	for (const TSharedPtr<IContentBrowserSelectionMenuExtender>& extender : ContentBrowserSelectionMenuExtenders)
	{
		if (extender.IsValid())
		{
			extender->Extend();
		}
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FRefExplorerEditorModule_PRIVATE::ReferenceExplorer, FOnSpawnTab::CreateRaw(this, &FRefExplorerEditorModule::OnSpawnTab));

	ReferenceExplorerGraphNodeFactory = MakeShareable(new FReferenceExplorerGraphNodeFactory());
	FEdGraphUtilities::RegisterVisualNodeFactory(ReferenceExplorerGraphNodeFactory);
}

void FRefExplorerEditorModule::ShutdownModule()
{
	FEdGraphUtilities::UnregisterVisualNodeFactory(ReferenceExplorerGraphNodeFactory);
	ReferenceExplorerGraphNodeFactory.Reset();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FRefExplorerEditorModule_PRIVATE::ReferenceExplorer);

	ContentBrowserSelectionMenuExtenders.Empty();

	ShutdownStyle();
}

FName FRefExplorerEditorModule::GetStyleSetName() { return "FRefExplorerEditorModule_Style"; }

FName FRefExplorerEditorModule::GetContextMenuReferenceExplorerIconName() { return "FRefExplorerEditorModule_Style_ContextMenu_ReferenceExplorer"; }

void FRefExplorerEditorModule::StartupStyle()
{
	const FVector2D Icon20x20(20.0f, 20.0f);

	StyleSet = MakeShareable(new FSlateStyleSet(GetStyleSetName()));

	if (const TSharedPtr<IPlugin> plugin = IPluginManager::Get().FindPlugin("RefExplorer"))
	{
		StyleSet->SetContentRoot(plugin->GetBaseDir() / TEXT("Resources"));
	}
	else
	{
		StyleSet->SetContentRoot(FPaths::ProjectPluginsDir() / TEXT("RefExplorer/Resources"));
	}

	StyleSet->Set(GetContextMenuReferenceExplorerIconName(), new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("Icon128"), TEXT(".png")), Icon20x20));

	const int BodyRadius = 10.0; // Designed for 4 but using 10 to accomodate the shared selection border.  Update to 4 all nodes get aligned.
	const FLinearColor SpillColor(.3, .3, .3, 1.0);

	StyleSet->Set("Graph.Node.BodyBackground", new FSlateRoundedBoxBrush(FStyleColors::Panel, BodyRadius));
	StyleSet->Set("Graph.Node.ColorSpill", new FSlateRoundedBoxBrush(SpillColor, FVector4(BodyRadius, BodyRadius, 0.0, 0.0)));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

void FRefExplorerEditorModule::ShutdownStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
	ensure(StyleSet.IsUnique());

	StyleSet.Reset();
}

TSharedRef<SDockTab> FRefExplorerEditorModule::OnSpawnTab(const FSpawnTabArgs& SpawnTabArgs)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab).TabRole(ETabRole::NomadTab);
	DockTab->SetContent(SNew(SReferenceExplorer));
	return DockTab;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FRefExplorerEditorModule, RefExplorerEditor)