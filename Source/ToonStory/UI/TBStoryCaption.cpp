// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "UI/TBStoryCaption.h"
#include "Animation/WidgetAnimation.h"
#include "Components/TextBlock.h"
#include "EngineUtils.h"
#include "UI/TBUIText.h"

ATBStoryCaptionCue::ATBStoryCaptionCue()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void UTBStoryCaption::NativeConstruct()
{
	Super::NativeConstruct();
	SetSkipHintShown(false);
	Refresh();
}

void UTBStoryCaption::SetSkipHintShown(bool bShown)
{
	SkipHint->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!SkipHintFadeIn)
	{
		return;
	}
	if (bShown)
	{
		PlayAnimationForward(SkipHintFadeIn);
	}
	else
	{
		StopAnimation(SkipHintFadeIn);
	}
}

void UTBStoryCaption::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	Refresh();
}

void UTBStoryCaption::Refresh()
{
	// 字幕の番はタイトルのステージに1つだけ置く。
	if (!Cue.IsValid())
	{
		TActorIterator<ATBStoryCaptionCue> It(GetWorld());
		if (It) Cue = *It;
	}
	const FString Key = Cue.IsValid() ? Cue->CaptionKey : FString();
	if (Key == ShownKey && ShownRevision == TBText::GetRevision()) return;
	ShownKey = Key;
	ShownRevision = TBText::GetRevision();
	// Collapsed から出すと最初のフレームは中央揃えの幅が決まらず左に描かれるので、常に表示して空文字にする。
	Caption->SetText(Key.IsEmpty() ? FText::GetEmpty() : TBText::Get(*Key));
	Caption->SetVisibility(ESlateVisibility::HitTestInvisible);
	SkipHint->SetText(TBText::Get(TEXT("Root.Title.Skip")));
}
