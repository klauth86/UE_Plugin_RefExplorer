// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraphSchema.h"
#include "Misc/AssetRegistryInterface.h"
#include "EdGraphUtilities.h"
#include "RefExplorerEditorModule_private.generated.h"

namespace FRefExplorerEditorModule_PRIVATE
{
	enum class EDependencyPinCategory;
}

class UToolMenu;
class UGraphNodeContextMenuContext;
class FConnectionDrawingPolicy;
class FSlateWindowElementList;
class UEdGraph;
class FAssetThumbnailPool;

//------------------------------------------------------
// FReferenceExplorerGraphNodeFactory
//------------------------------------------------------

struct FReferenceExplorerGraphNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<SGraphNode> CreateNode(UEdGraphNode* InNode) const override;
};

//--------------------------------------------------------------------
// SReferenceExplorer
//--------------------------------------------------------------------

class SReferenceExplorer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SReferenceExplorer) {}
	SLATE_END_ARGS()

	~SReferenceExplorer();

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	/**
	 * Sets a new root package name
	 *
	 * @param NewGraphRootIdentifiers	The root elements of the new graph to be generated
	 * @param ReferenceViewerParams		Different visualization settings, such as whether it should display the referencers or the dependencies of the NewGraphRootIdentifiers
	 */
	void SetGraphRootIdentifier(const FAssetIdentifier& NewGraphRootIdentifier, const FReferenceViewerParams& ReferenceViewerParams = FReferenceViewerParams());

	/** Gets graph editor */
	TSharedPtr<SGraphEditor> GetGraphEditor() const { return GraphEditorPtr; }

	/**SWidget interface **/
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void RebuildGraph();

	void OnNodeDoubleClicked(UEdGraphNode* Node);

	FActionMenuContent OnCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed);

	/** Refresh the current view */
	void RefreshClicked();

	/** Gets the text to be displayed for warning/status updates */
	FText GetStatusText() const;

	TSharedRef<SWidget> GetShowMenuContent();

	void RegisterActions();
	void ShowSelectionInContentBrowser();
	void OpenSelectedInAssetEditor();
	void CopyReferencedObjects();
	void CopyReferencingObjects();
	void ShowReferencedObjects();
	void ShowReferencingObjects();
	void ShowReferenceTree();
	void ZoomToFit();
	bool CanZoomToFit() const;

	/** Find Path */
	TSharedRef<SWidget> GenerateFindPathAssetPickerMenu();
	void OnFindPathAssetSelected(const FAssetData& AssetData);
	void OnFindPathAssetEnterPressed(const TArray<FAssetData>& AssetData);
	TSharedPtr<SComboButton> FindPathAssetPicker;
	FAssetIdentifier FindPathAssetId;

	FString GetReferencedObjectsList() const;
	FString GetReferencingObjectsList() const;

	UObject* GetObjectFromSingleSelectedNode() const;
	void GetPackageNamesFromSelectedNodes(TSet<FName>& OutNames) const;
	bool HasExactlyOneNodeSelected() const;
	bool HasExactlyOnePackageNodeSelected() const;
	bool HasAtLeastOnePackageNodeSelected() const;
	bool HasAtLeastOneRealNodeSelected() const;

	void OnAssetRegistryChanged(const FAssetData& AssetData);
	void OnInitialAssetRegistrySearchComplete();
	EActiveTimerReturnType TriggerZoomToFit(double InCurrentTime, float InDeltaTime);

private:
	TSharedRef<SWidget> MakeToolBar();

	TSharedPtr<SGraphEditor> GraphEditorPtr;

	TSharedPtr<FUICommandList> ReferenceViewerActions;

	UEdGraph_ReferenceExplorer* GraphObj;

	/** True if our view is out of date due to asset registry changes */
	bool bDirtyResults;

	/** Handle to know if dirty */
	FDelegateHandle AssetRefreshHandle;
};

//--------------------------------------------------------------------
// UReferenceExplorerSchema
//--------------------------------------------------------------------

UCLASS()
class UReferenceExplorerSchema : public UEdGraphSchema
{
	GENERATED_UCLASS_BODY()

public:
	// UEdGraphSchema interface
	virtual void GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	virtual FName GetParentContextMenuName() const override { return NAME_None; }
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const override {}
	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override {}
	virtual FPinConnectionResponse MovePinLinks(UEdGraphPin& MoveFromPin, UEdGraphPin& MoveToPin, bool bIsIntermediateMove = false, bool bNotifyLinkedNodes = false) const override { return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, FString()); }
	virtual FPinConnectionResponse CopyPinLinks(UEdGraphPin& CopyFromPin, UEdGraphPin& CopyToPin, bool bIsIntermediateCopy = false) const override { return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, FString()); }
	virtual FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const override;
	virtual void DroppedAssetsOnGraph(const TArray<FAssetData>& Assets, const FVector2D& GraphPosition, UEdGraph* Graph) const override {}
	virtual void GetAssetsGraphHoverMessage(const TArray<FAssetData>& Assets, const UEdGraph* HoverGraph, FString& OutTooltipText, bool& OutOkIcon) const override { OutOkIcon = true; }
	// End of UEdGraphSchema interface
};

//--------------------------------------------------------------------
// FReferenceExplorerNodeInfo
//--------------------------------------------------------------------

struct FReferenceExplorerNodeInfo
{
	FAssetIdentifier AssetId;

	FAssetData AssetData;

	TArray<TPair<FAssetIdentifier, FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory>> Children;

	TSet<FAssetIdentifier> Parents;

	FReferenceExplorerNodeInfo(const FAssetIdentifier& InAssetId) :AssetId(InAssetId) {};
};

//--------------------------------------------------------------------
// UEdGraphNode_ReferenceExplorer
//--------------------------------------------------------------------

UCLASS()
class UEdGraphNode_ReferenceExplorer : public UEdGraphNode
{
	GENERATED_UCLASS_BODY()

	FORCEINLINE const FAssetIdentifier& GetIdentifier() const { return Identifier; }

	UEdGraph_ReferenceExplorer* GetReferenceViewerGraph() const;

	// UEdGraphNode implementation
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override { return NodeTitle; }
	FLinearColor GetNodeTitleColor() const override;
	virtual FText GetTooltipText() const override;
	virtual void AllocateDefaultPins() override;
	FORCEINLINE virtual FSlateIcon GetIconAndTint(FLinearColor& OutColor) const override { OutColor = AssetTypeColor; return AssetBrush; }
	virtual bool ShowPaletteIconOnNode() const override { return true; }
	// End UEdGraphNode implementation

	FORCEINLINE bool UsesThumbnail() const { return bUsesThumbnail; }
	FORCEINLINE bool IsPackage() const { return bIsPackage; }

	FORCEINLINE FAssetData GetAssetData() const { return CachedAssetData; }

	FORCEINLINE UEdGraphPin* GetDependencyPin() { return DependencyPin; }
	FORCEINLINE UEdGraphPin* GetReferencerPin() { return ReferencerPin; }

private:
	void SetupReferenceNode(const FIntPoint& NodeLoc, const FAssetIdentifier& NewIdentifier, const FAssetData& InAssetData);
	void AddReferencer(UEdGraphNode_ReferenceExplorer* ReferencerNode);

protected:
	FAssetIdentifier Identifier;
	FText NodeTitle;

	bool bUsesThumbnail;
	bool bIsPackage;
	bool bIsPrimaryAsset;

	FAssetData CachedAssetData;
	FLinearColor AssetTypeColor;
	FSlateIcon AssetBrush;

	UEdGraphPin* DependencyPin;
	UEdGraphPin* ReferencerPin;

	friend UEdGraph_ReferenceExplorer;
};

//--------------------------------------------------------------------
// UEdGraph_ReferenceExplorer
//--------------------------------------------------------------------

UCLASS()
class UEdGraph_ReferenceExplorer : public UEdGraph
{
	GENERATED_UCLASS_BODY()

public:
	// UObject implementation
	virtual void BeginDestroy() override;
	// End UObject implementation

	/** Set reference viewer to focus on these assets */
	void SetGraphRoot(const FAssetIdentifier& GraphRootIdentifier, const FIntPoint& GraphRootOrigin = FIntPoint(ForceInitToZero));

	/** Accessor for the thumbnail pool in this graph */
	const TSharedPtr<FAssetThumbnailPool>& GetAssetThumbnailPool() const;

	/** Force the graph to rebuild */
	UEdGraphNode_ReferenceExplorer* RebuildGraph();

	const FReferenceExplorerNodeInfo& GetGraphRootNodeInfo() const { return ReferencerNodeInfos[CurrentGraphRootIdentifier]; }

private:
	FORCEINLINE void SetReferenceViewer(TSharedPtr<SReferenceExplorer> InViewer) { ReferenceViewer = InViewer; }

	/* Searches for the AssetData for the list of packages derived from the AssetReferences  */
	void GatherAssetData(TMap<FAssetIdentifier, FReferenceExplorerNodeInfo>& InNodeInfos);

	/* Uses the NodeInfos map to generate and layout the graph nodes */
	UEdGraphNode_ReferenceExplorer* RecursivelyCreateNodes(
		const FAssetIdentifier& InAssetId,
		const FIntPoint& InNodeLoc,
		const FAssetIdentifier& InParentId,
		UEdGraphNode_ReferenceExplorer* InParentNode,
		TMap<FAssetIdentifier, FReferenceExplorerNodeInfo>& InNodeInfos,
		bool bIsRoot = false
	);

	/** Removes all nodes from the graph */
	void RemoveAllNodes();

	void GetSortedLinks(const FAssetIdentifier& GraphRootIdentifier, TMap<FAssetIdentifier, FRefExplorerEditorModule_PRIVATE::EDependencyPinCategory>& OutLinks) const;

private:
	/** Pool for maintaining and rendering thumbnails */
	TSharedPtr<FAssetThumbnailPool> AssetThumbnailPool;

	/** Editor for this pool */
	TWeakPtr<SReferenceExplorer> ReferenceViewer;

	FAssetIdentifier CurrentGraphRootIdentifier;
	
	FIntPoint CurrentGraphRootOrigin;

	TMap<FAssetIdentifier, FReferenceExplorerNodeInfo> ReferencerNodeInfos;

	friend SReferenceExplorer;
};