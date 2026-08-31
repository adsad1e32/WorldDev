#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWindow.h"
#include "MoKuEditWidgets.h"



struct FAssetData;
class UMoKuEditorGizmoBase;
class FLibraryAssetWindow
{

public:

	static FLibraryAssetWindow& Get()
	{
		static FLibraryAssetWindow LibWindow;
		return LibWindow;
	};

	void CreateLibAssetWindow(TWeakObjectPtr<UMoKuEditorGizmoBase> InOwnerGizmo);
	TSharedPtr<SWindow> GetLibAssetWindow() const;
	void RefreshContent(TWeakObjectPtr<UMoKuEditorGizmoBase> EditorGizmo);

private:

	FLibraryAssetWindow() {};
	FLibraryAssetWindow(const FLibraryAssetWindow&) = delete;
	FLibraryAssetWindow& operator = (const FLibraryAssetWindow&) = delete;
	TSharedPtr<SWindow> AssetWindow;
	TSharedPtr<SMoKuRecommendAssetView> AssetView;
	FOnWindowClosed OnWindowClosed;
	void WindowClosed(const TSharedRef<SWindow>& Window);



};