#include "Utils/MetaLandRoadCompVisUtils.h"

#include "Editor.h"
#include "Selection.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementSelectionSet.h"

namespace CompVisUtils
{
	static FTypedElementSelectionOptions MakeActorSelectionOptions(bool bAllowLegacyNotifications)
	{
		return FTypedElementSelectionOptions()
			.SetAllowHidden(true)
			.SetWarnIfLocked(true)
			.SetAllowLegacyNotifications(bAllowLegacyNotifications);
	}

	static void RebuildComponentVisualizersForActorSelection()
	{
		static bool bInRefresh = false;
		if (bInRefresh || !GEditor)
		{
			return;
		}
		TGuardValue<bool> RefreshGuard(bInRefresh, true);

		UTypedElementSelectionSet* ActorSelectionSet = GEditor->GetSelectedActors()->GetElementSelectionSet();
		if (!ActorSelectionSet)
		{
			return;
		}

		TArray<FTypedElementHandle> SelectedHandles = ActorSelectionSet->GetSelectedElementHandles();
		if (SelectedHandles.Num() > 0)
		{
			// Re-mark pending typed-element selection without legacy notify (avoids SelectionChanged recursion).
			ActorSelectionSet->SetSelection(SelectedHandles, MakeActorSelectionOptions(false));
		}

		ActorSelectionSet->NotifyPendingChanges();
	}

	bool IsComponentInActorElementSelection(const UActorComponent* TargetComponent)
	{
		if (!TargetComponent || !GEditor)
		{
			return false;
		}

		UTypedElementSelectionSet* ActorSelectionSet = GEditor->GetSelectedActors()->GetElementSelectionSet();
		if (!ActorSelectionSet)
		{
			return false;
		}

		const FTypedElementHandle ComponentHandle =
			UEngineElementsLibrary::AcquireEditorComponentElementHandle(const_cast<UActorComponent*>(TargetComponent));
		if (!ComponentHandle)
		{
			return false;
		}

		return ActorSelectionSet->IsElementSelected(ComponentHandle, FTypedElementIsSelectedOptions());
	}

	void RefreshComponentVisualizersForSelection()
	{
		RebuildComponentVisualizersForActorSelection();
	}

	void SelectRoadSplineExclusive(AActor* Owner, UActorComponent* TargetComponent, bool bNotify)
	{
		if (!GEditor || !Owner || !TargetComponent)
		{
			return;
		}

		static bool bInSelectionSync = false;
		if (bInSelectionSync)
		{
			return;
		}
		TGuardValue<bool> ReentranceGuard(bInSelectionSync, true);

		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AActor* Selected = Cast<AActor>(*It))
			{
				if (Selected != Owner)
				{
					GEditor->SelectActor(Selected, false, false);
				}
			}
		}

		GEditor->GetSelectedComponents()->DeselectAll();

		const FTypedElementSelectionOptions ActorOptions = MakeActorSelectionOptions(false);
		const FTypedElementHandle ActorHandle = UEngineElementsLibrary::AcquireEditorActorElementHandle(Owner);
		const FTypedElementHandle ComponentHandle =
			UEngineElementsLibrary::AcquireEditorComponentElementHandle(TargetComponent);

		if (UTypedElementSelectionSet* ActorSelectionSet = GEditor->GetSelectedActors()->GetElementSelectionSet())
		{
			if (ActorHandle && ComponentHandle)
			{
				TArray<FTypedElementHandle> NewSelection;
				NewSelection.Reserve(2);
				NewSelection.Add(ActorHandle);
				NewSelection.Add(ComponentHandle);

				ActorSelectionSet->SetSelection(NewSelection, ActorOptions);
				ActorSelectionSet->NotifyPendingChanges();
			}
		}

		if (!GEditor->GetSelectedActors()->IsSelected(Owner))
		{
			GEditor->SelectActor(Owner, true, false);
		}

		GEditor->SelectComponent(TargetComponent, true, false, true);

		if (bNotify)
		{
			GEditor->NoteSelectionChange();
		}
	}
}
