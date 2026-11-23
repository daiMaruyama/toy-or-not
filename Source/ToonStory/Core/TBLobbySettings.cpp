#include "TBLobbySettings.h"
#include "TBController.h"
#include "TBSession.h"
#include "TBGameMode.h"
#include "TBGameState.h"
#include "TBPlayerState.h"
#include "UI/TBUIText.h"
#include "Data/TBRuleData.h"
#include "UI/Loading/TBLoadingScreenSettings.h"
#include "UI/Loading/TBLoadingScreenSubsystem.h"
#include "GamePlay/TBPickup.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"

namespace
{
	struct FStageDefinition
	{
		int32 Id;
		const TCHAR* Path;
		const TCHAR* NameKey;
		int32 ItemCount;
	};
	// ロビーでの表示・上限。実際のアイテム数はステージ到着時に再確認する。
	// IDs survive saved selections; retired IDs and invalid values fall back to the retained Demo.
	constexpr FStageDefinition Stages[] = {{TBLobbySettings::DemoStageId, TEXT("/Game/Maps/TB_ToyWorldDemoExpanded"), TEXT("Stage.Demo"), 10},
	                                       {TBLobbySettings::HouseStageId, TEXT("/Game/Maps/TB_StylizedHouse"), TEXT("Stage.House"), 8}};
	const FStageDefinition* FindStage(int32 Index)
	{
		for (const auto& Stage : Stages) if (Stage.Id == Index) return &Stage;
		return &Stages[0];
	}
	int32 NormalizeSelection(int32 Index)
	{
		return Index == TBLobbySettings::RandomStageId ? Index : FindStage(Index)->Id;
	}
} // namespace

void UTBLobbySettings::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	MapLoadedHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddWeakLambda(
	    this,
	    [this](UWorld* World)
	    {
		    if (World && World->GetGameInstance() == GetGameInstance() && World->GetMapName().Contains(TEXT("Title")))
		    {
			    PendingMap.Reset();
			    bStartOnArrival = false;
			    bArrivalCancelled = false;
			    bStageSelected = false;
			    ArrivalDeadline = 0;
			    StageRevealAt = 0;
			    bDedicatedLobby = false;
			    SelectedStage = 1;
			    ResolvedStage = INDEX_NONE;
		    }
	    });
}
void UTBLobbySettings::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(MapLoadedHandle);
	Super::Deinitialize();
}

const TCHAR* TBLobbySettings::StagePath(int32 Index)
{
	return Index == RandomStageId ? nullptr : FindStage(Index)->Path;
}
bool TBLobbySettings::IsStageAvailable(int32 Index)
{
	if (Index != RandomStageId) return FPackageName::DoesPackageExist(StagePath(Index));
	for (const auto& Stage : Stages)
		if (!FPackageName::DoesPackageExist(Stage.Path)) return false;
	return true;
}
void TBLobbySettings::ResolveStageForStart(UWorld* World)
{
	if (!World || !World->GetAuthGameMode<ATBGameMode>()) return;
	auto* State = World->GetGameState<ATBGameState>();
	auto* Pending = World->GetGameInstance()->GetSubsystem<UTBLobbySettings>();
	if (!State || State->Phase != ETBPhase::Lobby || State->bTravelPending ||
	    State->SelectedStage != RandomStageId || Pending->ResolvedStage != INDEX_NONE) return;
	Pending->ResolvedStage = Stages[FMath::RandRange(0, int32(UE_ARRAY_COUNT(Stages)) - 1)].Id;
	State->StageName = StageDisplayName(Pending->ResolvedStage);
	State->ForceNetUpdate();
}
void TBLobbySettings::ResetStageResolution(UWorld* World)
{
	if (!World || !World->GetGameInstance()) return;
	World->GetGameInstance()->GetSubsystem<UTBLobbySettings>()->ResolvedStage = INDEX_NONE;
	if (auto* State = World->GetGameState<ATBGameState>(); State && State->SelectedStage == RandomStageId)
	{
		State->StageName = StageDisplayName(RandomStageId);
		State->ForceNetUpdate();
	}
}
FString TBLobbySettings::StageDisplayName(int32 Index)
{
	return TBText::String(Index == RandomStageId ? TEXT("Stage.Random") : FindStage(Index)->NameKey);
}
int32 TBLobbySettings::StageIndex(const UWorld* World)
{
	for (const auto& Stage : Stages)
	{
		if (World && World->GetMapName().EndsWith(FPackageName::GetShortName(Stage.Path)))
		{
			return Stage.Id;
		}
	}
	return INDEX_NONE;
}
int32 TBLobbySettings::StageItemCount(int32 Index)
{
	if (Index != RandomStageId) return FindStage(Index)->ItemCount;
	int32 Count = MAX_int32;
	for (const auto& Stage : Stages) Count = FMath::Min(Count, Stage.ItemCount);
	return Count;
}
int32 TBLobbySettings::AvailableItems(UWorld* World)
{
	// Authored pickup counts for the retained Demo and Stylized House. Revalidated on arrival
	// so a later map edit cannot produce an impossible collection target.
	if (ShouldTravelToStage(World))
	{
		const auto* State = World->GetGameState<ATBGameState>();
		return State ? StageItemCount(State->SelectedStage) : 1;
	}
	int32 Count = 0;
	for (TActorIterator<ATBPickup> It(World); It; ++It)
	{
		if (!It->bTaken)
		{
			++Count;
		}
	}
	return Count;
}
void TBLobbySettings::SelectStage(ATBController* Host, int32 Index)
{
	if (!Host)
	{
		return;
	}
	auto* World = Host->GetWorld();
	auto* Mode = World->GetAuthGameMode<ATBGameMode>();
	auto* State = World->GetGameState<ATBGameState>();
	Index = NormalizeSelection(Index);
	auto* Pending = World->GetGameInstance()->GetSubsystem<UTBLobbySettings>();
	if (!Mode || !Mode->IsHost(Host) || !State || State->Phase != ETBPhase::Lobby || State->bTravelPending ||
	    !Pending->PendingMap.IsEmpty())
	{
		return;
	}
	if (!IsStageAvailable(Index))
	{
		return;
	}
	Pending->bStageSelected = true;
	Pending->ResolvedStage = INDEX_NONE;
	Pending->SelectedStage = Index;
	State->SelectedStage = Index;
	State->Settings.RequiredItems = FMath::Min(State->Settings.RequiredItems, AvailableItems(World));
	State->StageName = StageDisplayName(Index);
	State->ForceNetUpdate();
}
bool TBLobbySettings::IsLobbyMapName(const FString& MapName)
{
	return MapName.EndsWith(TEXT("TB_Lobby"));
}
bool TBLobbySettings::IsLobby(const UWorld* World)
{
	return World && IsLobbyMapName(World->GetMapName());
}
bool TBLobbySettings::ShouldTravelToStage(const UWorld* World)
{
	if (!World || !World->GetGameInstance()) return false;
	const auto* Pending = World->GetGameInstance()->GetSubsystem<UTBLobbySettings>();
	const auto* State = World->GetGameState<ATBGameState>();
	const int32 Destination = State && State->SelectedStage == RandomStageId
	    ? Pending->ResolvedStage : State ? State->SelectedStage : INDEX_NONE;
	return IsLobby(World) || (State && Pending->bStageSelected &&
	    (Destination == INDEX_NONE || StageIndex(World) != Destination));
}

void TBLobbySettings::CancelTravel(UWorld* World)
{
	if (!World || !World->GetGameInstance()) return;
	auto* Pending = World->GetGameInstance()->GetSubsystem<UTBLobbySettings>();
	Pending->PendingMap.Reset();
	ResetStageResolution(World);
	Pending->bStartOnArrival = false;
	Pending->bArrivalCancelled = false;
	Pending->ArrivalDeadline = 0;
	Pending->StageRevealAt = 0;
	if (auto* State = World->GetGameState<ATBGameState>())
	{
		State->bTravelPending = false;
		for (APlayerState* Player : State->PlayerArray)
		{
			if (auto* Info = Cast<ATBPlayerState>(Player)) Info->bReady = false;
		}
		State->ForceNetUpdate();
	}
	if (auto* Mode = World->GetAuthGameMode<ATBGameMode>()) Mode->SyncLobbyCounts();
	if (auto* Session = World->GetGameInstance()->GetSubsystem<UTBSession>()) Session->RefreshLobbyAvailability();
}

FString TBLobbySettings::TryTravel(ATBGameMode* Mode, bool StartMatch)
{
	auto* World = Mode->GetWorld();
	auto* State = World->GetGameState<ATBGameState>();
	auto* Pending = World->GetGameInstance()->GetSubsystem<UTBLobbySettings>();
	if (!State || State->bTravelPending || !Pending->PendingMap.IsEmpty())
	{
		return TEXT("別のマップ移動が処理中です。");
	}
	if (StartMatch) ResolveStageForStart(World);
	const int32 DestinationStage = State->SelectedStage == RandomStageId ? Pending->ResolvedStage : State->SelectedStage;
	if (StartMatch && DestinationStage == INDEX_NONE) return TEXT("移動先がまだ決まっていません。");
	const TCHAR* Destination = StartMatch ? StagePath(DestinationStage) : TEXT("/Game/Maps/TB_Lobby");
	if (!Destination)
	{
		return TEXT("移動先が設定されていません。");
	}
	const FString Path(Destination);
	if (!FPackageName::DoesPackageExist(Path))
	{
		if (StartMatch) ResetStageResolution(World);
		return TEXT("移動先のマップが見つかりません。");
	}
	State->SelectedStage = NormalizeSelection(State->SelectedStage);
	if (StartMatch) State->StageName = StageDisplayName(DestinationStage);
	Pending->PendingRules = State->Settings;
	Pending->SelectedStage = State->SelectedStage;
	Pending->ExpectedPlayers = State->PlayerArray.Num();
	Pending->PendingMap = Path;
	Pending->bStartOnArrival = StartMatch;
	Pending->ArrivalDeadline = 0;
	Pending->StageRevealAt = 0;
	Pending->bArrivalCancelled = false;
	State->bTravelPending = true;
	Mode->bUseSeamlessTravel = true;
	auto StartTravel = FSimpleDelegate::CreateWeakLambda(Mode, [World, Path, Pending, State]()
	{
		if (Pending->PendingMap == Path && State->bTravelPending && !World->ServerTravel(Path))
			GEngine->BroadcastTravelFailure(World, ETravelFailure::ServerTravelFailure, TEXT("Server travel could not start."));
	});
	State->ForceNetUpdate();
	if (!UTBLoadingScreenSubsystem::DeferForCurtain(World, Path, StartTravel) && !World->ServerTravel(Path))
	{
		State->bTravelPending = false;
		Pending->PendingMap.Reset();
		ResetStageResolution(World);
		Pending->bStartOnArrival = false;
		return TEXT("サーバーがマップ移動を開始できませんでした。");
	}
	if (auto* Session = World->GetGameInstance()->GetSubsystem<UTBSession>())
	{
		Session->RefreshLobbyAvailability();
	}
	return {};
}
void TBLobbySettings::TickArrival(ATBGameMode* Mode)
{
	auto* World = Mode->GetWorld();
	auto* Pending = World->GetGameInstance()->GetSubsystem<UTBLobbySettings>();
	if (!Pending->bStartOnArrival || !Pending->PendingMap.IsEmpty() || World->IsInSeamlessTravel())
	{
		return;
	}
	auto* State = World->GetGameState<ATBGameState>();
	if (!State)
	{
		return;
	}
	bool Ready = State->PlayerArray.Num() == Pending->ExpectedPlayers;
	for (APlayerState* Player : State->PlayerArray)
	{
		auto* Info = Cast<ATBPlayerState>(Player);
		auto* Controller = Info ? Cast<APlayerController>(Info->GetOwner()) : nullptr;
		Ready &= Info && Info->bReady && Info->GetPawn() && Controller && Controller->HasClientLoadedCurrentWorld();
	}
	if (Pending->ArrivalDeadline == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("[ToyBoxArrival] Observe map=%s phase=%d players=%d expected=%d ready=%d cancelled=%d seamless=%d streamLevels=%d reason=%s"),
		    *World->GetMapName(), int32(State->Phase), State->PlayerArray.Num(), Pending->ExpectedPlayers,
		    Ready, Pending->bArrivalCancelled, World->IsInSeamlessTravel(), World->GetStreamingLevels().Num(), *State->Reason);
		Pending->ArrivalDeadline = FPlatformTime::Seconds() + 60;
	}
	Ready &= !Pending->bArrivalCancelled;
	if (Ready)
	{
		// Start the presentation clock only after the last client has loaded. Even a stalled
		// editor or a slow disk gets a few real seconds of film before the round timer starts.
		const double Now = FPlatformTime::Seconds();
		const auto* Loading = GetDefault<UTBLoadingScreenSettings>();
		if (Pending->StageRevealAt == 0.)
			Pending->StageRevealAt = Now + (Loading->bEnabled ? Loading->MinimumTravelSeconds : 0.f);
		if (Now < Pending->StageRevealAt) return;
	}
	else
	{
		Pending->StageRevealAt = 0;
	}
	if (!Ready && !Pending->bArrivalCancelled && FPlatformTime::Seconds() < Pending->ArrivalDeadline)
	{
		return;
	}
	Pending->bStartOnArrival = false;
	State->bTravelPending = false;
	UE_LOG(LogTemp, Display, TEXT("[ToyBoxArrival] Attempt map=%s phase=%d players=%d expected=%d ready=%d cancelled=%d deadlineExpired=%d reason=%s"),
	    *World->GetMapName(), int32(State->Phase), State->PlayerArray.Num(), Pending->ExpectedPlayers,
	    Ready, Pending->bArrivalCancelled, FPlatformTime::Seconds() >= Pending->ArrivalDeadline, *State->Reason);
	for (const auto* Stream : World->GetStreamingLevels())
		if (Stream) UE_LOG(LogTemp, Display, TEXT("[ToyBoxArrival] Stream package=%s loaded=%d visible=%d"),
		    *Stream->GetWorldAssetPackageName(), Stream->IsLevelLoaded(), Stream->IsLevelVisible());
	if (Ready)
	{
		for (auto It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (Mode->IsHost(It->Get()))
			{
				UE_LOG(LogTemp, Display, TEXT("[ToyBoxArrival] Before Start map=%s phase=%d players=%d reason=%s"),
				    *World->GetMapName(), int32(State->Phase), State->PlayerArray.Num(), *State->Reason);
				const auto* Loading = GetDefault<UTBLoadingScreenSettings>();
				Mode->StartRound(It->Get(), Loading->bEnabled ? Loading->FadeOutSeconds : 0.f);
				const auto* PC = Cast<ATBController>(It->Get());
				UE_LOG(LogTemp, Display, TEXT("[ToyBoxArrival] After Start map=%s phase=%d players=%d round=%u preparing=%d reason=%s notice=%s"),
				    *World->GetMapName(), int32(State->Phase), State->PlayerArray.Num(), State->HidePreparation.Round,
				    State->IsPreparing(), *State->Reason, PC ? *PC->Notice : TEXT(""));
				break;
			}
		}
	}
	if (State->Phase != ETBPhase::Playing)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ToyBoxArrival] Lobby fallback map=%s phase=%d players=%d ready=%d cancelled=%d reason=%s"),
		    *World->GetMapName(), int32(State->Phase), State->PlayerArray.Num(), Ready, Pending->bArrivalCancelled, *State->Reason);
		if (!Travel(Mode, false))
		{
			CancelTravel(World);
		}
	}
}

void TBLobbySettings::Restore(ATBGameMode* Mode)
{
	auto* World = Mode->GetWorld();
	auto* State = World->GetGameState<ATBGameState>();
	if (!State)
	{
		return;
	}
	auto* Pending = World->GetGameInstance()->GetSubsystem<UTBLobbySettings>();
	if (IsLobby(World))
	{
		Pending->bDedicatedLobby = true;
		Pending->ResolvedStage = INDEX_NONE;
	}
	const int32 CurrentStage = StageIndex(World);
	if (!Pending->bDedicatedLobby && !Pending->bStageSelected && Pending->PendingMap.IsEmpty() && CurrentStage != INDEX_NONE)
	{
		Pending->SelectedStage = CurrentStage;
	}
	Pending->SelectedStage = NormalizeSelection(Pending->SelectedStage);
	State->SelectedStage = Pending->SelectedStage;
	// Arenaなどの直接起動時は、GameModeが設定したマップ名を維持する。
	if (Pending->bDedicatedLobby || CurrentStage != INDEX_NONE)
	{
		State->StageName = StageDisplayName(State->SelectedStage == RandomStageId && CurrentStage != INDEX_NONE
		    ? CurrentStage : State->SelectedStage);
	}
	if (!Pending->PendingMap.IsEmpty() && World->GetMapName().EndsWith(FPackageName::GetShortName(Pending->PendingMap)))
	{
		State->Settings = Pending->PendingRules;
		State->Settings.RequiredItems =
		    FMath::Clamp(State->Settings.RequiredItems, 1, FMath::Max(1, AvailableItems(World)));
		Pending->PendingMap.Reset();
		State->bTravelPending = Pending->bStartOnArrival;
		State->ForceNetUpdate();
		if (auto* Session = World->GetGameInstance()->GetSubsystem<UTBSession>())
		{
			Session->RefreshLobbyAvailability();
		}
	}
}
bool TBLobbySettings::HandleClick(ATBController* Controller, FName Name)
{
	auto* State = Controller->GetWorld()->GetGameState<ATBGameState>();
	if (!State || State->Phase != ETBPhase::Lobby)
	{
		return false;
	}
	auto Rules = State->Settings;
	const auto& Match = TB::Rules().Match;
	if (Name == TEXT("FewerNPCHumans") || Name == TEXT("MoreNPCHumans"))
	{
		Rules.NPCHumans += Name == TEXT("MoreNPCHumans") ? 1 : -1;
	}
	else if (Name == TEXT("FewerNPCToys") || Name == TEXT("MoreNPCToys"))
	{
		Rules.NPCToys += Name == TEXT("MoreNPCToys") ? 1 : -1;
	}
	else if (Name == TEXT("LessTime"))
	{
		Rules.Duration = FMath::Max(Match.MinDuration, Rules.Duration - Match.DurationStep);
	}
	else if (Name == TEXT("MoreTime"))
	{
		Rules.Duration = FMath::Min(Match.MaxDuration, Rules.Duration + Match.DurationStep);
	}
	else if (Name == TEXT("FewerBatteries"))
	{
		Rules.RequiredItems = FMath::Max(1, Rules.RequiredItems - 1);
	}
	else if (Name == TEXT("MoreBatteries"))
	{
		Rules.RequiredItems = FMath::Min(AvailableItems(Controller->GetWorld()), Rules.RequiredItems + 1);
	}
	else
	{
		return false;
	}
	Controller->ServerRules(Rules);
	return true;
}
void ATBController::ServerSelectStage_Implementation(int32 Index)
{
	TBLobbySettings::SelectStage(this, Index);
}
void ATBController::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();
	LobbyCamera.Reset();
	bLobbyCameraSearched = false;
	bLobbyCameraActive = false;
}
