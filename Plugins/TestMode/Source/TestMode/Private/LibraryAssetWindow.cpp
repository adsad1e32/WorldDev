#include "LibraryAssetWindow.h"
#include "FrameWork/Application/SlateApplication.h"
#include "Widgets/SBoxPanel.h"
#include "AssetRegistry/AssetData.h"
#include "MoKuEditorGizmoBase.h"
#include "Interfaces/IMainFrameModule.h"

#define LOCTEXT_NAMESPACE "LibraryAssetWindow"


void FLibraryAssetWindow::CreateLibAssetWindow(TWeakObjectPtr<UMoKuEditorGizmoBase> InOwnerGizmo)
{
	if (AssetWindow.IsValid())AssetWindow->RequestDestroyWindow();
	if (!AssetWindow.IsValid())
	{
		AssetWindow = SNew(SWindow)
			.Title(LOCTEXT("LibraryAssetWindow", "Library Asset Windows"))
			.ClientSize(FVector2D(300,650))
			.SupportsMinimize(false)
			.SupportsMaximize(false)
			[
				SNew(SBox)
				.VAlign(VAlign_Fill)
				.Padding(1)
				[
					SAssignNew(AssetView,SMoKuRecommendAssetView)
					.EditorGizmo(InOwnerGizmo)
				]
			];
		AssetWindow->MoveWindowTo(FVector2D(200, 200));
		AssetWindow->SetOnWindowClosed(FOnWindowClosed::CreateRaw(this, &FLibraryAssetWindow::WindowClosed));
		IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));

		if (MainFrameModule.GetParentWindow().IsValid())
		{
			FSlateApplication::Get().AddWindowAsNativeChild(AssetWindow.ToSharedRef(), MainFrameModule.GetParentWindow().ToSharedRef());
		}
		else
		{
			FSlateApplication::Get().AddWindow(AssetWindow.ToSharedRef());
		}

	}
}

TSharedPtr<SWindow> FLibraryAssetWindow::GetLibAssetWindow() const
{
	return AssetWindow;
}

void FLibraryAssetWindow::WindowClosed(const TSharedRef<SWindow>& Window)
{
	AssetWindow.Reset();
}


void FLibraryAssetWindow::RefreshContent(TWeakObjectPtr<UMoKuEditorGizmoBase> EditorGizmo)
{
	if (AssetView.IsValid())
	{
		AssetView = SNew(SMoKuRecommendAssetView)
					.EditorGizmo(EditorGizmo);
	}
}


#undef LOCTEXT_NAMESPACE