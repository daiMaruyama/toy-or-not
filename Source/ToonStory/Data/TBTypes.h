// Copyright (c) 2026 Mamodou. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "TBTypes.generated.h"

UENUM(BlueprintType)
enum class ETBTeam : uint8
{
	None,
	Human,
	Toy
};
UENUM(BlueprintType)
enum class ETBToyState : uint8
{
	Free,
	Grabbed,
	Carried,
	Storing,
	Boxed
};
// おもちゃの見られ方。並びはTBRuleMath::EGazeStateと同じ。
UENUM(BlueprintType)
enum class ETBGazeState : uint8
{
	None,
	Seen,
	Gazed,
	Frozen
};
UENUM(BlueprintType)
enum class ETBPhase : uint8
{
	Lobby,
	Playing,
	Results,
	Aborted
};
UENUM(BlueprintType)
enum class ETBWinner : uint8
{
	None,
	Humans,
	Toys
};
// リザルトへの切り替え演出。車で脱出、箱のふたが閉まる、ママの吹き出しで覆う。
UENUM(BlueprintType)
enum class ETBResultStyle : uint8
{
	None,
	Car,
	Lid,
	Mom
};
UENUM(BlueprintType)
enum class ETBRemoteKey : uint8
{
	Digit,
	Next,
	Prev,
	Power
};

USTRUCT(BlueprintType)
struct FTBSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Humans = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Toys = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Duration = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 NPCHumans = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 NPCToys = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 RequiredItems = 5;
};
