// Copyright (c) 2026 Mamodou. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Data/TBTypes.h"
#include "TBGameState.generated.h"

class ATBBox;
class ATBEscapeVehicle;

USTRUCT()
struct FTBEscapeIgnition
{
	GENERATED_BODY()
	static constexpr double Duration = 111.0 / 24.0;
	UPROPERTY()
	double StartsAt = -1;
	UPROPERTY()
	TObjectPtr<ATBEscapeVehicle> Vehicle = nullptr;
};

// Replicate validity, authority decision and deadline together; zero on a client is not permission to reveal.
USTRUCT()
struct FTBHidePreparation
{
	GENERATED_BODY()
	UPROPERTY()
	bool bValid = false;
	UPROPERTY()
	bool bHiding = false;
	UPROPERTY()
	uint32 Round = 0;
	UPROPERTY()
	double StartsAt = 0;
	UPROPERTY()
	double EndsAt = 0;
};

/** 試合設定・残り時間・勝敗など、全員が参照する情報を持つ。 */
UCLASS()
class TOONSTORY_API ATBGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	ATBGameState();
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndReason) override;
private:
	void ConfigureStageActors(ULevel* Level, UWorld* World);
	TMap<TWeakObjectPtr<UPrimitiveComponent>, double> PropPeakHeights;
	UFUNCTION()
	void OnPropHit(UPrimitiveComponent* Part, AActor* Other, UPrimitiveComponent* OtherPart,
	               FVector Impulse, const FHitResult& Hit);
public:
	UPROPERTY(Replicated)
	FTBHidePreparation HidePreparation;
	bool IsPreparing() const;
	float HideRemaining() const;
	float HideProgress() const;
	UPROPERTY(Replicated, BlueprintReadOnly)
	FString StageName;
	UPROPERTY(Replicated, BlueprintReadOnly)
	int32 PlayerLimit = 0;
	UPROPERTY(Replicated, BlueprintReadOnly)
	int32 SelectedStage = 1;
	UPROPERTY(ReplicatedUsing=OnRep_TravelPending, BlueprintReadOnly)
	bool bTravelPending = false;
	UFUNCTION()
	void OnRep_TravelPending();
	UPROPERTY(Replicated, BlueprintReadOnly)
	FTBSettings Settings;
	UPROPERTY(Replicated, BlueprintReadOnly)
	ETBPhase Phase = ETBPhase::Lobby;
	UPROPERTY(Replicated, BlueprintReadOnly)
	ETBWinner Winner = ETBWinner::None;
	UPROPERTY(Replicated, BlueprintReadOnly)
	FString Reason;
	UPROPERTY(Replicated, BlueprintReadOnly)
	double EndTime = 0;
	UPROPERTY(Replicated)
	FString LobbyReturnError;
	bool IsLobbyReturnStopped() const { return !bTravelPending && !LobbyReturnError.IsEmpty() && EndTime == 0; }
	UPROPERTY(Replicated, BlueprintReadOnly)
	int32 Collected = 0;
	UPROPERTY(Replicated, BlueprintReadOnly)
	bool bEscapePhase = false;
	UPROPERTY(ReplicatedUsing=OnRep_Ignition)
	FTBEscapeIgnition Ignition;
	UFUNCTION()
	void OnRep_Ignition();
private:
	double IgnitionReceivedAt = -1;
public:
	bool IsIgniting() const { return Phase == ETBPhase::Playing && !bEscapePhase && Ignition.StartsAt >= 0; }
	double IgnitionElapsed() const;
	UPROPERTY(Replicated, BlueprintReadOnly)
	bool bVehicleEscapeResult = false;
	// 車で脱出できた人。勝敗と同じ GameState で送り、結果の画面や音と同時に届くようにする。
	UPROPERTY(Replicated, BlueprintReadOnly)
	TArray<TObjectPtr<APlayerState>> EscapedPlayers;
	bool EscapedInVehicle(const APlayerState* Player) const;
	// リザルトへの切り替え演出。決着の時刻から各PCが同じ流れを再生する。
	UPROPERTY(Replicated, BlueprintReadOnly)
	ETBResultStyle ResultStyle = ETBResultStyle::None;
	UPROPERTY(Replicated, BlueprintReadOnly)
	double ResultStartTime = 0;
	// 車とふたの演出で映すカメラの位置。サーバーが決めて送る。
	UPROPERTY(Replicated, BlueprintReadOnly)
	FTransform ResultShot;
	/** 決着からの秒。リザルト以外は0 */
	double ResultElapsed() const;
	/** 演出が終わり、結果の画面を出してよい */
	bool IsResultRevealed() const;
	UPROPERTY(Replicated, BlueprintReadOnly)
	int32 BoxedCount = 0;
	UPROPERTY(Replicated, BlueprintReadOnly)
	TObjectPtr<ATBBox> Box;
	UFUNCTION(BlueprintPure)
	float Remaining() const;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
