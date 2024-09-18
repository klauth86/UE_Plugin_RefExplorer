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

#define FONT(...) FSlateFontInfo(FCoreStyle::GetDefaultFont(), __VA_ARGS__)

//--------------------------------------------------------------------
// COMMON
//--------------------------------------------------------------------

namespace FRefExplorerEditorModule_PRIVATE
{
	const FSlateFontInfo Small(FONT(8, "Regular"));
	const FSlateFontInfo SmallBold(FONT(8, "Bold"));

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

	const FString CATEGORY_DEFAULT = "Default";

	FString GetCategory(FField* field) { return field && field->HasMetaData("Category") ? field->GetMetaData("Category") : CATEGORY_DEFAULT; }

	struct FRefPropInfo
	{
		const FString Name;
		const FString Category;

		FRefPropInfo(const FString& name, const FString& category = "") : Name(name), Category(category) {}
	};

	void FindRecursive(UStruct* generatedClass, void* containerOwner, UObject* rootAsset, TArray<FRefPropInfo>& refPropInfos)
	{
		for (TFieldIterator<FStructProperty> It(generatedClass); It; ++It)
		{
			FStructProperty* structProperty = *It;

			void* structValue = structProperty->ContainerPtrToValuePtr<void>(containerOwner);

			for (TFieldIterator<FObjectPropertyBase> internalIt(structProperty->Struct); internalIt; ++internalIt)
			{
				if (FObjectPropertyBase* objectProperty = *internalIt)
				{
					UObject* objectPropertyValue = objectProperty->GetObjectPropertyValue(objectProperty->ContainerPtrToValuePtr<void>(structValue));

					if (objectPropertyValue == rootAsset)
					{
						refPropInfos.Add(FRefPropInfo(structProperty->GetDisplayNameText().ToString(), GetCategory(structProperty)));
					}

					if (UBlueprint* rootBlueprint = Cast<UBlueprint>(rootAsset))
					{
						if (objectPropertyValue == rootBlueprint->GeneratedClass)
						{
							refPropInfos.Add(FRefPropInfo(structProperty->GetDisplayNameText().ToString(), GetCategory(structProperty)));
						}
					}
				}
			}

			FindRecursive(structProperty->Struct, structProperty, rootAsset, refPropInfos);
		}
	}
}
//--------------------------------------------------------------------
// FRefExplorerCommands
//--------------------------------------------------------------------

class FRefExplorerCommands : public TCommands<FRefExplorerCommands>
{
public:
	FRefExplorerCommands() : TCommands<FRefExplorerCommands>(
		"RefExplorerCommands", NSLOCTEXT("Contexts", "RefExplorerCommands", "Ref Explorer"),
		NAME_None, FAppStyle::GetAppStyleSetName())
	{}

	// TCommands<> interface
	virtual void RegisterCommands() override
	{
		UI_COMMAND(OpenSelectedInAssetEditor, "Edit...", "Opens the selected asset in the relevant editor.", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::E));
		UI_COMMAND(ZoomToFit, "Zoom to Fit", "Zoom in and center the view on the selected item", EUserInterfaceActionType::Button, FInputChord(EKeys::F));

		UI_COMMAND(CopyReferencedObjects, "Copy Referenced Objects List", "Copies the list of objects that the selected asset references to the clipboard.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(CopyReferencingObjects, "Copy Referencing Objects List", "Copies the list of objects that reference the selected asset to the clipboard.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ShowReferencedObjects, "Show Referenced Objects List", "Shows a list of objects that the selected asset references.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ShowReferencingObjects, "Show Referencing Objects List", "Shows a list of objects that reference the selected asset.", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(ShowReferenceTree, "Show Reference Tree", "Shows a reference tree for the selected asset.", EUserInterfaceActionType::Button, FInputChord());
	}
	// End of TCommands<> interface

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
// FRefExplorerConnectionDrawingPolicy
//--------------------------------------------------------------------

class FRefExplorerConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
public:
	FRefExplorerConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements)
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
// URefExplorerSchema
//--------------------------------------------------------------------

URefExplorerSchema::URefExplorerSchema(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

void URefExplorerSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	{
		FToolMenuSection& Section = Menu->AddSection(TEXT("Asset"), NSLOCTEXT("RefExplorerSchema", "AssetSectionLabel", "Asset"));
		Section.AddMenuEntry(FGlobalEditorCommonCommands::Get().FindInContentBrowser);
		Section.AddMenuEntry(FRefExplorerCommands::Get().OpenSelectedInAssetEditor);
	}

	{
		FToolMenuSection& Section = Menu->AddSection(TEXT("Misc"), NSLOCTEXT("RefExplorerSchema", "MiscSectionLabel", "Misc"));
		Section.AddMenuEntry(FRefExplorerCommands::Get().ZoomToFit);
	}

	{
		FToolMenuSection& Section = Menu->AddSection(TEXT("References"), NSLOCTEXT("RefExplorerSchema", "ReferencesSectionLabel", "References"));
		Section.AddMenuEntry(FRefExplorerCommands::Get().CopyReferencedObjects);
		Section.AddMenuEntry(FRefExplorerCommands::Get().CopyReferencingObjects);
		Section.AddMenuEntry(FRefExplorerCommands::Get().ShowReferencedObjects);
		Section.AddMenuEntry(FRefExplorerCommands::Get().ShowReferencingObjects);
		Section.AddMenuEntry(FRefExplorerCommands::Get().ShowReferenceTree);
	}
}

FLinearColor URefExplorerSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return FRefExplorerEditorModule_PRIVATE::GetColor(FRefExplorerEditorModule_PRIVATE::ParseDependencyPinCategory(PinType.PinCategory));
}

FConnectionDrawingPolicy* URefExplorerSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) const
{
	return new FRefExplorerConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements);
}

//--------------------------------------------------------------------
// UEdGraphNode_RefExplorer
//--------------------------------------------------------------------

UEdGraphNode_RefExplorer::UEdGraphNode_RefExplorer(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	bUsesThumbnail = false;
	bIsPackage = false;
	bIsPrimaryAsset = false;

	AssetTypeColor = FLinearColor(0.55f, 0.55f, 0.55f);

	DependencyPin = NULL;
	ReferencerPin = NULL;
}

void UEdGraphNode_RefExplorer::SetupRefExplorerNode(const FIntPoint& NodeLoc, const FAssetIdentifier& NewIdentifier, const FAssetData& InAssetData)
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

void UEdGraphNode_RefExplorer::AddReferencer(UEdGraphNode_RefExplorer* ReferencerNode)
{
	UEdGraphPin* ReferencerDependencyPin = ReferencerNode->GetDependencyPin();

	if (ensure(ReferencerDependencyPin))
	{
		ReferencerDependencyPin->bHidden = false;
		ReferencerPin->bHidden = false;
		ReferencerPin->MakeLinkTo(ReferencerDependencyPin);
	}
}

UEdGraph_RefExplorer* UEdGraphNode_RefExplorer::GetRefExplorerGraph() const { return Cast<UEdGraph_RefExplorer>(GetGraph()); }

FLinearColor UEdGraphNode_RefExplorer::GetNodeTitleColor() const
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

FText UEdGraphNode_RefExplorer::GetTooltipText() const { return FText::FromString(Identifier.ToString()); }

void UEdGraphNode_RefExplorer::AllocateDefaultPins()
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
// UEdGraph_RefExplorer
//--------------------------------------------------------------------

UEdGraph_RefExplorer::UEdGraph_RefExplorer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (!IsTemplate())
	{
		AssetThumbnailPool = MakeShareable(new FAssetThumbnailPool(1024));
	}
}

void UEdGraph_RefExplorer::BeginDestroy()
{
	AssetThumbnailPool.Reset();

	Super::BeginDestroy();
}

void UEdGraph_RefExplorer::SetGraphRoot(const FAssetIdentifier& GraphRootIdentifier, const FIntPoint& GraphRootOrigin)
{
	CurrentGraphRootIdentifier = GraphRootIdentifier;
	CurrentGraphRootOrigin = GraphRootOrigin;
	UAssetManager::Get().UpdateManagementDatabase();
}

UEdGraphNode_RefExplorer* UEdGraph_RefExplorer::RebuildGraph()
{
	RemoveAllNodes();

	RefExplorerNodeInfos.Reset();
	RefExplorerNodeInfos.FindOrAdd(CurrentGraphRootIdentifier, FRefExplorerNodeInfo(CurrentGraphRootIdentifier));

	TMap<FAssetIdentifier, FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory> ReferenceLinks;
	GetSortedLinks(CurrentGraphRootIdentifier, ReferenceLinks);

	RefExplorerNodeInfos[CurrentGraphRootIdentifier].Children.Reserve(ReferenceLinks.Num());

	for (const TPair<FAssetIdentifier, FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory>& Pair : ReferenceLinks)
	{
		const FAssetIdentifier ChildId = Pair.Key;

		if (!RefExplorerNodeInfos.Contains(ChildId))
		{
			FRefExplorerNodeInfo& NewNodeInfo = RefExplorerNodeInfos.FindOrAdd(ChildId, FRefExplorerNodeInfo(ChildId));
			RefExplorerNodeInfos[ChildId].Parents.FindOrAdd(CurrentGraphRootIdentifier);
			RefExplorerNodeInfos[CurrentGraphRootIdentifier].Children.Emplace(Pair);
		}
		else if (!RefExplorerNodeInfos[ChildId].Parents.Contains(CurrentGraphRootIdentifier))
		{
			RefExplorerNodeInfos[ChildId].Parents.FindOrAdd(CurrentGraphRootIdentifier);
			RefExplorerNodeInfos[CurrentGraphRootIdentifier].Children.Emplace(Pair);
		}
	}

	TSet<FName> AllPackageNames;

	for (TPair<FAssetIdentifier, FRefExplorerNodeInfo>& InfoPair : RefExplorerNodeInfos)
	{
		if (!InfoPair.Key.IsValue() && !InfoPair.Key.PackageName.IsNone())
		{
			AllPackageNames.Add(InfoPair.Key.PackageName);
		}
	}

	TMap<FName, FAssetData> PackagesToAssetDataMap;
	UE::AssetRegistry::GetAssetForPackages(AllPackageNames.Array(), PackagesToAssetDataMap);

	TSet<FTopLevelAssetPath> AllClasses;
	for (TPair<FAssetIdentifier, FRefExplorerNodeInfo>& InfoPair : RefExplorerNodeInfos)
	{
		InfoPair.Value.AssetData = PackagesToAssetDataMap.FindRef(InfoPair.Key.PackageName);
		if (InfoPair.Value.AssetData.IsValid())
		{
			AllClasses.Add(InfoPair.Value.AssetData.AssetClassPath);
		}
	}

	UEdGraphNode_RefExplorer* RootNode = nullptr;

	if (!RefExplorerNodeInfos.IsEmpty())
	{
		const FRefExplorerNodeInfo& NodeInfo = RefExplorerNodeInfos[CurrentGraphRootIdentifier];
		RootNode = Cast<UEdGraphNode_RefExplorer>(CreateNode(UEdGraphNode_RefExplorer::StaticClass(), false));
		RootNode->SetupRefExplorerNode(CurrentGraphRootOrigin, CurrentGraphRootIdentifier, NodeInfo.AssetData);

		// References
		RecursivelyCreateNodes(CurrentGraphRootIdentifier, CurrentGraphRootOrigin, CurrentGraphRootIdentifier, RootNode, RefExplorerNodeInfos, /*bIsRoot*/ true);
	}

	NotifyGraphChanged();

	return RootNode;
}

void UEdGraph_RefExplorer::GetSortedLinks(const FAssetIdentifier& GraphRootIdentifier, TMap<FAssetIdentifier, FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory>& OutLinks) const
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
	for (const FAssetDependency& LinkToAsset : LinksToAsset)
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

void UEdGraph_RefExplorer::GatherAssetData(TMap<FAssetIdentifier, FRefExplorerNodeInfo>& InNodeInfos)
{
	// Grab the list of packages
	TSet<FName> PackageNames;
	for (TPair<FAssetIdentifier, FRefExplorerNodeInfo>& InfoPair : InNodeInfos)
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
	for (TPair<FAssetIdentifier, FRefExplorerNodeInfo>& InfoPair : InNodeInfos)
	{
		InfoPair.Value.AssetData = PackagesToAssetDataMap.FindRef(InfoPair.Key.PackageName);
	}
}

UEdGraphNode_RefExplorer* UEdGraph_RefExplorer::RecursivelyCreateNodes(const FAssetIdentifier& InAssetId, const FIntPoint& InNodeLoc, const FAssetIdentifier& InParentId, UEdGraphNode_RefExplorer* InParentNode, TMap<FAssetIdentifier, FRefExplorerNodeInfo>& InNodeInfos, bool bIsRoot)
{
	check(InNodeInfos.Contains(InAssetId));

	const FRefExplorerNodeInfo& NodeInfo = InNodeInfos[InAssetId];

	UEdGraphNode_RefExplorer* NewNode = nullptr;
	
	if (bIsRoot)
	{
		NewNode = InParentNode;
	}
	else
	{
		NewNode = Cast<UEdGraphNode_RefExplorer>(CreateNode(UEdGraphNode_RefExplorer::StaticClass(), false));
		NewNode->SetupRefExplorerNode(InNodeLoc, { InAssetId }, NodeInfo.AssetData);
	}

	FIntPoint ChildLoc = InNodeLoc;

	const int32 WidthStep = 256;
	const int32 HeightStep = 400;
	
	if (InNodeInfos[InAssetId].Children.Num() > 0)
	{
		const float DeltaAngle = UE_PI / InNodeInfos[InAssetId].Children.Num();
		const float LastAngle = DeltaAngle * (InNodeInfos[InAssetId].Children.Num() == 1 ? 0 : (InNodeInfos[InAssetId].Children.Num() - 1));		
		const float Radius = HeightStep / FMath::Max(FMath::Abs(1 - FMath::Cos(DeltaAngle)), FMath::Abs(FMath::Sin(DeltaAngle)));

		for (int32 ChildIdx = 0; ChildIdx < InNodeInfos[InAssetId].Children.Num(); ChildIdx++)
		{
			const TPair<FAssetIdentifier, FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory>& Pair = InNodeInfos[InAssetId].Children[ChildIdx];

			FAssetIdentifier ChildId = Pair.Key;

			const float AccumAngle = ChildIdx * DeltaAngle;

			ChildLoc.X = InNodeLoc.X - (FMath::Min(ChildIdx, InNodeInfos[InAssetId].Children.Num() - ChildIdx - 1) + 1) * WidthStep;
			ChildLoc.Y = InNodeLoc.Y - Radius * FMath::Sin(AccumAngle + (UE_PI - LastAngle / 2));

			UEdGraphNode_RefExplorer* ChildNode = RecursivelyCreateNodes(ChildId, ChildLoc, InAssetId, NewNode, InNodeInfos);

			ChildNode->GetDependencyPin()->PinType.PinCategory = FRefExplorerEditorModule_PRIVATE::GetName(Pair.Value);
			NewNode->AddReferencer(ChildNode);
		}
	}

	return NewNode;
}

const TSharedPtr<FAssetThumbnailPool>& UEdGraph_RefExplorer::GetAssetThumbnailPool() const
{
	return AssetThumbnailPool;
}

void UEdGraph_RefExplorer::RemoveAllNodes()
{
	TArray<UEdGraphNode*> NodesToRemove = Nodes;
	for (int32 NodeIndex = 0; NodeIndex < NodesToRemove.Num(); ++NodeIndex)
	{
		RemoveNode(NodesToRemove[NodeIndex]);
	}
}

//------------------------------------------------------
// SGraphNode_RefExplorer
//------------------------------------------------------

class SGraphNode_RefExplorer : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGraphNode_RefExplorer) {}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, UEdGraphNode_RefExplorer* InNode);

	// SGraphNode implementation
	virtual void UpdateGraphNode() override;
	virtual bool IsNodeEditable() const override { return false; }
	// End SGraphNode implementation

private:
	TSharedPtr<class FAssetThumbnail> AssetThumbnail;
};

void SGraphNode_RefExplorer::Construct(const FArguments& InArgs, UEdGraphNode_RefExplorer* InNode)
{
	const int32 ThumbnailSize = 128;

	if (InNode->UsesThumbnail())
	{
		// Create a thumbnail from the graph's thumbnail pool
		TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool = InNode->GetRefExplorerGraph()->GetAssetThumbnailPool();
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
void SGraphNode_RefExplorer::UpdateGraphNode()
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
	UEdGraphNode_RefExplorer* RefGraphNode = CastChecked<UEdGraphNode_RefExplorer>(GraphNode);

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

	TArray<FRefExplorerEditorModule_PRIVATE::FRefPropInfo> refPropInfos;

	if (UEdGraphNode_RefExplorer* RefExplorerNode = Cast<UEdGraphNode_RefExplorer>(GraphNode))
	{
		UEdGraph_RefExplorer* RefExplorerGraph = RefExplorerNode->GetRefExplorerGraph();

		if (UObject* rootAsset = RefExplorerGraph->GetGraphRootNodeInfo().AssetData.GetAsset())
		{
			if (UObject* refAsset = RefExplorerNode->GetAssetData().GetAsset())
			{
				if (rootAsset != refAsset)
				{
					if (UBlueprint* refBlueprint = Cast<UBlueprint>(refAsset))
					{
						UObject* genClassDefaultObject = refBlueprint->GeneratedClass->GetDefaultObject();

						for (TFieldIterator<FObjectPropertyBase> It(refBlueprint->GeneratedClass); It; ++It)
						{
							if (FObjectPropertyBase* objectProperty = *It)
							{
								UObject* objectPropertyValue = objectProperty->GetObjectPropertyValue(objectProperty->ContainerPtrToValuePtr<void>(genClassDefaultObject));

								if (objectPropertyValue == rootAsset)
								{
									refPropInfos.Add(FRefExplorerEditorModule_PRIVATE::FRefPropInfo(objectProperty->GetDisplayNameText().ToString(), FRefExplorerEditorModule_PRIVATE::GetCategory(objectProperty)));
								}

								if (UBlueprint* rootBlueprint = Cast<UBlueprint>(rootAsset))
								{
									if (objectPropertyValue == rootBlueprint->GeneratedClass)
									{
										refPropInfos.Add(FRefExplorerEditorModule_PRIVATE::FRefPropInfo(objectProperty->GetDisplayNameText().ToString(), FRefExplorerEditorModule_PRIVATE::GetCategory(objectProperty)));
									}
								}
							}
						}

						if (UScriptStruct* scriptStruct = Cast<UScriptStruct>(rootAsset))
						{
							for (TFieldIterator<FStructProperty> It(refBlueprint->GeneratedClass); It; ++It)
							{
								FStructProperty* structProperty = *It;

								if (structProperty->Struct == scriptStruct)
								{
									refPropInfos.Add(FRefExplorerEditorModule_PRIVATE::FRefPropInfo(structProperty->GetDisplayNameText().ToString(), FRefExplorerEditorModule_PRIVATE::GetCategory(structProperty)));
								}
							}
						}
						else
						{
							FRefExplorerEditorModule_PRIVATE::FindRecursive(refBlueprint->GeneratedClass, genClassDefaultObject, rootAsset, refPropInfos);
						}
					}
					else if (UScriptStruct* refStruct = Cast<UScriptStruct>(refAsset))
					{
						const int32 structureSize = refStruct->GetStructureSize();
						uint8* structDefault = new uint8[structureSize];
						refStruct->InitializeDefaultValue(structDefault);

						for (TFieldIterator<FObjectPropertyBase> It(refStruct); It; ++It)
						{
							if (FObjectPropertyBase* objectProperty = *It)
							{
								UObject* value = objectProperty->GetObjectPropertyValue(objectProperty->ContainerPtrToValuePtr<void>(structDefault));

								if (value == rootAsset)
								{
									refPropInfos.Add(FRefExplorerEditorModule_PRIVATE::FRefPropInfo(objectProperty->GetDisplayNameText().ToString(), FRefExplorerEditorModule_PRIVATE::GetCategory(objectProperty)));
								}
							}
						}

						if (UScriptStruct* scriptStruct = Cast<UScriptStruct>(rootAsset))
						{
							for (TFieldIterator<FStructProperty> It(refStruct); It; ++It)
							{
								FStructProperty* structProperty = *It;

								if (structProperty->Struct == scriptStruct)
								{
									refPropInfos.Add(FRefExplorerEditorModule_PRIVATE::FRefPropInfo(structProperty->GetDisplayNameText().ToString(), FRefExplorerEditorModule_PRIVATE::GetCategory(structProperty)));
								}
							}
						}
						else
						{
							FRefExplorerEditorModule_PRIVATE::FindRecursive(refStruct, structDefault, rootAsset, refPropInfos);
						}

						delete[] structDefault;
					}
					else if (UClass* refAssetClass = refAsset->GetClass())
					{
						for (TFieldIterator<FObjectPropertyBase> It(refAssetClass); It; ++It)
						{
							if (FObjectPropertyBase* objectProperty = *It)
							{
								UObject* value = objectProperty->GetObjectPropertyValue(objectProperty->ContainerPtrToValuePtr<void>(refAsset));

								if (value == rootAsset)
								{
									refPropInfos.Add(FRefExplorerEditorModule_PRIVATE::FRefPropInfo(objectProperty->GetDisplayNameText().ToString(), FRefExplorerEditorModule_PRIVATE::GetCategory(objectProperty)));
								}
							}
						}

						if (UScriptStruct* scriptStruct = Cast<UScriptStruct>(rootAsset))
						{
							for (TFieldIterator<FStructProperty> It(refAssetClass); It; ++It)
							{
								FStructProperty* structProperty = *It;

								if (structProperty->Struct == scriptStruct)
								{
									refPropInfos.Add(FRefExplorerEditorModule_PRIVATE::FRefPropInfo(structProperty->GetDisplayNameText().ToString(), FRefExplorerEditorModule_PRIVATE::GetCategory(structProperty)));
								}
							}
						}
						else
						{
							FRefExplorerEditorModule_PRIVATE::FindRecursive(refBlueprint->GeneratedClass, refAssetClass, rootAsset, refPropInfos);
						}
					}
				}
			}
		}
	}

	TSharedRef<SWidget> refPropsWidget = SNullWidget::NullWidget;

	TMultiMap<FString, FString> categorizedProps;

	if (refPropInfos.Num())
	{
		for (const FRefExplorerEditorModule_PRIVATE::FRefPropInfo& refPropInfo : refPropInfos)
		{
			categorizedProps.Add(refPropInfo.Category, refPropInfo.Name);
		}

		if (!categorizedProps.IsEmpty())
		{
			TArray<FString> categories;
			categorizedProps.GetKeys(categories);
			categories.Sort();

			TSharedRef<SVerticalBox> verticalBox = SNew(SVerticalBox);

			for (const FString& category : categories)
			{
				verticalBox->AddSlot()[SNew(STextBlock).Text(FText::FromString(category + ":")).Font(FRefExplorerEditorModule_PRIVATE::SmallBold)];

				TArray<FString> props;
				categorizedProps.MultiFind(category, props);
				props.Sort();

				for (const FString& prop : props)
				{
					verticalBox->AddSlot()[SNew(STextBlock).Text(FText::FromString(" - " + prop)).Font(FRefExplorerEditorModule_PRIVATE::Small)];
				}
			}

			refPropsWidget = verticalBox;
		}
	}

	ContentScale.Bind(this, &SGraphNode_RefExplorer::GetContentScale);
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
								.BorderBackgroundColor(this, &SGraphNode_RefExplorer::GetNodeTitleColor)
								.Padding(0)
								[
									SNew(SVerticalBox)
										.ToolTipText(this, &SGraphNode_RefExplorer::GetNodeTooltip)

										+ SVerticalBox::Slot()
										.AutoHeight()
										.HAlign(HAlign_Fill)
										.VAlign(VAlign_Top)
										.Padding(0)
										[
											SNew(SBorder)
												.Padding(FMargin(10.0f, 4.0f, 6.0f, 4.0f))
												.BorderImage(FRefExplorerEditorModule::GetStyleSet()->GetBrush("Graph.Node.ColorSpill"))
												.BorderBackgroundColor(this, &SGraphNode_RefExplorer::GetNodeTitleColor)
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
																		.OnVerifyTextChanged(this, &SGraphNode_RefExplorer::OnVerifyNameTextChanged)
																		.OnTextCommitted(this, &SGraphNode_RefExplorer::OnNameTextCommited)
																		.IsReadOnly(this, &SGraphNode_RefExplorer::IsNameReadOnly)
																		.IsSelected(this, &SGraphNode_RefExplorer::IsSelectedExclusively)
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
												.BackgroundColor(this, &SGraphNode_RefExplorer::GetErrorColor)
												.ToolTipText(this, &SGraphNode_RefExplorer::GetErrorMsgToolTip)
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

																+SVerticalBox::Slot().AutoHeight().Padding(categorizedProps.IsEmpty() ? FMargin() : FMargin(0, 4, 0, 0))
																[
																	refPropsWidget
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
// FRefExplorerGraphNodeFactory
//------------------------------------------------------

TSharedPtr<SGraphNode> FRefExplorerGraphNodeFactory::CreateNode(UEdGraphNode* InNode) const
{
	if (UEdGraphNode_RefExplorer* RefExplorerNode = Cast<UEdGraphNode_RefExplorer>(InNode))
	{
		return SNew(SGraphNode_RefExplorer, RefExplorerNode);
	}

	return nullptr;
}

//--------------------------------------------------------------------
// SRefExplorer
//--------------------------------------------------------------------

SRefExplorer::~SRefExplorer()
{
	if (!GExitPurge)
	{
		if (ensure(GraphObj))
		{
			GraphObj->RemoveFromRoot();
		}
	}
}

void SRefExplorer::Construct(const FArguments& InArgs)
{
	// Create an action list and register commands
	RegisterActions();

	// Create the graph
	GraphObj = NewObject<UEdGraph_RefExplorer>();
	GraphObj->Schema = URefExplorerSchema::StaticClass();
	GraphObj->AddToRoot();
	GraphObj->SetRefExplorer(StaticCastSharedRef<SRefExplorer>(AsShared()));

	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &SRefExplorer::OnNodeDoubleClicked);
	GraphEvents.OnCreateActionMenu = SGraphEditor::FOnCreateActionMenu::CreateSP(this, &SRefExplorer::OnCreateGraphActionMenu);

	// Create the graph editor
	GraphEditorPtr = SNew(SGraphEditor)
		.AdditionalCommands(RefExplorerActions)
		.GraphToEdit(GraphObj)
		.GraphEvents(GraphEvents)
		.ShowGraphStateOverlay(false);

	FEditorWidgetsModule& EditorWidgetsModule = FModuleManager::LoadModuleChecked<FEditorWidgetsModule>("EditorWidgets");
	TSharedRef<SWidget> AssetDiscoveryIndicator = EditorWidgetsModule.CreateAssetDiscoveryIndicator(EAssetDiscoveryIndicatorScaleMode::Scale_None, FMargin(16, 8), false);

	const FRefExplorerCommands& UICommands = FRefExplorerCommands::Get();

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
										.OnGetMenuContent(this, &SRefExplorer::GenerateFindPathAssetPickerMenu)
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
								.Text(this, &SRefExplorer::GetStatusText)
						]
				]
		];
}

FReply SRefExplorer::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	return RefExplorerActions->ProcessCommandBindings(InKeyEvent) ? FReply::Handled() : FReply::Unhandled();
}

void SRefExplorer::SetGraphRootIdentifier(const FAssetIdentifier& NewGraphRootIdentifier, const FReferenceViewerParams& ReferenceViewerParams)
{
	GraphObj->SetGraphRoot(NewGraphRootIdentifier);
	RebuildGraph();

	TriggerZoomToFit(0, 0);
	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SRefExplorer::TriggerZoomToFit));
}

EActiveTimerReturnType SRefExplorer::TriggerZoomToFit(double InCurrentTime, float InDeltaTime)
{
	if (GraphEditorPtr.IsValid())
	{
		GraphEditorPtr->ZoomToFit(false);
	}
	return EActiveTimerReturnType::Stop;
}

void SRefExplorer::RebuildGraph()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	if (AssetRegistryModule.Get().IsLoadingAssets())
	{
		// We are still discovering assets, listen for the completion delegate before building the graph
		if (!AssetRegistryModule.Get().OnFilesLoaded().IsBoundToObject(this))
		{
			AssetRegistryModule.Get().OnFilesLoaded().AddSP(this, &SRefExplorer::OnInitialAssetRegistrySearchComplete);
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
			AssetRefreshHandle = AssetRegistryModule.Get().OnAssetUpdated().AddSP(this, &SRefExplorer::OnAssetRegistryChanged);
			AssetRegistryModule.Get().OnAssetAdded().AddSP(this, &SRefExplorer::OnAssetRegistryChanged);
			AssetRegistryModule.Get().OnAssetRemoved().AddSP(this, &SRefExplorer::OnAssetRegistryChanged);
		}
	}
}

void SRefExplorer::OnNodeDoubleClicked(UEdGraphNode* Node)
{
	if (GraphObj)
	{
		if (UEdGraphNode_RefExplorer* RefExplorerNode = Cast<UEdGraphNode_RefExplorer>(Node))
		{
			GraphObj->SetGraphRoot(RefExplorerNode->GetIdentifier());
			GraphObj->RebuildGraph();

			TriggerZoomToFit(0, 0);
			RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SRefExplorer::TriggerZoomToFit));
		}
	}
}

FActionMenuContent SRefExplorer::OnCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed)
{
	// no context menu when not over a node
	return FActionMenuContent();
}

void SRefExplorer::RefreshClicked()
{
	RebuildGraph();

	TriggerZoomToFit(0, 0);
	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SRefExplorer::TriggerZoomToFit));
}

FText SRefExplorer::GetStatusText() const
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

void SRefExplorer::RegisterActions()
{
	RefExplorerActions = MakeShareable(new FUICommandList);
	FRefExplorerCommands::Register();

	RefExplorerActions->MapAction(
		FRefExplorerCommands::Get().ZoomToFit,
		FExecuteAction::CreateSP(this, &SRefExplorer::ZoomToFit),
		FCanExecuteAction::CreateSP(this, &SRefExplorer::CanZoomToFit));

	RefExplorerActions->MapAction(
		FGlobalEditorCommonCommands::Get().FindInContentBrowser,
		FExecuteAction::CreateSP(this, &SRefExplorer::ShowSelectionInContentBrowser),
		FCanExecuteAction::CreateSP(this, &SRefExplorer::HasAtLeastOnePackageNodeSelected));

	RefExplorerActions->MapAction(
		FRefExplorerCommands::Get().OpenSelectedInAssetEditor,
		FExecuteAction::CreateSP(this, &SRefExplorer::OpenSelectedInAssetEditor),
		FCanExecuteAction::CreateSP(this, &SRefExplorer::HasAtLeastOneRealNodeSelected));

	RefExplorerActions->MapAction(
		FRefExplorerCommands::Get().CopyReferencedObjects,
		FExecuteAction::CreateSP(this, &SRefExplorer::CopyReferencedObjects),
		FCanExecuteAction::CreateSP(this, &SRefExplorer::HasAtLeastOnePackageNodeSelected));

	RefExplorerActions->MapAction(
		FRefExplorerCommands::Get().CopyReferencingObjects,
		FExecuteAction::CreateSP(this, &SRefExplorer::CopyReferencingObjects),
		FCanExecuteAction::CreateSP(this, &SRefExplorer::HasAtLeastOnePackageNodeSelected));

	RefExplorerActions->MapAction(
		FRefExplorerCommands::Get().ShowReferencedObjects,
		FExecuteAction::CreateSP(this, &SRefExplorer::ShowReferencedObjects),
		FCanExecuteAction::CreateSP(this, &SRefExplorer::HasAtLeastOnePackageNodeSelected));

	RefExplorerActions->MapAction(
		FRefExplorerCommands::Get().ShowReferencingObjects,
		FExecuteAction::CreateSP(this, &SRefExplorer::ShowReferencingObjects),
		FCanExecuteAction::CreateSP(this, &SRefExplorer::HasAtLeastOnePackageNodeSelected));

	RefExplorerActions->MapAction(
		FRefExplorerCommands::Get().ShowReferenceTree,
		FExecuteAction::CreateSP(this, &SRefExplorer::ShowReferenceTree),
		FCanExecuteAction::CreateSP(this, &SRefExplorer::HasExactlyOnePackageNodeSelected));
}

void SRefExplorer::ShowSelectionInContentBrowser()
{
	TArray<FAssetData> AssetList;

	// Build up a list of selected assets from the graph selection set
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator It(SelectedNodes); It; ++It)
	{
		if (UEdGraphNode_RefExplorer* RefExplorerNode = Cast<UEdGraphNode_RefExplorer>(*It))
		{
			if (RefExplorerNode->GetAssetData().IsValid())
			{
				AssetList.Add(RefExplorerNode->GetAssetData());
			}
		}
	}

	if (AssetList.Num() > 0)
	{
		GEditor->SyncBrowserToObjects(AssetList);
	}
}

void SRefExplorer::OpenSelectedInAssetEditor()
{
	TArray<FAssetIdentifier> IdentifiersToEdit;
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator It(SelectedNodes); It; ++It)
	{
		if (UEdGraphNode_RefExplorer* RefExplorerNode = Cast<UEdGraphNode_RefExplorer>(*It))
		{
			IdentifiersToEdit.Add(RefExplorerNode->GetIdentifier());
		}
	}

	// This will handle packages as well as searchable names if other systems register
	FEditorDelegates::OnEditAssetIdentifiers.Broadcast(IdentifiersToEdit);
}

FString SRefExplorer::GetReferencedObjectsList() const
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

FString SRefExplorer::GetReferencingObjectsList() const
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

void SRefExplorer::CopyReferencedObjects()
{
	const FString ReferencedObjectsList = GetReferencedObjectsList();
	FPlatformApplicationMisc::ClipboardCopy(*ReferencedObjectsList);
}

void SRefExplorer::CopyReferencingObjects()
{
	const FString ReferencingObjectsList = GetReferencingObjectsList();
	FPlatformApplicationMisc::ClipboardCopy(*ReferencingObjectsList);
}

void SRefExplorer::ShowReferencedObjects()
{
	const FString ReferencedObjectsList = GetReferencedObjectsList();
	SGenericDialogWidget::OpenDialog(LOCTEXT("ReferencedObjectsDlgTitle", "Referenced Objects"), SNew(STextBlock).Text(FText::FromString(ReferencedObjectsList)));
}

void SRefExplorer::ShowReferencingObjects()
{
	const FString ReferencingObjectsList = GetReferencingObjectsList();
	SGenericDialogWidget::OpenDialog(LOCTEXT("ReferencingObjectsDlgTitle", "Referencing Objects"), SNew(STextBlock).Text(FText::FromString(ReferencingObjectsList)));
}

void SRefExplorer::ShowReferenceTree()
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

UObject* SRefExplorer::GetObjectFromSingleSelectedNode() const
{
	UObject* ReturnObject = nullptr;

	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	if (ensure(SelectedNodes.Num()) == 1)
	{
		UEdGraphNode_RefExplorer* RefExplorerNode = Cast<UEdGraphNode_RefExplorer>(SelectedNodes.Array()[0]);
		if (RefExplorerNode)
		{
			const FAssetData& AssetData = RefExplorerNode->GetAssetData();
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

void SRefExplorer::GetPackageNamesFromSelectedNodes(TSet<FName>& OutNames) const
{
	TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
	
	for (UObject* Node : SelectedNodes)
	{
		if (UEdGraphNode_RefExplorer* RefExplorerNode = Cast<UEdGraphNode_RefExplorer>(Node))
		{
			if (RefExplorerNode->GetIdentifier().IsPackage())
			{
				OutNames.Add(RefExplorerNode->GetIdentifier().PackageName);
			}
		}
	}
}

bool SRefExplorer::HasExactlyOneNodeSelected() const
{
	if (GraphEditorPtr.IsValid())
	{
		return GraphEditorPtr->GetSelectedNodes().Num() == 1;
	}

	return false;
}

bool SRefExplorer::HasExactlyOnePackageNodeSelected() const
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
			UEdGraphNode_RefExplorer* RefExplorerNode = Cast<UEdGraphNode_RefExplorer>(Node);
			if (RefExplorerNode)
			{
				if (RefExplorerNode->IsPackage())
				{
					return true;
				}
			}
			return false;
		}
	}

	return false;
}

bool SRefExplorer::HasAtLeastOnePackageNodeSelected() const
{
	if (GraphEditorPtr.IsValid())
	{
		TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
		for (UObject* Node : SelectedNodes)
		{
			UEdGraphNode_RefExplorer* RefExplorerNode = Cast<UEdGraphNode_RefExplorer>(Node);
			if (RefExplorerNode)
			{
				if (RefExplorerNode->IsPackage())
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool SRefExplorer::HasAtLeastOneRealNodeSelected() const
{
	if (GraphEditorPtr.IsValid())
	{
		TSet<UObject*> SelectedNodes = GraphEditorPtr->GetSelectedNodes();
		for (UObject* Node : SelectedNodes)
		{
			UEdGraphNode_RefExplorer* RefExplorerNode = Cast<UEdGraphNode_RefExplorer>(Node);
			if (RefExplorerNode)
			{
				return true;
			}
		}
	}

	return false;
}

void SRefExplorer::OnAssetRegistryChanged(const FAssetData& AssetData)
{
	// We don't do more specific checking because that data is not exposed, and it wouldn't handle newly added references anyway
	bDirtyResults = true;
}

void SRefExplorer::OnInitialAssetRegistrySearchComplete()
{
	if (GraphObj)
	{
		GraphObj->RebuildGraph();

		TriggerZoomToFit(0, 0);
		RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SRefExplorer::TriggerZoomToFit));
	}
}

void SRefExplorer::ZoomToFit()
{
	if (GraphEditorPtr.IsValid())
	{
		GraphEditorPtr->ZoomToFit(true);
	}
}

bool SRefExplorer::CanZoomToFit() const
{
	if (GraphEditorPtr.IsValid())
	{
		return true;
	}

	return false;
}

TSharedRef<SWidget> SRefExplorer::GetShowMenuContent()
{
	FMenuBuilder MenuBuilder(true, RefExplorerActions);
	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SRefExplorer::MakeToolBar()
{
	FToolBarBuilder ToolBarBuilder(RefExplorerActions, FMultiBoxCustomization::None, TSharedPtr<FExtender>(), true);
	
	//////ToolBarBuilder.SetStyle(&FReferenceViewerStyle::Get(), "AssetEditorToolbar");
	//////ToolBarBuilder.BeginSection("Test");

	//////ToolBarBuilder.AddToolBarButton(
	//////	FUIAction(FExecuteAction::CreateSP(this, &SRefExplorer::RefreshClicked)),
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>(),
	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.Refresh"));

	//////ToolBarBuilder.AddToolBarButton(
	//////	FUIAction(
	//////		FExecuteAction::CreateSP(this, &SRefExplorer::BackClicked),
	//////		FCanExecuteAction::CreateSP(this, &SRefExplorer::IsBackEnabled)
	//////	),
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>::CreateSP(this, &SRefExplorer::GetHistoryBackTooltip),
	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.ArrowLeft"));

	//////ToolBarBuilder.AddToolBarButton(
	//////	FUIAction(
	//////		FExecuteAction::CreateSP(this, &SRefExplorer::ForwardClicked),
	//////		FCanExecuteAction::CreateSP(this, &SRefExplorer::IsForwardEnabled)
	//////	),
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>::CreateSP(this, &SRefExplorer::GetHistoryForwardTooltip),
	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.ArrowRight"));

	//////ToolBarBuilder.AddToolBarButton(FRefExplorerCommands::Get().FindPath,
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>(),
	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "BlueprintEditor.FindInBlueprint"));

	//////ToolBarBuilder.AddSeparator();

	//////ToolBarBuilder.AddComboButton(
	//////	FUIAction(),
	//////	FOnGetContent::CreateSP(this, &SRefExplorer::GetShowMenuContent),
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>(),
	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.Visibility"),
	//////	/*bInSimpleComboBox*/ false);

	//////ToolBarBuilder.AddToolBarButton(FRefExplorerCommands::Get().ShowDuplicates,
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>::CreateLambda([this]() -> FText
	//////		{
	//////			if (Settings->GetFindPathEnabled())
	//////			{
	//////				return LOCTEXT("DuplicatesDisabledTooltip", "Duplicates are always shown when using the Find Path tool.");
	//////			}

	//////			return FRefExplorerCommands::Get().ShowDuplicates->GetDescription();
	//////		}),

	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.Duplicate"));

	//////ToolBarBuilder.AddSeparator();

	//////ToolBarBuilder.AddToolBarButton(FRefExplorerCommands::Get().Filters,
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>::CreateLambda([this]() -> FText
	//////		{
	//////			if (Settings->GetFindPathEnabled())
	//////			{
	//////				return LOCTEXT("FiltersDisabledTooltip", "Filtering is disabled when using the Find Path tool.");
	//////			}

	//////			return FRefExplorerCommands::Get().Filters->GetDescription();
	//////		}),

	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.Filters"));

	//////ToolBarBuilder.AddToolBarButton(FRefExplorerCommands::Get().AutoFilters,
	//////	NAME_None,
	//////	TAttribute<FText>(),
	//////	TAttribute<FText>::CreateLambda([this]() -> FText
	//////		{
	//////			if (Settings->GetFindPathEnabled())
	//////			{
	//////				return LOCTEXT("AutoFiltersDisabledTooltip", "AutoFiltering is disabled when using the Find Path tool.");
	//////			}

	//////			return FRefExplorerCommands::Get().AutoFilters->GetDescription();
	//////		}),

	//////	FSlateIcon(FReferenceViewerStyle::Get().GetStyleSetName(), "Icons.AutoFilters"));

	//////ToolBarBuilder.EndSection();

	return ToolBarBuilder.MakeWidget();
}

TSharedRef<SWidget> SRefExplorer::GenerateFindPathAssetPickerMenu()
{
	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.Filter.bRecursiveClasses = true;
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SRefExplorer::OnFindPathAssetSelected);
	AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateSP(this, &SRefExplorer::OnFindPathAssetEnterPressed);
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

void SRefExplorer::OnFindPathAssetSelected(const FAssetData& AssetData)
{
	FindPathAssetPicker->SetIsOpen(false);

	GraphObj->SetGraphRoot(FAssetIdentifier(AssetData.PackageName));
	GraphObj->RebuildGraph();

	TriggerZoomToFit(0, 0);
	RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SRefExplorer::TriggerZoomToFit));
}

void SRefExplorer::OnFindPathAssetEnterPressed(const TArray<FAssetData>& AssetData)
{
	FindPathAssetPicker->SetIsOpen(false);

	if (!AssetData.IsEmpty())
	{
		GraphObj->SetGraphRoot(FAssetIdentifier(AssetData[0].PackageName));
		GraphObj->RebuildGraph();

		TriggerZoomToFit(0, 0);
		RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateSP(this, &SRefExplorer::TriggerZoomToFit));
	}
}

namespace FRefExplorerEditorModule_PRIVATE
{
	const FName RefExplorerTabId("Ref Explorer");

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
	// FContentBrowserSelectionMenuExtender_RefExplorer
	//--------------------------------------------------------------------

	class FContentBrowserSelectionMenuExtender_RefExplorer : public FContentBrowserSelectionMenuExtender<UObject>
	{
	public:
		FContentBrowserSelectionMenuExtender_RefExplorer(const FText& label, const FText& toolTip, const FName styleSetName, const FName iconName)
			: FContentBrowserSelectionMenuExtender(label, toolTip, styleSetName, iconName)
		{}

	protected:
		virtual void Execute(FAssetIdentifier assetIdentifier) const override
		{
			if (TSharedPtr<SDockTab> NewTab = FGlobalTabmanager::Get()->TryInvokeTab(RefExplorerTabId))
			{
				TSharedRef<SRefExplorer> RefExplorer = StaticCastSharedRef<SRefExplorer>(NewTab->GetContent());
				RefExplorer->SetGraphRootIdentifier(assetIdentifier, FReferenceViewerParams());
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

	ContentBrowserSelectionMenuExtenders.Add(MakeShareable(new FRefExplorerEditorModule_PRIVATE::FContentBrowserSelectionMenuExtender_RefExplorer(
		LOCTEXT("FContentBrowserSelectionMenuExtender_RefExplorer_Label", "Ref Explorer"),
		LOCTEXT("FContentBrowserSelectionMenuExtender_RefExplorer_ToolTip", "Explore and edit properties that are referencing selected asset"),
		GetStyleSetName(),
		GetContextMenuRefExplorerIconName()
	)));

	for (const TSharedPtr<IContentBrowserSelectionMenuExtender>& extender : ContentBrowserSelectionMenuExtenders)
	{
		if (extender.IsValid())
		{
			extender->Extend();
		}
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FRefExplorerEditorModule_PRIVATE::RefExplorerTabId, FOnSpawnTab::CreateRaw(this, &FRefExplorerEditorModule::OnSpawnTab));

	RefExplorerGraphNodeFactory = MakeShareable(new FRefExplorerGraphNodeFactory());
	FEdGraphUtilities::RegisterVisualNodeFactory(RefExplorerGraphNodeFactory);
}

void FRefExplorerEditorModule::ShutdownModule()
{
	FEdGraphUtilities::UnregisterVisualNodeFactory(RefExplorerGraphNodeFactory);
	RefExplorerGraphNodeFactory.Reset();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FRefExplorerEditorModule_PRIVATE::RefExplorerTabId);

	ContentBrowserSelectionMenuExtenders.Empty();

	ShutdownStyle();
}

FName FRefExplorerEditorModule::GetStyleSetName() { return "FRefExplorerEditorModule_Style"; }

FName FRefExplorerEditorModule::GetContextMenuRefExplorerIconName() { return "FRefExplorerEditorModule_Style_ContextMenu_RefExplorer"; }

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

	StyleSet->Set(GetContextMenuRefExplorerIconName(), new FSlateImageBrush(StyleSet->RootToContentDir(TEXT("Icon_ContextMenu_RefExplorer_128"), TEXT(".png")), Icon20x20));

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
	DockTab->SetContent(SNew(SRefExplorer));
	return DockTab;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FRefExplorerEditorModule, RefExplorerEditor)