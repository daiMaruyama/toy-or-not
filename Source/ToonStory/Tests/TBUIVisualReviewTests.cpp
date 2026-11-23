// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/TBCharacter.h"
#include "Character/TBMovement.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/SceneComponent.h"
#include "Core/TBController.h"
#include "Core/TBChat.h"
#include "Core/TBGameMode.h"
#include "Core/TBGameState.h"
#include "Core/TBGameUserSettings.h"
#include "Core/TBPlayerState.h"
#include "Data/TBRuleData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "GamePlay/TBBox.h"
#include "GamePlay/TBBoxEscapeCourse.h"
#include "GamePlay/TBDummyToy.h"
#include "GamePlay/Events/TBMomArrivalZone.h"
#include "GamePlay/Items/TBEscapeExit.h"
#include "GamePlay/Items/TBEscapeVehicle.h"
#include "Input/TBKeyLabels.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Tests/TBTestMatch.h"
#include "UI/TBHUD.h"
#include "UI/TBOptionsMenu.h"
#include "UI/TBOptionsPages.h"
#include "UI/TBPaperStyle.h"
#include "UI/TBTitleMenu.h"
#include "UI/TBUIText.h"
#include "UI/Loading/TBLoadingScreenSettings.h"
#include "UI/Loading/TBLoadingScreenSubsystem.h"
#include "GameFramework/HUDHitBox.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/IToolTip.h"

namespace
{
	UWorld* ReviewWorld()
	{
		for (const auto& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game && Context.World() &&
			    Context.World()->GetNetMode() == NM_Standalone)
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	// This is an opt-in screenshot fixture, not a screenshot-diff or network test.
	// Use a fresh standalone process at -ResX=1280 -ResY=800, then at 1920x1080.
	class FUIReview : public IAutomationLatentCommand
	{
	public:
		FUIReview(FAutomationTestBase* InTest, UWorld* InWorld, bool bInTitle)
		    : Test(InTest), World(InWorld), bTitle(bInTitle), Deadline(FPlatformTime::Seconds() + CaptureTimeout)
		{
			OldLanguage = TBText::GetLanguage();
			bRosterOnly = !bTitle && FParse::Param(FCommandLine::Get(), TEXT("UIReviewRosterOnly"));
			bRandomOnly = !bTitle && FParse::Param(FCommandLine::Get(), TEXT("UIReviewRandomStageOnly"));
			if (bRandomOnly) Step = 1;
			bCompactHintsOnly = !bTitle && FParse::Param(FCommandLine::Get(), TEXT("UIReviewCompactHintsOnly"));
			if (auto* Settings = UTBGameUserSettings::Get()) OldSavedLanguage = Settings->GetLanguage();
			FString RequestedLanguage;
			if (FParse::Value(FCommandLine::Get(), TEXT("UIReviewLanguage="), RequestedLanguage))
			{
				Test->TestTrue(TEXT("Requested UI language activates"), TBText::SetLanguage(RequestedLanguage));
				if (auto* Settings = UTBGameUserSettings::Get()) Settings->SetLanguage(RequestedLanguage);
			}
			PC = Cast<ATBController>(InWorld->GetFirstPlayerController());
			State = InWorld->GetGameState<ATBGameState>();
			Mode = InWorld->GetAuthGameMode<ATBGameMode>();
			if (!bTitle && PC.IsValid() && State.IsValid())
			{
				Player = PC->GetPlayerState<ATBPlayerState>();
				HUD = Cast<ATBHUD>(PC->GetHUD());
				Body = PC->GetPawn<ATBCharacter>();
				OldViewTarget = PC->GetViewTarget();
				if (Body.IsValid())
				{
					if (const auto* Movement = Cast<UTBMovement>(Body->GetCharacterMovement()))
						bOldGazeRequested = Movement->bWantsToGaze;
					ReviewCamera = Body->Camera;
					if (ReviewCamera.IsValid())
					{
						OldCameraAspect = ReviewCamera->AspectRatio;
						bOldCameraConstrained = ReviewCamera->bConstrainAspectRatio;
					}
				}
				OldTeam = Player->Team;
				OldToyState = Player->ToyState;
				OldGazeState = Player->GazeState;
				OldGazedSeconds = Player->GazedSeconds;
				bOldFrozen = Player->bFrozen;
				bOldReady = Player->bReady;
				OldSettings = State->Settings;
				OldWinner = State->Winner;
				OldStyle = State->ResultStyle;
				OldReason = State->Reason;
				OldCollected = State->Collected;
				bOldEscape = State->bEscapePhase;
				bOldVehicleResult = State->bVehicleEscapeResult;
				OldResultStart = State->ResultStartTime;
				bOldSettings = HUD->IsLobbySettingsOpen();
				OldRound = State->HidePreparation.Round;
				bOldModeTick = Mode.IsValid() && Mode->IsActorTickEnabled();
				if (Mode.IsValid()) Mode->SetActorTickEnabled(false);
			}
		}

		~FUIReview() override
		{
			RestoreTitleButton();
			RestoreChat();
			ClearEscapeReview();
			RestoreNavigation();
			RestoreStorage();
			if (Options.IsValid()) Options->Dismiss();
			if (PC.IsValid())
			{
				PC->CloseChat();
				if (PC->IsLeaveMenuOpen()) PC->ToggleLeaveMenu();
			}
			if (State.IsValid() && !bTitle)
			{
				State->Winner = OldWinner;
				State->ResultStyle = OldStyle;
				State->Reason = OldReason;
				State->Collected = OldCollected;
				State->bEscapePhase = bOldEscape;
				State->bVehicleEscapeResult = bOldVehicleResult;
				State->ResultStartTime = OldResultStart;
				State->Settings = OldSettings;
			}
			Playing.Reset();
			if (Player.IsValid())
			{
				Player->Team = OldTeam;
				Player->ToyState = OldToyState;
				Player->bReady = bOldReady;
				Player->SetGaze(OldGazeState, OldGazedSeconds);
				Player->bFrozen = bOldFrozen;
			}
			if (Body.IsValid())
			{
				Body->SetGazeRequested(bOldGazeRequested);
				Body->ApplyState();
			}
			RestoreOverview();
			if (ReviewCamera.IsValid())
			{
				ReviewCamera->AspectRatio = OldCameraAspect;
				ReviewCamera->bConstrainAspectRatio = bOldCameraConstrained;
			}
			if (bChangedGazeView && PC.IsValid() && OldViewTarget.IsValid()) PC->SetViewTarget(OldViewTarget.Get());
			for (const auto& Dummy : Roster)
			{
				if (!Dummy.IsValid()) continue;
				if (State.IsValid()) State->RemovePlayerState(Dummy.Get());
				Dummy->Destroy();
			}
			if (PC.IsValid()) PC->ClientPreparationRole(OldRound, OldTeam);
			if (HUD.IsValid())
			{
				HUD->DismissConfirmation();
				HUD->NotifyHitBoxClick(TEXT("ClosePlayers"));
				HUD->NotifyHitBoxClick(bOldSettings ? TEXT("OpenSettings") : TEXT("CloseSettings"));
			}
			if (Mode.IsValid()) Mode->SetActorTickEnabled(bOldModeTick);
			TBText::SetLanguage(OldLanguage);
			if (auto* Settings = UTBGameUserSettings::Get()) Settings->SetLanguage(OldSavedLanguage);
		}

		bool Update() override
		{
			if (!World.IsValid() || FPlatformTime::Seconds() > Deadline)
			{
				Test->AddError(FString::Printf(TEXT("UI review lost its world or timed out on capture %d (%s)."), Step, *Name));
				return true;
			}
			if (State.IsValid() && (State->bTravelPending || World->IsInSeamlessTravel()))
			{
				Test->AddError(TEXT("UI fixture unexpectedly initiated travel; refusing misleading captures."));
				return true;
			}
			if (Playing && ((Step >= 9 && Step <= 15) || (Step >= 19 && Step <= 22) || (Step >= 24 && Step <= 44) ||
			    (Step >= 48 && Step <= 50)))
			{
				Playing->Hold(State->GetServerWorldTimeSeconds() + (Step == 19 ? 10 : 187));
				if (Step == 12)
				{
					State->HidePreparation.bHiding = true;
					State->HidePreparation.StartsAt = State->GetServerWorldTimeSeconds() - 3;
					State->HidePreparation.EndsAt = State->GetServerWorldTimeSeconds() + 12;
				}
			}
			if (!bEntered)
			{
				if (bTitle ? !EnterTitle() : !EnterGame()) return true;
				bEntered = true;
				const auto* Loading = GetDefault<UTBLoadingScreenSettings>();
				const double Delay = !bTitle && Step == 9 ? .25 :
				    !bTitle && (Step == 10 || Step == 19) ? Loading->MatchSwitchSeconds + Loading->FadeOutSeconds + 1.0 :
				    !bTitle && Step == 18 ? Loading->MatchSwitchSeconds + Loading->SettleWaitSeconds + Loading->FadeOutSeconds + 1.0 : 1.25;
				ReadyAt = FPlatformTime::Seconds() + Delay;
				Frames = 0;
				return false;
			}
			// Automation runs with an unfocused window; Slate can close an edit box
			// after the opening tick. Keep this fixture open through the capture frame.
			if (!bTitle && Step == 4 && PC.IsValid()) PC->OpenChat();
			if (TitleSlateButton.IsValid() && !HoldTitleButton()) return true;
			if (bOverviewActive && !(ReviewCar.IsValid() && Body.IsValid() && Body->EscapeVehicle == ReviewCar.Get())) HoldOverview();
			// Keep the real replicated start time at the selected progress while rendering;
			// screenshot latency must not turn the half-full sample into a finished attempt.
			if (!bTitle && Step >= 40 && Step <= 42 && Body.IsValid())
				Body->StoreStarted = State->GetServerWorldTimeSeconds() - StorageFraction() * TB::Rules().Box.StoreSeconds;
			if (++Frames < (bCompactHintsOnly ? 3 : 12) || FPlatformTime::Seconds() < ReadyAt) return false;
			if (!bRequested)
			{
				if (bTitle && Step == 0)
				{
#if WITH_EDITOR
					for (const auto& Texture : ReviewTextures)
						if (Texture->IsCompiling()) return false;
#endif
					for (const auto& Texture : ReviewTextures)
						Test->TestTrue(*FString::Printf(TEXT("UI artwork has generated mips: %s (%d)"),
						    *Texture->GetName(), Texture->GetNumMips()), Texture->GetNumMips() > 1);
				}
				if (TitleSlateButton.IsValid() && !CheckTitleButton()) return true;
				if (bTitle && (Step == 1 || Step == 10))
				{
					const auto* Goal = Cast<UTextBlock>(Title->WidgetTree->FindWidget(TEXT("HelpGoal")));
					const TCHAR* Expected = Step == 1 ? TEXT("Root.Help.ToyGoal") : TEXT("Root.Help.HumanGoal");
					const auto* Sheet = Cast<UImage>(Title->WidgetTree->FindWidget(TEXT("HelpComic")));
					const auto* Art = Sheet ? Cast<UTexture2D>(Sheet->GetBrush().GetResourceObject()) : nullptr;
					if (!Test->TestNotNull(TEXT("Role page has the player's comic"), Art) ||
					    !Test->TestEqual(TEXT("Tab selects the correct photo comic"), Art->GetName(),
					        FString(Step == 1 ? TEXT("T_UI_GuideToy") : TEXT("T_UI_GuideHuman"))) ||
					    !Test->TestTrue(TEXT("The complete comic fits with its original proportions"),
					        FMath::IsNearlyEqual(Sheet->GetCachedGeometry().GetLocalSize().X /
					            FMath::Max(1., double(Sheet->GetCachedGeometry().GetLocalSize().Y)),
					            double(Art->GetSizeX()) / Art->GetSizeY(), .012))) return true;
					if (!Test->TestNotNull(TEXT("Role page has an objective"), Goal) ||
					    !Test->TestEqual(TEXT("Selected role shows its localized objective"),
					        Goal->GetText().ToString(), TBText::String(Expected))) return true;
					for (int32 Panel = 1; Panel <= 3; ++Panel)
					{
						const auto* Speech = Cast<UTextBlock>(Title->WidgetTree->FindWidget(
						    FName(*FString::Printf(TEXT("HelpSpeech%d"), Panel))));
						const FString Key = FString::Printf(TEXT("Root.Help.%s.Panel%d.Speech"),
						    Step == 1 ? TEXT("Toy") : TEXT("Human"), Panel);
						if (!Test->TestNotNull(TEXT("Panel has editable dialogue"), Speech) ||
						    !Test->TestEqual(TEXT("Panel uses this role's dialogue"), Speech->GetText().ToString(),
						        TBText::String(*Key))) return true;
						// The safe area sits inside the oval, including room for its tilt.
						const auto& Line = Speech->GetCachedGeometry();
						const auto& Page = Sheet->GetCachedGeometry();
						const FVector2D Size = Line.GetLocalSize();
						const FVector2D Center(Panel == 2 ? 523.f : 1057.f, 202.5f + (Panel - 1) * 196.f);
						for (FVector2D Corner : {FVector2D::ZeroVector, FVector2D(Size.X,0), Size, FVector2D(0,Size.Y)})
						{
							const FVector2D Offset = Page.AbsoluteToLocal(Line.LocalToAbsolute(Corner)) - Center;
							if (!Test->TestTrue(*FString::Printf(TEXT("%s panel %d dialogue corner (%.1f, %.1f) fits within the balloon"),
							    *Art->GetName(), Panel, Offset.X, Offset.Y),
							    FMath::Square(Offset.X / 114.) + FMath::Square(Offset.Y / 58.) < 1.)) return true;
						}
					}
				}
				if (bTitle && Step == 3 && !CheckOptionButtons()) return true;
				if (bTitle && (Step == 3 || Step == 11 || Step == 12) && !CheckSettingsScrollWidth()) return true;
				if (!bTitle && (Step == 0 || Step == 1 || Step == 2 || Step == 23))
				{
					const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
					const double Scale = FMath::Min(Size.X / 1440., Size.Y / 810.);
					const FVector2D Origin = (FVector2D(Size) - FVector2D(1440, 810) * Scale) * .5;
					auto AtHeaderPosition = [&](FName Id, double CenterX)
					{
						const auto* Hit = HUD->GetHitBoxWithName(Id);
						return Hit && Hit->Contains(Origin + FVector2D(CenterX, 45) * Scale);
					};
					if (!Test->TestTrue(TEXT("Lobby and stage keep Settings, Players and Leave in the same positions"),
					        AtHeaderPosition(TEXT("OpenOptions"), 1135) && AtHeaderPosition(TEXT("Players"), 1230) &&
					        AtHeaderPosition(TEXT("Leave"), 1349))) return true;
				}
				if (!bTitle && Step == 47)
				{
					if (!Test->TestNotNull(TEXT("Exit confirmation offers cancel"), HUD->GetHitBoxWithName(TEXT("CancelLeave"))) ||
					    !Test->TestNotNull(TEXT("Exit confirmation offers confirm"), HUD->GetHitBoxWithName(TEXT("ConfirmLeave"))) ||
					    !Test->TestNull(TEXT("Lobby controls are hidden behind the prompt"), HUD->GetHitBoxWithName(TEXT("Ready")))) return true;
				}
				if (!bTitle && Step == 9 &&
				    !Test->TestTrue(TEXT("The loading capture contains the game's transition overlay"),
				        UTBLoadingScreenSubsystem::IsCovering(World->GetGameInstance()))) return true;
				if (!bTitle && Step == 14)
				{
					const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
					const double Scale = FMath::Min(Size.X / 1600., Size.Y / 900.);
					const FVector2D Origin = FVector2D(Size) * .5 - FVector2D(384, 246.5) * Scale;
					const auto Covers = [&](FName Id, FVector2D Center)
					{
						const FHUDHitBox* Hit = HUD->GetHitBoxWithName(Id);
						return Hit && Hit->Contains(Origin + Center * Scale);
					};
					if (!Test->TestTrue(TEXT("Reset, resume and players respond on their three hanging cards"),
					        Covers(TEXT("Unstuck"), {150, 280}) && Covers(TEXT("Resume"), {384, 280}) &&
					        Covers(TEXT("Players"), {618, 280})) ||
					    !Test->TestTrue(TEXT("Settings and leave respond in the pause footer"),
					        Covers(TEXT("OpenOptions"), {548, 447}) && Covers(TEXT("Leave"), {675, 447}))) return true;
				}
				if (!bTitle && (Step == 1 || Step == 2 || Step == 23))
				{
					const bool bNPCEnabled = Step != 1;
					if (!Test->TestEqual(TEXT("NPC human controls appear only while enabled"),
					    HUD->GetHitBoxWithName(TEXT("DraftMoreNPCHumans")) != nullptr, bNPCEnabled) ||
					    !Test->TestEqual(TEXT("NPC toy controls appear only while enabled"),
					    HUD->GetHitBoxWithName(TEXT("DraftMoreNPCToys")) != nullptr, bNPCEnabled)) return true;
					if (!Test->TestNotNull(TEXT("Stage has the shared settings entry"), HUD->GetHitBoxWithName(TEXT("OpenOptions"))) ||
					    !Test->TestNotNull(TEXT("Room can be left from the stage menu"), HUD->GetHitBoxWithName(TEXT("Leave")))) return true;
				}
				if (!bTitle && ((Step >= 10 && Step <= 15 && Step != 12) || Step == 19 || Step == 21 || Step == 22 || Step >= 24) &&
				    !Test->TestFalse(TEXT("HUD capture is not obscured by preparation masking"), PC->ShouldHidePreparationView()))
					return true;
				if (!bTitle && (Step == 21 || Step == 22))
				{
					if (!Test->TestTrue(TEXT("Gaze capture uses the possessed pawn view"), Body.IsValid() &&
					    PC->GetPawn() == Body.Get() && PC->GetViewTarget() == Body.Get())) return true;
					if (!Test->TestTrue(TEXT("Human gaze has reached full camera zoom"), Body->IsHuman() &&
					    Body->IsGazing() && Body->GetGazeZoom() > .98f)) return true;
					if (!Test->TestNotNull(TEXT("Gaze camera manager"), PC->PlayerCameraManager.Get())) return true;
					const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
					if (!Test->TestTrue(TEXT("Gaze view keeps the authored room overview"), bOverviewActive &&
					    View.Location.Equals(OverviewLocation, 2.f) && View.Rotation.Equals(OverviewRotation, 1.f))) return true;
					if (Step == 22)
					{
						if (!Test->TestTrue(TEXT("Gaze camera renders constrained side bars"), View.bConstrainAspectRatio &&
						    FMath::IsNearlyEqual(View.AspectRatio, 4.f / 3.f, .001f) && PC->ViewAspect > View.AspectRatio * 1.01f)) return true;
					}
				}
				if (!bTitle && Step == 4 &&
				    !Test->TestTrue(TEXT("Chat remains open on the capture frame"), PC->IsChatOpen())) return true;
				if (!bTitle && Step >= 24 && Step <= 28)
				{
					if (!Test->TestTrue(TEXT("Peripheral feedback capture uses the local pawn camera"),
					    Body.IsValid() && PC->GetViewTarget() == Body.Get())) return true;
					const bool bExpectedHuman = Step == 28;
					if (!Test->TestEqual(TEXT("Peripheral feedback uses the expected team"), Body->IsHuman(), bExpectedHuman) ||
					    !Test->TestEqual(TEXT("Seen feedback is driven by the local gaze state"), Body->IsSeen(), Step == 24) ||
					    !Test->TestEqual(TEXT("Gazed feedback is driven by the local gaze state"), Body->IsGazed(), Step == 25) ||
					    !Test->TestEqual(TEXT("Frozen feedback is driven by the local frozen state"), Body->IsGazeFrozen(), Step == 26)) return true;
				}
				if (!bTitle && Step >= 29 && Step <= 36 && !CheckNavigation()) return true;
				if (!bTitle && Step == 37 && !Test->TestTrue(TEXT("Floor marker is a visible active transient event"),
				    NavigationMom.IsValid() && !NavigationMom->IsHidden() &&
				    ATBMomArrivalZone::FindActive(World.Get()) == NavigationMom.Get())) return true;
				if (!bTitle && (Step == 38 || Step == 39) &&
				    !Test->TestTrue(TEXT("Box guidance follows the real player state and course lookup"),
				        Player->Team == ETBTeam::Toy && Player->ToyState == ETBToyState::Boxed &&
				        (ATBBoxEscapeCourse::Find(World.Get(), State->Box) != nullptr) == (Step == 38))) return true;
				if (!bTitle && Step >= 40 && Step <= 44 && !CheckStorage()) return true;
				if (!bTitle && Step >= 48 && Step <= 50)
				{
					const bool bBoarded = Step != 48;
					if (!Test->TestTrue(TEXT("Escape guide capture uses the current toy's replicated boarding state"),
					    ReviewCar.IsValid() && ReviewExit.IsValid() && Body.IsValid() && State->bEscapePhase &&
					    Player->Team == ETBTeam::Toy && Player->ToyState == ETBToyState::Free &&
					    Body->EscapeVehicle.Get() == (bBoarded ? ReviewCar.Get() : nullptr))) return true;
					const FVector Target = bBoarded ? ReviewExit->GetActorLocation() : ReviewCar->GetActorLocation();
					FVector2D Screen;
					const float Width = GEngine->GameViewport->Viewport->GetSizeXY().X;
					if (!Test->TestTrue(TEXT("The photographed target is on the requested side of the actual camera"),
					    PC->ProjectWorldLocationToScreen(Target, Screen) &&
					    (Step == 48 ? Screen.X < 0 : Step == 49 ? Screen.X > Width : Screen.X > Width * .2f && Screen.X < Width * .8f)))
						return true;
				}
				if (!bTitle && (Step == 45 || Step == 46) &&
				    !Test->TestTrue(TEXT("Result capture shows the local winner after the reveal"),
				        State->Phase == ETBPhase::Results && PC->IsResultShown() && Body.IsValid() &&
				        Body->IsCelebrating() && Body->IsHuman() == (Step == 45))) return true;
				const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
				FString CaptureRoot;
				if (!FParse::Value(FCommandLine::Get(), TEXT("UIReviewOutput="), CaptureRoot))
					CaptureRoot = FPaths::ProjectSavedDir() / (bCompactHintsOnly ? TEXT("UIRefresh/CompactHints") : TEXT("UIRefresh/ScreenshotsV2"));
				const FString Folder = CaptureRoot / TBText::GetLanguage() /
				    FString::Printf(TEXT("%dx%d"), Size.X, Size.Y);
				IFileManager::Get().MakeDirectory(*Folder, true);
				Path = Folder / (Name + TEXT(".png"));
				FScreenshotRequest::RequestScreenshot(Path, true, false);
				bRequested = true;
				Frames = 0;
				return false;
			}
			if (FScreenshotRequest::IsScreenshotRequested()) return false;
			Test->TestTrue(*FString::Printf(TEXT("Rendered screenshot: %s"), *Name),
			              IFileManager::Get().FileSize(*Path) > 1024);
			if (bRandomOnly || (!bTitle && Step == 46) || (bTitle && Step == 13)) return true;
			// Photograph the settings return before the existing match sequence;
			// retain the earlier step indices used by transition/gaze assertions.
			if (bCompactHintsOnly && Step == 0) Step = 9;
			else if (bCompactHintsOnly && Step == 10) Step = 28;
			else if (bCompactHintsOnly && Step == 36) Step = 40;
			else if (!bTitle && Step == 0) Step = 47;
			else if (!bTitle && Step == 47) Step = 1;
			else if (bTitle && Step == 0) Step = 4;
			else if (bTitle && Step == 9) { RestoreTitleButton(); Step = 1; }
			else if (bTitle && Step == 1) Step = 10;
			else if (bTitle && Step == 10) Step = 2;
			else if (bTitle && Step == 3) Step = 11;
			else if (!bTitle && Step == 8) Step = 23;
			else if (!bTitle && Step == 23) Step = 9;
			else if (!bTitle && Step == 22) Step = 24;
			else if (!bTitle && Step == 44) Step = 48;
			else if (!bTitle && Step == 50) Step = 45;
			else ++Step;
			if (bRosterOnly && Step == 3) Step = 18;
			else if (bRosterOnly && Step == 19) return true;
			bEntered = false;
			bRequested = false;
			Deadline = FPlatformTime::Seconds() + CaptureTimeout;
			return false;
		}

	private:
		// Each screenshot has its own deadline so a long series can run at native display resolution.
		static constexpr double CaptureTimeout = 60.;
		float StorageFraction() const { return Step == 40 ? 0.f : Step == 41 ? .5f : .95f; }

		void ClearEscapeReview()
		{
			if (ReviewCar.IsValid()) ReviewCar->Destroy();
			if (ReviewExit.IsValid()) ReviewExit->Destroy();
			ReviewCar.Reset();
			ReviewExit.Reset();
		}

		void RestoreStorage()
		{
			if (!bStorageActive) return;
			if (Body.IsValid())
			{
				Body->bHoldingInteract = bOldHoldingInteract;
				Body->bRescuing = bOldStorageRescuing;
				Body->VehicleExitStarted = OldVehicleExitStarted;
				Body->SetActorTickEnabled(bOldStorageTick);
			}
			bStorageActive = false;
		}

		bool CheckStorage()
		{
			if (!Test->TestTrue(TEXT("Storage review uses a human pawn in the active round"),
			        Body.IsValid() && Body->IsHuman() && State->Phase == ETBPhase::Playing &&
			        !State->IsPreparing() && State->Box == NavigationBox.Get()) ||
			    !Test->TestTrue(TEXT("Storage fixture suspends pawn simulation only for the capture"),
			        bStorageActive && !Body->IsActorTickEnabled())) return false;
			if (Step == 44)
				return Test->TestTrue(TEXT("Completed storage state has no carried toy or active timer"),
				    !Body->HasCarriedToy() && Body->StoreStarted < 0 && !Body->bHoldingInteract);
			if (!Test->TestTrue(TEXT("Storage and cancelled attempts retain the carried toy"),
			        Body->CarriedDummy == NavigationDummy.Get() && Body->HasCarriedToy())) return false;
			if (!Test->TestTrue(TEXT("Storage and cancelled captures remain in reach of the box front"),
			        NavigationBox->CanStoreFrom(Body.Get()))) return false;
			if (Step == 43)
				return Test->TestTrue(TEXT("Releasing storage clears the active timer without dropping the toy"),
				    Body->StoreStarted < 0 && !Body->bHoldingInteract);
			return Test->TestTrue(TEXT("Storage sample is driven by the server-clock start time"),
			    Body->StoreStarted >= 0 && Body->bHoldingInteract && FMath::IsNearlyEqual(
			        float((State->GetServerWorldTimeSeconds() - Body->StoreStarted) / TB::Rules().Box.StoreSeconds),
			        StorageFraction(), .001f));
		}

		bool BeginNavigation()
		{
			if (!Test->TestTrue(TEXT("Direction fixture starts with a human local pawn"),
			        Body.IsValid() && Body->IsHuman() && PC->GetViewTarget() == Body.Get()) ||
			    !Test->TestNull(TEXT("Direction fixture does not replace an authored active mom event"),
			        ATBMomArrivalZone::FindActive(World.Get()))) return false;
			OldNavigationBox = State->Box;
			OldCarriedToy = Body->CarriedToy;
			OldCarriedDummy = Body->CarriedDummy;
			OldStoreStarted = Body->StoreStarted;
			bNavigationActive = true;
			FActorSpawnParameters Spawn;
			Spawn.ObjectFlags |= RF_Transient;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector Location = Body->GetActorLocation();
			NavigationBox = World->SpawnActor<ATBBox>(Location, FRotator::ZeroRotator, Spawn);
			NavigationDummy = World->SpawnActor<ATBDummyToy>(Body->CarryAnchor->GetComponentLocation(),
			    FRotator::ZeroRotator, Spawn);
			NavigationMom = World->SpawnActor<ATBMomArrivalZone>(Location, FRotator::ZeroRotator, Spawn);
			if (!Test->TestNotNull(TEXT("Transient direction box"), NavigationBox.Get()) ||
			    !Test->TestNotNull(TEXT("Transient carried dummy"), NavigationDummy.Get()) ||
			    !Test->TestNotNull(TEXT("Transient mom zone"), NavigationMom.Get())) return false;
			for (AActor* Actor : {static_cast<AActor*>(NavigationBox.Get()),
			         static_cast<AActor*>(NavigationDummy.Get()), static_cast<AActor*>(NavigationMom.Get())})
			{
				Actor->SetActorTickEnabled(false);
				Actor->SetActorEnableCollision(false);
				Actor->SetActorHiddenInGame(true);
			}
			State->Box = NavigationBox.Get();
			Body->CarriedToy = nullptr;
			Body->CarriedDummy = NavigationDummy.Get();
			Body->StoreStarted = -1;
			return true;
		}

		void PositionNavigation(float BoxForward, float BoxRight, float MomForward = 900.f, float MomRight = 2800.f)
		{
			const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
			auto Point = [&](float Forward, float Right)
			{
				return View.Location + View.Rotation.RotateVector(FVector(Forward, Right, 0));
			};
			// The box marker points above its interaction anchor; keep that point at camera height.
			NavigationBox->SetActorLocation(Point(BoxForward, BoxRight) -
			    NavigationBox->InteractionPoint->GetRelativeLocation() - FVector(0, 0, 90), false, nullptr,
			    ETeleportType::TeleportPhysics);
			NavigationMom->SetActorLocation(Point(MomForward, MomRight), false, nullptr, ETeleportType::TeleportPhysics);
		}

		void RestoreNavigation()
		{
			if (!bNavigationActive) return;
			if (NavigationCourse.IsValid()) NavigationCourse->Destroy();
			NavigationCourse.Reset();
			if (State.IsValid()) State->Box = OldNavigationBox.Get();
			if (Body.IsValid())
			{
				Body->CarriedToy = OldCarriedToy.Get();
				Body->CarriedDummy = OldCarriedDummy.Get();
				Body->StoreStarted = OldStoreStarted;
			}
			if (NavigationMom.IsValid()) { NavigationMom->ResetEvent(); NavigationMom->Destroy(); }
			if (NavigationBox.IsValid()) NavigationBox->Destroy();
			if (NavigationDummy.IsValid()) NavigationDummy->Destroy();
			NavigationMom.Reset(); NavigationBox.Reset(); NavigationDummy.Reset();
			bNavigationActive = false;
		}

		bool CheckNavigation()
		{
			if (!Test->TestTrue(TEXT("Direction capture retains the human pawn camera"),
			        Body.IsValid() && Body->IsHuman() && PC->GetViewTarget() == Body.Get()) ||
			    !Test->TestTrue(TEXT("Direction capture stays in the active round without advancing gameplay"),
			        State->Phase == ETBPhase::Playing && !State->IsPreparing() &&
			        Mode.IsValid() && !Mode->IsActorTickEnabled())) return false;
			if (Step == 36)
			{
				return Test->TestTrue(TEXT("Direction fixture restores original carried state and box"),
				           Body->CarriedToy == OldCarriedToy.Get() && Body->CarriedDummy == OldCarriedDummy.Get() &&
				           State->Box == OldNavigationBox.Get() && Body->StoreStarted == OldStoreStarted) &&
				    Test->TestNull(TEXT("Temporary mom direction has cleared"), ATBMomArrivalZone::FindActive(World.Get()));
			}
			if (!Test->TestTrue(TEXT("Box direction is driven by the real carrying state"), Body->HasCarriedToy() &&
			        Body->CarriedDummy == NavigationDummy.Get() && State->Box == NavigationBox.Get())) return false;
			const bool MomActive = Step >= 32;
			if (!Test->TestTrue(TEXT("Mom direction is driven by the actual active-zone lookup"),
			        ATBMomArrivalZone::FindActive(World.Get()) == (MomActive ? NavigationMom.Get() : nullptr))) return false;
			const FVector BoxPoint = NavigationBox->InteractionPoint->GetComponentLocation() + FVector(0, 0, 90);
			FVector2D BoxScreen, MomScreen;
			const bool BoxInFront = PC->ProjectWorldLocationToScreen(BoxPoint, BoxScreen);
			const bool MomInFront = PC->ProjectWorldLocationToScreen(NavigationMom->GetActorLocation(), MomScreen);
			const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
			if (Step == 31)
			{
				const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
				return Test->TestTrue(TEXT("Behind-box fixture is behind the actual rendered camera"),
				    !BoxInFront && FVector::DotProduct(BoxPoint - View.Location, View.Rotation.Vector()) < -100.f);
			}
			if (!Test->TestTrue(TEXT("Direction targets are projectable ahead of the camera"),
			        BoxInFront && (!MomActive || MomInFront))) return false;
			if (Step == 34)
			{
				auto Inside = [Size](FVector2D Position)
				{
					return Position.X > Size.X * .2f && Position.X < Size.X * .8f &&
					    Position.Y > Size.Y * .2f && Position.Y < Size.Y * .8f;
				};
				return Test->TestTrue(TEXT("Both direction targets are inside the visible camera area"),
				    Inside(BoxScreen) && Inside(MomScreen));
			}
			const bool BoxLeft = Step == 29 || Step == 32;
			if (!Test->TestTrue(TEXT("Box fixture lies beyond the requested camera edge"),
			        BoxLeft ? BoxScreen.X < 0 : BoxScreen.X > Size.X)) return false;
			if (MomActive && !Test->TestTrue(TEXT("Mom fixture lies beyond the right camera edge"), MomScreen.X > Size.X))
				return false;
			if (Step == 35)
			{
				const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
				return Test->TestTrue(TEXT("Direction capture uses constrained side bars"), View.bConstrainAspectRatio &&
				    FMath::IsNearlyEqual(View.AspectRatio, 4.f / 3.f, .001f) && PC->ViewAspect > View.AspectRatio * 1.01f);
			}
			return true;
		}

		bool BeginOverview()
		{
			if (!Test->TestNotNull(TEXT("Overview character"), Body.Get()) ||
			    !Test->TestNotNull(TEXT("Overview pawn camera"), ReviewCamera.Get())) return false;
			if (!bOverviewActive)
			{
				UCameraComponent* AuthoredCamera = nullptr;
				for (TActorIterator<ACameraActor> It(World.Get()); It; ++It)
				{
					if (!It->ActorHasTag(TEXT("TBLobbyCamera"))) continue;
					AuthoredCamera = It->GetCameraComponent();
					break;
				}
				if (!Test->TestNotNull(TEXT("Authored lobby overview camera"), AuthoredCamera)) return false;
				OverviewMovement = Cast<UTBMovement>(Body->GetCharacterMovement());
				if (!Test->TestNotNull(TEXT("Overview character movement"), OverviewMovement.Get())) return false;
				OverviewLocation = AuthoredCamera->GetComponentLocation();
				OverviewRotation = AuthoredCamera->GetComponentRotation();
				OldBodyTransform = Body->GetActorTransform();
				OldControlRotation = PC->GetControlRotation();
				OldMovementMode = OverviewMovement->MovementMode;
				OldCustomMovementMode = OverviewMovement->CustomMovementMode;
				OldVelocity = OverviewMovement->Velocity;
				bOldMovementTick = OverviewMovement->IsComponentTickEnabled();
				bOverviewActive = true;
				// ApplyState revives MOVE_None; suspend only movement ticking so the
				// actual pawn CalcCamera still drives gaze zoom without falling.
				OverviewMovement->SetComponentTickEnabled(false);
				OverviewMovement->StopMovementImmediately();
			}
			Body->ApplyState();
			HoldOverview();
			return true;
		}

		void HoldOverview()
		{
			if (!bOverviewActive || !Body.IsValid() || !ReviewCamera.IsValid() || !PC.IsValid()) return;
			PC->SetControlRotation(OverviewRotation);
			const FRotator Heading(0.f, OverviewRotation.Yaw, 0.f);
			if (!Body->GetActorRotation().Equals(Heading, .01f)) Body->SetActorRotation(Heading, ETeleportType::TeleportPhysics);
			// Human/toy eye heights differ. Align the real camera component, not
			// BaseEyeHeight or an assumed offset, after each role's ApplyState.
			const FVector CameraOffset = ReviewCamera->GetComponentLocation() - Body->GetActorLocation();
			const FVector Position = OverviewLocation - CameraOffset;
			if (!Body->GetActorLocation().Equals(Position, .01f))
				Body->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
		}

		void RestoreOverview()
		{
			if (!bOverviewActive) return;
			if (Body.IsValid()) Body->SetActorTransform(OldBodyTransform, false, nullptr, ETeleportType::TeleportPhysics);
			if (PC.IsValid()) PC->SetControlRotation(OldControlRotation);
			if (OverviewMovement.IsValid())
			{
				OverviewMovement->SetMovementMode(OldMovementMode, OldCustomMovementMode);
				OverviewMovement->Velocity = OldVelocity;
				OverviewMovement->SetComponentTickEnabled(bOldMovementTick);
			}
			bOverviewActive = false;
		}

		void RestoreChat()
		{
			if (!SampleChat.IsValid()) return;
			SampleChat->Clear();
			// The public log API restores content/order, not IDs or timestamps.
			// This opt-in fixture requires a fresh standalone process.
			for (const FTBChatLine& Line : OldChatLines)
			{
				if (Line.bNotice) SampleChat->AddNotice(Line.Text);
				else SampleChat->Add(Line.Name, Line.Text, Line.bSelf);
			}
			SampleChat.Reset();
			OldChatLines.Reset();
		}

		bool ClickTitle(FName ButtonName)
		{
			auto* Button = Title.IsValid() ? Cast<UButton>(Title->WidgetTree->FindWidget(ButtonName)) : nullptr;
			if (!Test->TestNotNull(*ButtonName.ToString(), Button)) return false;
			Button->OnClicked.Broadcast();
			return true;
		}

		bool CheckOptionButtons()
		{
			bool Valid = true;
			for (const TCHAR* ButtonName : {TEXT("SettingsTab0"), TEXT("SettingsTab1"), TEXT("SettingsTab2"),
			    TEXT("SettingsTab3"), TEXT("SettingsReset"), TEXT("SettingsClose")})
			{
				auto* Button = Cast<UButton>(Options->WidgetTree->FindWidget(ButtonName));
				if (!Test->TestNotNull(ButtonName, Button)) return false;
				const auto& Frame = Button->GetCachedGeometry();
				Valid &= Test->TestTrue(TEXT("Settings controls have a visible inked face"),
				    Frame.GetLocalSize().GetMin() >= 30 && Button->GetStyle().Normal.DrawAs == ESlateBrushDrawType::RoundedBox);
				bool bMeasured = false;
				UWidgetTree::ForEachWidgetAndChildrenUntil(Button, [&](UWidget* Child)
				{
					const auto* Caption = Cast<UTextBlock>(Child);
					if (!Caption) return true;
					const auto& Text = Caption->GetCachedGeometry();
					const FVector2D TL = Frame.AbsoluteToLocal(Text.LocalToAbsolute(FVector2D::ZeroVector));
					const FVector2D BR = Frame.AbsoluteToLocal(Text.LocalToAbsolute(Text.GetLocalSize()));
					bMeasured = !Caption->GetText().IsEmpty() && Text.GetLocalSize().GetMin() > 0 &&
					    TL.X >= 2 && TL.Y >= 1 && BR.X <= Frame.GetLocalSize().X - 2 && BR.Y <= Frame.GetLocalSize().Y - 1;
					return false;
				});
				Valid &= Test->TestTrue(TEXT("Editable settings label stays inside its button"), bMeasured);
			}
			return Valid;
		}

		bool CheckSettingsScrollWidth()
		{
			bool Valid = true;
			int32 MeasuredControls = 0;
			Options->WidgetTree->ForEachWidget([&](UWidget* Widget)
			{
				auto* Scroll = Cast<UScrollBox>(Widget);
				if (!Scroll || Scroll->GetVisibility() != ESlateVisibility::Visible) return;
				auto* List = Cast<UVerticalBox>(Scroll->GetChildAt(0));
				if (!List) return;
				const auto& Content = List->GetCachedGeometry();
				for (UWidget* Child : List->GetAllChildren())
				{
					auto* Row = Cast<UUserWidget>(Child);
					if (!Row) continue;
					Row->WidgetTree->ForEachWidget([&](UWidget* Control)
					{
						if (!Control->IsA<UButton>() && !Control->IsA<UComboBoxString>() && !Control->IsA<USlider>()) return;
						const auto& Geometry = Control->GetCachedGeometry();
						if (Geometry.GetLocalSize().GetMin() <= 0) return;
						const auto Corner = Content.AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize()));
						Valid &= Test->TestTrue(*FString::Printf(TEXT("Scroll content leaves right padding for %s"), *Control->GetName()),
						    Corner.X <= Content.GetLocalSize().X - 4.f);
						++MeasuredControls;
					});
				}
			});
			return Test->TestTrue(TEXT("Measured settings row controls"), MeasuredControls > 0) && Valid;
		}

		bool PrepareTitleReview()
		{
			bool bValid = true;
			for (const TCHAR* ButtonName : {TEXT("HostButton"), TEXT("FindButton"), TEXT("HelpButton"),
			    TEXT("OptionsButton"), TEXT("QuitButton")})
			{
				auto* Button = Cast<UButton>(Title->WidgetTree->FindWidget(FName(ButtonName)));
				if (!Test->TestNotNull(ButtonName, Button)) return false;
				UWidgetTree::ForEachWidgetAndChildrenUntil(Button, [&](UWidget* Widget)
				{
					const auto Slate = Widget->GetCachedWidget();
					const auto Tooltip = Slate.IsValid() ? Slate->GetToolTip() : nullptr;
					bValid &= Test->TestTrue(*FString::Printf(TEXT("Title control has no default tooltip: %s/%s"),
					    ButtonName, *Widget->GetName()), Widget->GetToolTipText().IsEmpty() && !Widget->GetToolTip() &&
					    (!Tooltip.IsValid() || Tooltip->IsEmpty()));
					return true;
				});
			}
			bool bHasSteamLogo = false;
			bool bHasCover = false;
			Title->WidgetTree->ForEachWidget([&](UWidget* Widget)
			{
				const auto* Image = Cast<UImage>(Widget);
				if (!Image) return;
				const FName Art = GetFNameSafe(Image->GetBrush().GetResourceObject());
				bHasSteamLogo |= Art == TEXT("T_UI_TitleSteamMark");
				bHasCover |= Art == TEXT("T_UI_TitleComicCover");
			});
			bValid &= Test->TestTrue(TEXT("Title displays the Steam logo"), bHasSteamLogo);
			bValid &= Test->TestFalse(TEXT("Left cover no longer obscures the title sequence"), bHasCover);
			for (const TCHAR* Art : {TEXT("Frame_Settings"),
			    TEXT("Icon_Hourglass"), TEXT("Icon_BatteryPickup"), TEXT("StagePreview_Demo"), TEXT("StagePreview_House")})
			{
				auto* Texture = TBPaper::Art(Art);
				if (!Test->TestNotNull(*FString::Printf(TEXT("UI review artwork loads: %s"), Art), Texture)) return false;
				ReviewTextures.Emplace(Texture);
			}
			return bValid;
		}

		void RestoreTitleButton()
		{
			if (TitleSlateButton.IsValid())
			{
				// Release without a key-up/click while every action delegate is still detached.
				TitleSlateButton->OnMouseCaptureLost(FCaptureLostEvent(0, 0));
				TitleSlateButton->OnMouseLeave(FPointerEvent());
				if (bOldTitleHovered)
					TitleSlateButton->OnMouseEnter(TitleSlateButton->GetCachedGeometry(), FPointerEvent());
			}
			if (TitleVisualButton.IsValid())
			{
				TitleVisualButton->SetPressMethod(OldTitlePressMethod);
				TitleVisualButton->OnClicked = OldTitleClicked;
				TitleVisualButton->OnPressed = OldTitlePressed;
				TitleVisualButton->OnReleased = OldTitleReleased;
			}
			TitleSlateButton.Reset();
			TitleVisualButton.Reset();
		}

		bool HoldTitleButton()
		{
			if (!TitleSlateButton->IsHovered())
				TitleSlateButton->OnMouseEnter(TitleSlateButton->GetCachedGeometry(), FPointerEvent());
			if (bTitlePressed && !TitleSlateButton->IsPressed())
			{
				// UE 5.8 exposes AppearPressed only to subclasses. Use the real Slate
				// pressed state, with UButton actions detached and no key-up dispatched.
				const FKeyEvent Accept(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
				return Test->TestTrue(TEXT("Title visual press is handled"),
				    TitleSlateButton->OnKeyDown(TitleSlateButton->GetCachedGeometry(), Accept).IsEventHandled());
			}
			return true;
		}

		bool CheckTitleButton()
		{
			if (!Test->TestTrue(TEXT("Title visual fixture has no bound click/press/release actions"),
			    !TitleVisualButton->OnClicked.IsBound() && !TitleVisualButton->OnPressed.IsBound() &&
			    !TitleVisualButton->OnReleased.IsBound()) ||
			    !Test->TestTrue(TEXT("UMG title control is hovered"), TitleVisualButton->IsHovered()) ||
			    !Test->TestEqual(TEXT("UMG title control reports the captured pressed state"),
			        TitleVisualButton->IsPressed(), bTitlePressed)) return false;
			const TCHAR* Key = TitleVisualButton->GetFName() == FName(TEXT("HostButton")) ? TEXT("Root.Title.Host") :
			    TitleVisualButton->GetFName() == FName(TEXT("FindButton")) ? TEXT("Root.Title.Find") : TEXT("Root.Title.Help");
			bool bMatches = false;
			UWidgetTree::ForEachWidgetAndChildrenUntil(TitleVisualButton.Get(), [&](UWidget* Widget)
			{
				if (const auto* Caption = Cast<UTextBlock>(Widget)) bMatches = Caption->GetText().EqualTo(TBText::Get(Key));
				return !bMatches;
			});
			const auto& Style = TitleVisualButton->GetStyle();
			return Test->TestTrue(TEXT("Pressed and hovered ink colors differ"),
			           Style.Pressed.TintColor != Style.Hovered.TintColor) &&
			       Test->TestTrue(TEXT("Cover button keeps the localized caption through mouse states"), bMatches);
		}

		bool EnterTitle()
		{
			switch (Step)
			{
			case 0:
			{
				TArray<UUserWidget*> Found;
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World.Get(), Found, UTBTitleMenu::StaticClass(), true);
				if (!Test->TestEqual(TEXT("One actual title menu"), Found.Num(), 1)) return false;
				Title = Cast<UTBTitleMenu>(Found[0]);
				Title->SkipStory();
				if (!PrepareTitleReview()) return false;
				Name = TEXT("01-title");
				break;
			}
			case 1:
				if (!ClickTitle(TEXT("HelpButton"))) return false;
				Name = TEXT("02-how-to-play");
				break;
			case 10:
				if (!ClickTitle(TEXT("HelpHumanTab"))) return false;
				Name = TEXT("02-how-to-play-human");
				break;
			case 2:
				if (!ClickTitle(TEXT("CloseHelp")) || !ClickTitle(TEXT("FindButton"))) return false;
				Test->TestTrue(TEXT("Room finder reveals session cards"), Title->AreRoomCardsShown());
				Name = TEXT("03-room-finder");
				break;
			case 3:
			{
				if (!ClickTitle(TEXT("BackFromFinder")) || !ClickTitle(TEXT("OptionsButton"))) return false;
				TArray<UUserWidget*> Found;
				UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World.Get(), Found, UTBOptionsMenu::StaticClass(), true);
				if (!Test->TestEqual(TEXT("Title opens shared options"), Found.Num(), 1)) return false;
				Options = Cast<UTBOptionsMenu>(Found[0]);
				Name = TEXT("04-title-options");
				break;
			}
			case 11: case 12: case 13:
				Options->ShowPage(Step - 10);
				Name = Step == 11 ? TEXT("04-title-display") :
				    Step == 12 ? TEXT("04-title-controls") : TEXT("04-title-language");
				break;
			case 4: case 5: case 6: case 7: case 8: case 9:
			{
				RestoreTitleButton();
				const TCHAR* Buttons[] = {TEXT("HostButton"), TEXT("FindButton"), TEXT("HelpButton")};
				const TCHAR* Captures[] = {TEXT("38-title-host-hover"), TEXT("39-title-host-pressed"),
				    TEXT("40-title-find-hover"), TEXT("41-title-find-pressed"), TEXT("42-title-help-hover"),
				    TEXT("43-title-help-pressed")};
				auto* Button = Cast<UButton>(Title->WidgetTree->FindWidget(FName(Buttons[(Step - 4) / 2])));
				if (!Test->TestNotNull(TEXT("Title visual control"), Button) ||
				    !Test->TestTrue(TEXT("Title visual control has a Slate widget"), Button->GetCachedWidget().IsValid()) ||
				    !Test->TestFalse(TEXT("Title control starts unpressed"), Button->IsPressed())) return false;
				TitleVisualButton = Button;
				TitleSlateButton = StaticCastSharedPtr<SButton>(Button->GetCachedWidget());
				OldTitleClicked = Button->OnClicked;
				OldTitlePressed = Button->OnPressed;
				OldTitleReleased = Button->OnReleased;
				OldTitlePressMethod = Button->GetPressMethod();
				bOldTitleHovered = Button->IsHovered();
				Button->OnClicked.Clear();
				Button->OnPressed.Clear();
				Button->OnReleased.Clear();
				Button->SetPressMethod(EButtonPressMethod::DownAndUp);
				bTitlePressed = Step % 2 == 1;
				Name = Captures[Step - 4];
				if (!HoldTitleButton()) return false;
				break;
			}
			default:
				return false;
			}
			return true;
		}

		bool EnterGame()
		{
			if (!PC.IsValid() || !State.IsValid() || !HUD.IsValid() || !Player.IsValid())
			{
				Test->AddError(TEXT("UI review lost its controller, HUD or state."));
				return false;
			}
			switch (Step)
			{
			case 0:
				State->Phase = ETBPhase::Lobby;
				Player->Team = ETBTeam::None;
				HUD->NotifyHitBoxClick(TEXT("CloseSettings"));
				Name = TEXT("10-lobby");
				break;
			case 47:
				HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
				HUD->NotifyHitBoxClick(TEXT("Leave"));
				Name = TEXT("10b-confirm-leave");
				break;
			case 1:
				HUD->DismissConfirmation();
				HUD->NotifyHitBoxClick(TEXT("CancelStage"));
				// Local screenshot state only; the fixture restores the original rules.
				State->Settings.NPCHumans = State->Settings.NPCToys = 0;
				HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
				Test->TestTrue(TEXT("Game settings opens directly to stage and rules"),
				    HUD->IsLobbySettingsOpen() && HUD->IsStagePickerOpen());
				// Select a known preview through the draft only; no map or rules save.
				HUD->NotifyHitBoxClick(bRandomOnly ? TEXT("StageRandom") : TEXT("StageExpanded"));
				Name = bRandomOnly ? TEXT("11-random-stage") : TEXT("11-room-settings");
				break;
			case 2:
				HUD->NotifyHitBoxClick(TEXT("DraftNextStage"));
				HUD->NotifyHitBoxClick(TEXT("ToggleDraftNPCs"));
				Test->TestTrue(TEXT("Changing the stage stays in the same settings screen"),
				    HUD->IsLobbySettingsOpen() && HUD->IsStagePickerOpen());
				Name = TEXT("12-stage-picker");
				break;
			case 3:
				HUD->NotifyHitBoxClick(TEXT("CancelStage"));
				Test->TestFalse(TEXT("Cancel closes stage and rules"), HUD->IsStagePickerOpen());
				Test->TestFalse(TEXT("Cancel returns directly to the lobby"), HUD->IsLobbySettingsOpen());
				HUD->NotifyHitBoxClick(TEXT("Players"));
				Test->TestTrue(TEXT("Player panel blocks chat"), HUD->IsChatBlocked());
				Name = TEXT("13-players");
				break;
			case 4:
				HUD->NotifyHitBoxClick(TEXT("ClosePlayers"));
				// The previous panel removed the chat widget. Recreate it through the
				// normal controller tick before exercising OpenChat, as real input does.
				PC->PlayerTick(0.f);
				PC->OpenChat();
				Test->TestTrue(TEXT("Lobby chat opens"), PC->IsChatOpen());
				if (auto* Log = World->GetGameInstance()->GetSubsystem<UTBChatLog>())
				{
					// Local fixture data only: never use SendChat / RPCs.
					SampleChat = Log;
					OldChatLines = Log->GetLines();
					Log->Add(TEXT("くま"), TEXT("次はおもちゃで参加するね！"), false);
					Log->Add(TEXT("おもちゃ探検隊の長いお名前"),
					    TEXT("電池を見つけたよ！みんなの準備ができたら、リビングの出口に集まって一緒に脱出しよう！"), false);
					Log->Add(TEXT("あなた"), TEXT("了解！準備できたよ。"), true);
				}
				Name = TEXT("14-chat");
				break;
			case 5:
				PC->CloseChat();
				RestoreChat();
				HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
				HUD->NotifyHitBoxClick(TEXT("StageExpanded"));
				HUD->NotifyHitBoxClick(TEXT("DraftMoreTime"));
				HUD->NotifyHitBoxClick(TEXT("ToggleDraftNPCs"));
				HUD->NotifyHitBoxClick(TEXT("DraftMoreNPCHumans"));
				HUD->NotifyHitBoxClick(TEXT("OpenOptions"));
				{
					TArray<UUserWidget*> Found;
					UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World.Get(), Found, UTBOptionsMenu::StaticClass(), true);
					if (!Test->TestEqual(TEXT("Stage opens one shared settings menu"), Found.Num(), 1)) return false;
					Options = Cast<UTBOptionsMenu>(Found[0]);
				}
				if (!Test->TestNotNull(TEXT("Stage shared options opens"), Options.Get())) return false;
				Options->ShowPage(TBOptionsPage::Audio);
				Name = TEXT("15-options-audio");
				break;
			case 6:
				Options->ShowPage(TBOptionsPage::Display);
				Test->TestEqual(TEXT("Display tab selected"), Options->GetPage(), int32(TBOptionsPage::Display));
				Name = TEXT("16-options-display");
				break;
			case 7:
				Options->ShowPage(TBOptionsPage::Controls);
				Test->TestTrue(TEXT("Controls has real remapping rows"), !Options->GetKeyRows().IsEmpty());
				Name = TEXT("17-options-controls");
				break;
			case 8:
				Options->BeginKeyCapture(TB::Keys::LobbyReady);
				Test->TestTrue(TEXT("Key capture opens without changing a binding"), Options->IsCapturing());
				Name = TEXT("18-key-capture");
				break;
			case 9:
				if (Options.IsValid()) Options->Dismiss();
				HUD->NotifyHitBoxClick(TEXT("CancelStage"));
				Test->TestFalse(TEXT("Visual review cancels the returned draft before the match"), HUD->IsStagePickerOpen());
				Playing = MakeUnique<TBTest::FPlayingScope>(*State, State->GetServerWorldTimeSeconds() + 190);
				State->HidePreparation.Round = OldRound + 1;
				Player->Team = ETBTeam::Toy;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				if (!BeginOverview()) return false;
				PC->PlayerTick(0.f);
				Name = TEXT("19-loading");
				break;
			case 10:
				Name = TEXT("20-toy-hud");
				break;
			case 11:
				Player->Team = ETBTeam::Human;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				Name = TEXT("21-human-hud");
				break;
			case 12:
				Name = TEXT("22-hide-preparation");
				break;
			case 13:
				Player->Team = ETBTeam::Toy;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				State->bEscapePhase = true;
				State->Collected = State->Settings.RequiredItems;
				Name = TEXT("23-escape-hud");
				break;
			case 14:
				PC->ToggleLeaveMenu();
				Test->TestTrue(TEXT("Pause menu opens"), PC->IsLeaveMenuOpen());
				Name = TEXT("24-pause");
				break;
			case 15:
				HUD->NotifyHitBoxClick(TEXT("Resume"));
				Test->TestFalse(TEXT("Resume closes pause menu"), PC->IsLeaveMenuOpen());
				PC->ToggleLeaveMenu();
				HUD->NotifyHitBoxClick(TEXT("Players"));
				Test->TestTrue(TEXT("Match participant list opens from pause"), HUD->IsChatBlocked());
				Name = TEXT("25-match-players");
				break;
			case 16:
				HUD->NotifyHitBoxClick(TEXT("ClosePlayers"));
				HUD->NotifyHitBoxClick(TEXT("Resume"));
				State->Phase = ETBPhase::Results;
				State->Winner = ETBWinner::Toys;
				State->Reason = TEXT("乗り物で脱出！");
				State->ResultStyle = ETBResultStyle::None;
				State->ResultStartTime = State->GetServerWorldTimeSeconds() - 30;
				State->EndTime = State->GetServerWorldTimeSeconds() + 120;
				Name = TEXT("30-results-toys");
				break;
			case 17:
				State->Winner = ETBWinner::Humans;
				State->Reason = TEXT("All toys boxed");
				Name = TEXT("31-results-humans");
				break;
			case 18:
			{
				RestoreOverview();
				State->Phase = ETBPhase::Lobby;
				Player->Team = ETBTeam::None;
				Player->bReady = true;
				State->bEscapePhase = false;
				State->Settings.Humans = 4;
				State->Settings.Toys = 4;
				State->Settings.NPCHumans = State->Settings.NPCToys = 0;
				const TCHAR* Names[] = {TEXT("くま"), TEXT("小さな探検家"), TEXT("ながいなまえのプレイヤーさん"),
				    TEXT("おもちゃ係"), TEXT("ねこのロボット"), TEXT("HIDE AND SEEK"), TEXT("ともだち")};
				for (int32 Index = State->PlayerArray.Num(); Index < 8; ++Index)
				{
					FActorSpawnParameters Params;
					Params.ObjectFlags |= RF_Transient;
					auto* Dummy = World->SpawnActor<ATBPlayerState>(Params);
					if (!Test->TestNotNull(TEXT("Local visual roster entry"), Dummy)) return false;
					Roster.Add(Dummy);
					Dummy->SetPlayerName(Names[(Index - 1 + UE_ARRAY_COUNT(Names)) % UE_ARRAY_COUNT(Names)]);
					Dummy->SetPlayerId(-1000 - Index);
					Dummy->Preference = Index % 3 == 0 ? ETBTeam::None : Index % 3 == 1 ? ETBTeam::Toy : ETBTeam::Human;
					Dummy->bReady = Index % 3 != 0;
				}
				Test->TestEqual(TEXT("Full eight-player roster"), State->PlayerArray.Num(), 8);
				HUD->NotifyHitBoxClick(TEXT("OpenSettings"));
				Test->TestTrue(TEXT("Eight-player lobby opens directly to stage settings"),
				    HUD->IsLobbySettingsOpen() && HUD->IsStagePickerOpen());
				HUD->NotifyHitBoxClick(TEXT("Players"));
				Test->TestTrue(TEXT("Eight-player participant list blocks chat input"), HUD->IsChatBlocked());
				Name = TEXT("32-eight-player-roster");
				break;
			}
			case 19:
				HUD->NotifyHitBoxClick(TEXT("ClosePlayers"));
				HUD->NotifyHitBoxClick(TEXT("CloseSettings"));
				Playing->Hold(State->GetServerWorldTimeSeconds() + 10);
				State->HidePreparation.Round = OldRound + 2;
				Player->Team = ETBTeam::Toy;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				if (!BeginOverview()) return false;
				Name = TEXT("33-urgent-timer");
				break;
			case 20:
				PC->ToggleLeaveMenu();
				Options = UTBOptionsMenu::Open(PC.Get());
				if (!Test->TestNotNull(TEXT("Language settings opens"), Options.Get())) return false;
				Options->ShowPage(TBOptionsPage::Language);
				Name = TEXT("34-options-language");
				break;
			case 21:
				Options->Dismiss();
				if (PC->IsLeaveMenuOpen()) HUD->NotifyHitBoxClick(TEXT("Resume"));
				if (!Test->TestNotNull(TEXT("Gaze review character"), Body.Get()) ||
				    !Test->TestNotNull(TEXT("Gaze review camera"), ReviewCamera.Get())) return false;
				Player->Team = ETBTeam::Human;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				if (!BeginOverview()) return false;
				PC->PlayerTick(0.f);
				PC->SetViewTarget(Body.Get());
				bChangedGazeView = true;
				Body->SetGazeRequested(true);
				Name = TEXT("35-human-gaze");
				break;
			case 22:
				// Simulate ultrawide side bars at either review resolution without
				// resizing the viewport or changing a saved camera/DA setting.
				ReviewCamera->AspectRatio = 4.f / 3.f;
				ReviewCamera->bConstrainAspectRatio = true;
				Name = TEXT("36-human-gaze-letterbox");
				break;
			case 23:
			{
				const FKeyEvent Escape(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0);
				Test->TestTrue(TEXT("Escape cancels key capture"), Options->SendKeyDownForTest(Escape).IsEventHandled());
				Test->TestFalse(TEXT("Key capture has closed"), Options->IsCapturing());
				Test->TestTrue(TEXT("Cancelling capture keeps settings open"), Options->IsInViewport());
				Options->SendKeyUpForTest(Escape);
				Test->TestTrue(TEXT("Escape returns from settings"), Options->SendKeyDownForTest(Escape).IsEventHandled());
				Test->TestFalse(TEXT("Returned settings overlay is removed"), Options->IsInViewport());
				Test->TestTrue(TEXT("Settings return keeps the stage draft"), HUD->IsLobbySettingsOpen() && HUD->IsStagePickerOpen());
				Name = TEXT("37-stage-options-return");
				break;
			}
			case 24:
				Body->SetGazeRequested(false);
				ReviewCamera->AspectRatio = OldCameraAspect;
				ReviewCamera->bConstrainAspectRatio = bOldCameraConstrained;
				Player->Team = ETBTeam::Toy;
				Player->SetGaze(ETBGazeState::Seen);
				Player->bFrozen = false;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				Name = TEXT("44-toy-seen");
				break;
			case 25:
				Player->SetGaze(ETBGazeState::Gazed);
				Name = TEXT("45-toy-gazed");
				break;
			case 26:
				Player->SetGaze(ETBGazeState::Frozen);
				Player->bFrozen = true;
				Name = TEXT("46-toy-frozen");
				break;
			case 27:
				Player->SetGaze(ETBGazeState::None);
				Player->bFrozen = false;
				Name = TEXT("47-toy-gaze-cleared");
				break;
			case 28:
				Player->Team = ETBTeam::Human;
				if (bCompactHintsOnly)
				{
					Body->SetGazeRequested(false);
					Player->SetGaze(ETBGazeState::None);
					Player->bFrozen = false;
					PC->SetViewTarget(Body.Get());
					bChangedGazeView = true;
				}
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				Name = TEXT("48-human-gaze-cleared");
				break;
			case 29:
				if (!BeginNavigation()) return false;
				PositionNavigation(900, -2800);
				Name = TEXT("49-carry-box-left");
				break;
			case 30:
				PositionNavigation(900, 2800);
				Name = TEXT("50-carry-box-right");
				break;
			case 31:
				PositionNavigation(-1500, 300);
				Name = TEXT("51-carry-box-behind");
				break;
			case 32:
				PositionNavigation(900, -2800);
				if (!Test->TestTrue(TEXT("Transient mom event starts through the existing event API"),
				        NavigationMom->StartEvent())) return false;
				NavigationMom->State.Remaining = 12;
				Name = TEXT("52-box-mom-opposite");
				break;
			case 33:
				PositionNavigation(900, 2800);
				Name = TEXT("53-box-mom-same-edge");
				break;
			case 34:
				PositionNavigation(1500, -300, 1500, 300);
				Name = TEXT("54-box-mom-visible");
				break;
			case 35:
				ReviewCamera->AspectRatio = 4.f / 3.f;
				ReviewCamera->bConstrainAspectRatio = true;
				PositionNavigation(900, 2800);
				Name = TEXT("55-box-mom-letterbox");
				break;
			case 36:
				RestoreNavigation();
				ReviewCamera->AspectRatio = OldCameraAspect;
				ReviewCamera->bConstrainAspectRatio = bOldCameraConstrained;
				Name = TEXT("56-directions-cleared");
				break;
			case 37:
			{
				if (!BeginNavigation()) return false;
				Body->CarriedDummy = nullptr;
				FVector Forward = OverviewRotation.Vector();
				Forward.Z = 0;
				Forward.Normalize();
				// Start inside the authored room; raising above the camera can hit the roof first.
				const FVector Start = OverviewLocation + Forward * 650;
				FCollisionQueryParams Query(SCENE_QUERY_STAT(UIReviewFloor), false);
				Query.AddIgnoredActor(Body.Get());
				Query.AddIgnoredActor(NavigationBox.Get());
				Query.AddIgnoredActor(NavigationMom.Get());
				Query.AddIgnoredActor(NavigationDummy.Get());
				FHitResult Floor;
				if (!Test->TestTrue(TEXT("Marker review finds a real horizontal room surface"),
				    World->LineTraceSingleByChannel(Floor, Start, Start - FVector(0, 0, 4000), ECC_Visibility, Query) &&
				    Floor.ImpactNormal.Z > .8f && Floor.ImpactPoint.Z < OverviewLocation.Z - 20)) return false;
				NavigationMom->SetActorLocation(Floor.ImpactPoint, false, nullptr, ETeleportType::TeleportPhysics);
				NavigationMom->SetActorHiddenInGame(false);
				if (!Test->TestTrue(TEXT("Visible floor marker starts through the event API"), NavigationMom->StartEvent())) return false;
				NavigationMom->State.Remaining = 12;
				const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
				OverviewLocation = Floor.ImpactPoint - Forward * 180 + Right * 120 + FVector(0, 0, 140);
				OverviewRotation = (Floor.ImpactPoint + FVector(0, 0, 15) - OverviewLocation).Rotation();
				HoldOverview();
				Name = TEXT("57-mom-floor-marker");
				break;
			}
			case 38:
			{
				NavigationMom->ResetEvent();
				FActorSpawnParameters Spawn;
				Spawn.ObjectFlags |= RF_Transient;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				NavigationCourse = World->SpawnActor<ATBBoxEscapeCourse>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
				if (!Test->TestNotNull(TEXT("Transient boxed guidance course"), NavigationCourse.Get())) return false;
				NavigationCourse->SetActorTickEnabled(false);
				NavigationCourse->SetActorEnableCollision(false);
				NavigationCourse->SetActorHiddenInGame(true);
				NavigationCourse->Box = NavigationBox.Get();
				Player->Team = ETBTeam::Toy;
				Player->ToyState = ETBToyState::Boxed;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				Name = TEXT("58-box-escape-goal");
				break;
			}
			case 39:
				NavigationCourse->Destroy();
				NavigationCourse.Reset();
				Name = TEXT("59-box-rescue-goal");
				break;
			case 40:
			{
				RestoreNavigation();
				Player->Team = ETBTeam::Human;
				Player->ToyState = ETBToyState::Free;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				PC->SetViewTarget(Body.Get());
				Body->ApplyState();
				if (!BeginNavigation()) return false;
				HoldOverview();
				const FRotator BoxHeading(0.f, OverviewRotation.Yaw, 0.f);
				NavigationBox->SetActorRotation(BoxHeading, ETeleportType::TeleportPhysics);
				NavigationBox->SetActorLocation(Body->GetActorLocation() - BoxHeading.RotateVector(FVector(-80, 0, 65)),
				    false, nullptr, ETeleportType::TeleportPhysics);
				const FVector StorageTarget = NavigationBox->GetActorTransform().TransformPosition(FVector(-10, 0, 60));
				OverviewRotation = (StorageTarget - Body->GetPawnViewLocation()).Rotation();
				HoldOverview();
				bOldStorageTick = Body->IsActorTickEnabled();
				bOldHoldingInteract = Body->bHoldingInteract;
				bOldStorageRescuing = Body->bRescuing;
				OldVehicleExitStarted = Body->VehicleExitStarted;
				bStorageActive = true;
				Body->SetActorTickEnabled(false);
				Body->bHoldingInteract = true;
				Body->StoreStarted = State->GetServerWorldTimeSeconds();
				Name = TEXT("60-storage-start");
				break;
			}
			case 41:
				Name = TEXT("61-storage-half");
				break;
			case 42:
				Name = TEXT("62-storage-nearly-complete");
				break;
			case 43:
				Body->ServerReleaseInteract();
				Name = TEXT("63-storage-cancelled");
				break;
			case 44:
				// The state a client receives after successful storage; no authored box is mutated.
				Body->CarriedDummy = nullptr;
				Name = TEXT("64-storage-complete");
				break;
			case 45:
				ClearEscapeReview();
				RestoreNavigation();
				RestoreStorage();
				Player->Team = ETBTeam::Human;
				State->bEscapePhase = false;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				Body->ApplyState();
				State->Phase = ETBPhase::Results;
				State->Winner = ETBWinner::Humans;
				State->ResultStyle = ETBResultStyle::None;
				State->bVehicleEscapeResult = false;
				State->ResultStartTime = State->GetServerWorldTimeSeconds() - 30;
				State->EndTime = State->GetServerWorldTimeSeconds() + 120;
				Name = TEXT("65-results-local-human-winner");
				break;
			case 46:
				Player->Team = ETBTeam::Toy;
				State->Winner = ETBWinner::Toys;
				Body->ApplyState();
				Name = TEXT("66-results-local-toy-winner");
				break;
			case 48:
			{
				RestoreNavigation();
				RestoreStorage();
				RestoreOverview();
				Player->Team = ETBTeam::Toy;
				Player->ToyState = ETBToyState::Free;
				State->bEscapePhase = true;
				PC->ClientPreparationRole(State->HidePreparation.Round, Player->Team);
				if (!BeginOverview()) return false;
				FActorSpawnParameters Spawn;
				Spawn.ObjectFlags |= RF_Transient;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				const FVector Left = OverviewLocation + OverviewRotation.RotateVector(FVector(700, -3000, 0));
				const FVector Right = OverviewLocation + OverviewRotation.RotateVector(FVector(700, 3000, 0));
				ReviewCar = World->SpawnActor<ATBEscapeVehicle>(Left, FRotator::ZeroRotator, Spawn);
				ReviewExit = World->SpawnActor<ATBEscapeExit>(Right, FRotator::ZeroRotator, Spawn);
				if (!Test->TestNotNull(TEXT("Transient escape car"), ReviewCar.Get()) ||
				    !Test->TestNotNull(TEXT("Transient escape gate"), ReviewExit.Get())) return false;
				ReviewCar->SetActorTickEnabled(false);
				ReviewExit->SetActorTickEnabled(false);
				Name = TEXT("67-escape-car-left");
				break;
			}
			case 49:
			{
				const FVector Forward = FRotator(0, OverviewRotation.Yaw, 0).Vector();
				const FVector AboveFloor = OverviewLocation + Forward * 350;
				FCollisionQueryParams Query(SCENE_QUERY_STAT(UIReviewEscapeFloor), false, Body.Get());
				FHitResult Floor;
				if (!Test->TestTrue(TEXT("Escape car stands on an actual room surface"),
				    World->LineTraceSingleByChannel(Floor, AboveFloor, AboveFloor - FVector(0, 0, 4000), ECC_Visibility, Query)))
					return false;
				ReviewCar->SetActorLocationAndRotation(Floor.ImpactPoint + FVector(0, 0, 20),
				    FRotator(0, OverviewRotation.Yaw, 0), false, nullptr, ETeleportType::TeleportPhysics);
				if (!Test->TestTrue(TEXT("Toy boards through the live escape vehicle API"), ReviewCar->Board(Body.Get()))) return false;
				PC->SetControlRotation(FRotator(-8, OverviewRotation.Yaw, 0));
				ReviewExit->SetActorLocation(ReviewCar->GetActorLocation() +
				    PC->GetControlRotation().RotateVector(FVector(700, 3000, 0)));
				Name = TEXT("68-escape-gate-right");
				break;
			}
			case 50:
				ReviewExit->SetActorLocation(ReviewCar->GetActorLocation() +
				    FRotator(0, PC->GetControlRotation().Yaw, 0).RotateVector(FVector(900, 100, 55)));
				Name = TEXT("69-escape-gate-visible");
				break;
			default:
				return false;
			}
			return true;
		}

		FAutomationTestBase* Test;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ATBController> PC;
		TWeakObjectPtr<ATBCharacter> Body;
		TWeakObjectPtr<ATBBox> NavigationBox, OldNavigationBox;
		TWeakObjectPtr<ATBBoxEscapeCourse> NavigationCourse;
		TWeakObjectPtr<ATBDummyToy> NavigationDummy, OldCarriedDummy;
		TWeakObjectPtr<ATBMomArrivalZone> NavigationMom;
		TWeakObjectPtr<ATBCharacter> OldCarriedToy;
		TWeakObjectPtr<ATBEscapeVehicle> ReviewCar;
		TWeakObjectPtr<ATBEscapeExit> ReviewExit;
		TWeakObjectPtr<UCameraComponent> ReviewCamera;
		TWeakObjectPtr<UTBMovement> OverviewMovement;
		TWeakObjectPtr<AActor> OldViewTarget;
		TWeakObjectPtr<ATBGameMode> Mode;
		TWeakObjectPtr<ATBGameState> State;
		TWeakObjectPtr<ATBPlayerState> Player;
		TWeakObjectPtr<ATBHUD> HUD;
		TWeakObjectPtr<UTBTitleMenu> Title;
		TWeakObjectPtr<UButton> TitleVisualButton;
		TSharedPtr<SButton> TitleSlateButton;
		TArray<TStrongObjectPtr<UTexture2D>> ReviewTextures;
		FOnButtonClickedEvent OldTitleClicked;
		FOnButtonPressedEvent OldTitlePressed;
		FOnButtonReleasedEvent OldTitleReleased;
		EButtonPressMethod::Type OldTitlePressMethod = EButtonPressMethod::DownAndUp;
		bool bTitlePressed = false, bOldTitleHovered = false;
		TWeakObjectPtr<UTBOptionsMenu> Options;
		TWeakObjectPtr<UTBChatLog> SampleChat;
		TArray<FTBChatLine> OldChatLines;
		TArray<TWeakObjectPtr<ATBPlayerState>> Roster;
		FTBSettings OldSettings;
		TUniquePtr<TBTest::FPlayingScope> Playing;
		ETBTeam OldTeam = ETBTeam::None;
		ETBToyState OldToyState = ETBToyState::Free;
		ETBGazeState OldGazeState = ETBGazeState::None;
		ETBWinner OldWinner = ETBWinner::None;
		ETBResultStyle OldStyle = ETBResultStyle::None;
		FString OldReason, Name, Path, OldLanguage, OldSavedLanguage;
		int32 OldCollected = 0, Step = 0, Frames = 0;
		uint32 OldRound = 0;
		bool bTitle, bEntered = false, bRequested = false, bOldModeTick = false;
		bool bRosterOnly = false;
		bool bRandomOnly = false;
		bool bCompactHintsOnly = false;
		bool bOldSettings = false, bOldEscape = false, bOldReady = false, bOldVehicleResult = false;
		bool bOldGazeRequested = false, bOldCameraConstrained = false, bChangedGazeView = false;
		bool bOldFrozen = false;
		bool bNavigationActive = false;
		bool bStorageActive = false, bOldStorageTick = false, bOldHoldingInteract = false;
		bool bOldStorageRescuing = false;
		bool bOverviewActive = false, bOldMovementTick = false;
		EMovementMode OldMovementMode = MOVE_None;
		uint8 OldCustomMovementMode = 0;
		FTransform OldBodyTransform = FTransform::Identity;
		FVector OverviewLocation = FVector::ZeroVector, OldVelocity = FVector::ZeroVector;
		FRotator OverviewRotation = FRotator::ZeroRotator, OldControlRotation = FRotator::ZeroRotator;
		float OldCameraAspect = 0;
		float OldGazedSeconds = 0;
		double OldResultStart = 0, Deadline, ReadyAt = 0;
		double OldStoreStarted = -1;
		double OldVehicleExitStarted = -1;
	};

	bool CanReview(FAutomationTestBase& Test)
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("UIReviewScreenshots")))
		{
			Test.AddInfo(TEXT("Opt in with -UIReviewScreenshots and a rendered standalone -game process."));
			return false;
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("NullRHI")) || !GEngine ||
		    !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
		{
			Test.AddError(TEXT("UI review requires a rendered viewport; do not use -NullRHI."));
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBUIReviewGame, "ToonStory.UI.VisualReview.Game",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBUIReviewGame::RunTest(const FString&)
{
	if (!CanReview(*this)) return !HasAnyErrors();
	auto* World = ReviewWorld();
	auto* PC = World ? Cast<ATBController>(World->GetFirstPlayerController()) : nullptr;
	if (!PC || !PC->GetPlayerState<ATBPlayerState>() || !Cast<ATBHUD>(PC->GetHUD()) ||
	    !World->GetGameState<ATBGameState>() || World->GetGameState<ATBGameState>()->Phase != ETBPhase::Lobby)
	{
		AddError(TEXT("Run in a fresh standalone /Game/Maps/TB_Lobby -game process."));
		return false;
	}
	for (const TCHAR* File : {TEXT("zen-medium.ttf"), TEXT("yusei.ttf"), TEXT("mochiy.ttf"), TEXT("toy-ui-numerals.ttf"), TEXT("zen.ttf")})
	{
		TestTrue(*FString::Printf(TEXT("Font staged: %s"), File),
		    IFileManager::Get().FileSize(*(FPaths::ProjectContentDir() / TEXT("UI/PaperClub/Fonts") / File)) > 1024);
	}
	for (const TCHAR* Art : {TEXT("Icon_Battery"), TEXT("Icon_Hourglass"), TEXT("Tape_Cream"),
	    TEXT("StagePreview_Demo"), TEXT("StagePreview_House")})
	{
		TestNotNull(*FString::Printf(TEXT("Designer artwork loads: %s"), Art), TBPaper::Art(Art));
	}
	ADD_LATENT_AUTOMATION_COMMAND(FUIReview(this, World, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBUIReviewTitle, "ToonStory.UI.VisualReview.Title",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBUIReviewTitle::RunTest(const FString&)
{
	if (!CanReview(*this)) return !HasAnyErrors();
	auto* World = ReviewWorld();
	if (!World || !World->GetMapName().EndsWith(TEXT("Title")))
	{
		AddError(TEXT("Run in a fresh standalone /Game/Maps/Title -game process."));
		return false;
	}
	const auto* Online = Online::GetSubsystem(World);
	if (!Online || Online->GetSubsystemName() != FName(TEXT("NULL")))
	{
		AddError(TEXT("Room-finder review requires LAN/Null, never a public Steam session."));
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FUIReview(this, World, true));
	return true;
}
#endif
