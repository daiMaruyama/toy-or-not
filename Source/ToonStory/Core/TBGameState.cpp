// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "TBGameState.h"
#include "Audio/TBAudioEvents.h"
#include "Components/TextRenderComponent.h"
#include "Core/TBLobbySettings.h"
#include "Data/TBPresentationData.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/PlayerState.h"
#include "GamePlay/TBBox.h"
#include "GamePlay/TBPushableProps.h"
#include "Net/UnrealNetwork.h"

ATBGameState::ATBGameState()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
}

void ATBGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	for (auto It = PropPeakHeights.CreateIterator(); It; ++It)
	{
		UPrimitiveComponent* Part = It.Key().Get();
		if (!Part) It.RemoveCurrent();
		else if (Part->IsAnyRigidBodyAwake())
			It.Value() = FMath::Max(It.Value(), Part->GetCenterOfMass().Z);
	}
	if (PropPeakHeights.IsEmpty()) SetActorTickEnabled(false);
}

void ATBGameState::OnPropHit(UPrimitiveComponent* Part, AActor* Other, UPrimitiveComponent* OtherPart,
                             FVector Impulse, const FHitResult& Hit)
{
	// Only a surface supporting this prop ends the fall. Brushing a wall preserves its apex.
	if (Hit.ImpactNormal.Z < 0.45) return;
	double* Peak = PropPeakHeights.Find(Part);
	if (!Peak) return;
	const double Height = Part->GetCenterOfMass().Z;
	const double Fall = *Peak - Height;
	*Peak = Height;
	if (const auto Sound = UTBAudioEvents::PropDropSound(Fall, UTBAudioEvents::IsGlassProp(Cast<UStaticMeshComponent>(Part))))
		UTBAudioEvents::Emit(this, *Sound, nullptr, Hit.ImpactPoint);
}

void ATBGameState::BeginPlay()
{
	Super::BeginPlay();
	if (TBLobbySettings::IsLobby(GetWorld()))
	{
		// 共有マップに残っている旧ロゴを、ホストと参加者それぞれの画面で隠す。
		for (AActor* Actor : GetWorld()->PersistentLevel->Actors)
		{
			auto* Label = Cast<ATextRenderActor>(Actor);
			if (!Label || Label->GetFName() != FName(TEXT("TextRenderActor_0"))) continue;
			auto* Text = Label->GetTextRender();
			if (Text && Text->Text.ToString() == TEXT("TOON STORY")) Text->SetVisibility(false);
			break;
		}
	}
	for (ULevel* Level : GetWorld()->GetLevels()) ConfigurePushableProps(Level, GetWorld());
	FWorldDelegates::LevelAddedToWorld.AddUObject(this, &ATBGameState::ConfigurePushableProps);
}

void ATBGameState::EndPlay(const EEndPlayReason::Type EndReason)
{
	FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
	Super::EndPlay(EndReason);
}

void ATBGameState::ConfigurePushableProps(ULevel* Level, UWorld* World)
{
	if (!Level || World != GetWorld()) return;
	for (AActor* Actor : Level->Actors)
	{
		auto* Prop = Cast<AStaticMeshActor>(Actor);
		if (!Prop) continue;
		TBPushableProps::TryEnable(*Prop);
		auto* Part = Prop->GetStaticMeshComponent();
		if (!Part || !Part->ComponentHasTag(TEXT("TBPushable"))) continue;
		// World geometry still supports the prop. A character cannot use it as a moving floor.
		Part->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Part->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
		Part->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		Part->CanCharacterStepUpOn = ECB_No;
		if (HasAuthority() && Part->IsSimulatingPhysics() && !PropPeakHeights.Contains(Part))
		{
			Prop->SetReplicates(true);
			Prop->SetReplicateMovement(true);
			PropPeakHeights.Add(Part, Part->GetCenterOfMass().Z);
			Part->SetNotifyRigidBodyCollision(true);
			Part->OnComponentHit.AddUniqueDynamic(this, &ATBGameState::OnPropHit);
			SetActorTickEnabled(true);
		}
	}
}

void ATBGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATBGameState, Settings);
	DOREPLIFETIME(ATBGameState, StageName);
	DOREPLIFETIME(ATBGameState, SelectedStage);
	DOREPLIFETIME(ATBGameState, bTravelPending);
	DOREPLIFETIME(ATBGameState, PlayerLimit);
	DOREPLIFETIME(ATBGameState, Phase);
	DOREPLIFETIME(ATBGameState, Winner);
	DOREPLIFETIME(ATBGameState, Reason);
	DOREPLIFETIME(ATBGameState, EndTime);
	DOREPLIFETIME(ATBGameState, LobbyReturnError);
	DOREPLIFETIME(ATBGameState, Collected);
	DOREPLIFETIME(ATBGameState, bEscapePhase);
	DOREPLIFETIME(ATBGameState, bVehicleEscapeResult);
	DOREPLIFETIME(ATBGameState, EscapedPlayers);
	DOREPLIFETIME(ATBGameState, ResultStyle);
	DOREPLIFETIME(ATBGameState, ResultStartTime);
	DOREPLIFETIME(ATBGameState, ResultShot);
	DOREPLIFETIME(ATBGameState, BoxedCount);
	DOREPLIFETIME(ATBGameState, Box);
	DOREPLIFETIME(ATBGameState, HidePreparation);
}

float ATBGameState::HideProgress() const
{
	const double Duration = HidePreparation.EndsAt - HidePreparation.StartsAt;
	return HidePreparation.bValid && Duration > 0
	    ? FMath::Clamp((GetServerWorldTimeSeconds() - HidePreparation.StartsAt) / Duration, 0.0, 1.0) : 0.f;
}

bool ATBGameState::EscapedInVehicle(const APlayerState* Player) const
{
	return Player && EscapedPlayers.Contains(Player);
}

double ATBGameState::ResultElapsed() const
{
	return Phase == ETBPhase::Results ? FMath::Max(0.0, GetServerWorldTimeSeconds() - ResultStartTime) : 0.0;
}

bool ATBGameState::IsResultRevealed() const
{
	return Phase == ETBPhase::Results &&
	    (ResultStyle == ETBResultStyle::None || ResultElapsed() >= TB::PresentationData().Result.CoverSeconds);
}

bool ATBGameState::IsPreparing() const
{
	return Phase == ETBPhase::Playing && (!HidePreparation.bValid || HidePreparation.bHiding);
}

float ATBGameState::HideRemaining() const
{
	return IsPreparing() && HidePreparation.bValid
	    ? FMath::Max(0., HidePreparation.EndsAt - GetServerWorldTimeSeconds()) : 0.f;
}

float ATBGameState::Remaining() const
{
	if (IsPreparing()) return Settings.Duration;
	return Phase == ETBPhase::Playing ? FMath::Max(0., EndTime - GetServerWorldTimeSeconds()) : 0.f;
}
