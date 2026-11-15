#include "GamePlay/Items/TBEscapeExit.h"
#include "GamePlay/Items/TBEscapeVehicle.h"
#include "Character/TBCharacter.h"
#include "Core/TBGameHelpers.h"
#include "Core/TBGameMode.h"
#include "Core/TBGameState.h"
#include "Core/TBPlayerState.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

ATBEscapeExit::ATBEscapeExit()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	ExitArea = CreateDefaultSubobject<UBoxComponent>(TEXT("ExitArea"));
	SetRootComponent(ExitArea);
	ExitArea->SetBoxExtent(FVector(100, 100, 100));
	ExitArea->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ExitArea->SetCollisionResponseToAllChannels(ECR_Overlap);
	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(ExitArea);
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetGenerateOverlapEvents(false);
	Marker->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Gate(TEXT("/Game/ToyWorld/Meshes/SM_Central_Gates.SM_Central_Gates"));
	if (Gate.Succeeded())
	{
		Marker->SetStaticMesh(Gate.Object);
	}
	RefreshAppearance();
	// 既定は門を抜けて少し先の脇、車の高さ。マップごとにレベルで動かせる。
	const float Floor = -ExitArea->GetUnscaledBoxExtent().Z;
	ResultCamera = CreateDefaultSubobject<USceneComponent>(TEXT("ResultCamera"));
	ResultCamera->SetupAttachment(ExitArea);
	const FVector CameraAt(250, 70, Floor + 35);
	ResultCamera->SetRelativeLocation(CameraAt);
	ResultCamera->SetRelativeRotation((FVector(-150, 0, Floor + 20) - CameraAt).Rotation());
	Lineup = CreateDefaultSubobject<USceneComponent>(TEXT("Lineup"));
	Lineup->SetupAttachment(ExitArea);
	Lineup->SetRelativeLocation(FVector(-250, 0, Floor));
	Lineup->SetRelativeRotation(FRotator(0, 180, 0));
}

void ATBEscapeExit::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshAppearance();
}

void ATBEscapeExit::BeginPlay()
{
	Super::BeginPlay();
	RefreshAppearance();
}

void ATBEscapeExit::RefreshAppearance()
{
	if (const UStaticMesh* Gate = Marker->GetStaticMesh())
	{
		// 門の通過方向を +X に向け、出口範囲の幅と底面に揃える。
		const FBox Bounds = Gate->GetBoundingBox();
		const FVector Extent = ExitArea->GetUnscaledBoxExtent();
		const float Scale = Extent.Y * 2.f / FMath::Max(1.f, float(Bounds.GetSize().X));
		const FRotator Rotation(0, 90, 0);
		const FVector Center = Rotation.RotateVector(Bounds.GetCenter() * Scale);
		Marker->SetRelativeRotation(Rotation);
		Marker->SetRelativeScale3D(FVector(Scale));
		Marker->SetRelativeLocation(FVector(-Center.X, -Center.Y, -Extent.Z - Bounds.Min.Z * Scale));
	}
}

FVector ATBEscapeExit::ResultPassPoint() const
{
	// カメラの横を抜けきったところ。門の中心線の上、床の高さ。
	const FTransform Gate = ExitArea->GetComponentTransform();
	const FVector Local = Gate.InverseTransformPosition(ResultCamera->GetComponentLocation());
	return Gate.TransformPosition(FVector(Local.X + 120, 0, -ExitArea->GetUnscaledBoxExtent().Z));
}

FTransform ATBEscapeExit::LineupSpot(int32 Index, int32 Count) const
{
	const FTransform Center(Lineup->GetComponentRotation(), Lineup->GetComponentLocation());
	const float Offset = (Index - (FMath::Max(Count, 1) - 1) * .5f) * 80.f;
	return FTransform(Center.GetRotation(), Center.TransformPosition(FVector(0, Offset, 0)));
}

FTransform ATBEscapeExit::ParkSpot() const
{
	// 列の後ろ（門の側）に、門へ向けて止める。
	const FTransform Center(Lineup->GetComponentRotation(), Lineup->GetComponentLocation());
	return FTransform(ExitArea->GetComponentQuat(), Center.TransformPosition(FVector(-160, 0, 0)));
}

void ATBEscapeExit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) return;
	auto* Match = TB::GS(GetWorld());
	if (!Match || !Match->bEscapePhase || Match->Phase != ETBPhase::Playing || Match->Remaining() <= 0.f) return;
	for (TActorIterator<ATBEscapeVehicle> It(GetWorld()); It; ++It)
	{
		if (It->Passengers.IsEmpty() || !ExitArea->IsOverlappingActor(*It)) continue;
		for (ATBCharacter* Toy : It->Passengers)
			if (IsValid(Toy) && Toy->TBPS()) Match->EscapedPlayers.AddUnique(Toy->TBPS());
		Match->bVehicleEscapeResult = true;
		if (auto* Mode = GetWorld()->GetAuthGameMode<ATBGameMode>()) Mode->Finish(ETBWinner::Toys, TEXT("乗り物で脱出！"));
		return;
	}
}
