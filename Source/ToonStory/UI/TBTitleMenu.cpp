// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "UI/TBTitleMenu.h"
#include "UI/Loading/TBLoadingScreenSubsystem.h"
#include "UI/TBUIText.h"
#include "Audio/TBAudioSubsystem.h"
#include "Core/TBSession.h"
#include "Core/TBTitleStory.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/CheckBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "UI/TBOptionsMenu.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "TBPaperStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Framework/Application/SlateApplication.h"

void UTBTitleMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	BuildPaperLayout();
	// 寸劇を飛ばしたときに一瞬挟む黒。いちばん手前に重ねる。
	if (auto* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget))
	{
		auto* Black = WidgetTree->ConstructWidget<UImage>();
		Black->SetColorAndOpacity(FLinearColor::Black);
		Black->SetVisibility(ESlateVisibility::Collapsed);
		auto* BlackoutSlot = Root->AddChildToCanvas(Black);
		BlackoutSlot->SetAnchors(FAnchors(0, 0, 1, 1));
		BlackoutSlot->SetOffsets(FMargin(0));
		Blackout = Black;
		auto* Hint = WidgetTree->ConstructWidget<UBorder>();
		Hint->SetBrush(FSlateRoundedBoxBrush(TBPaper::PrintInk.CopyWithNewOpacity(.72f), 1.f, TBPaper::PrintCream.CopyWithNewOpacity(.8f), 1.f));
		Hint->SetPadding(FMargin(13, 4));
		auto* Caption = WidgetTree->ConstructWidget<UTextBlock>();
		Caption->SetText(TBText::Get(TEXT("Root.Title.Skip")));
		Caption->SetFont(FSlateFontInfo(PaperFont, 13));
		Caption->SetColorAndOpacity(TBPaper::PrintCream);
		Hint->SetContent(Caption);
		auto* HintSlot = Root->AddChildToCanvas(Hint);
		HintSlot->SetAnchors(FAnchors(.976f, .966f));
		HintSlot->SetAlignment(FVector2D(1,1));
		HintSlot->SetAutoSize(true);
		Hint->SetVisibility(ESlateVisibility::Collapsed);
		StorySkipHint = Hint;
	}
}

void UTBTitleMenu::NativeConstruct()
{
	Super::NativeConstruct();
	Session = GetGameInstance()->GetSubsystem<UTBSession>();
	if (Session) Session->Changed.AddUniqueDynamic(this, &ThisClass::Refresh);
	Refresh(); // A late subscriber also needs the current state.
	// 物語はゲーム起動後の最初の1回だけ。寸劇が無ければ、その段階は飛ばす。
	auto* Memory = GetGameInstance()->GetSubsystem<UTBTitleStory>();
	if (Memory && !Memory->bShown)
	{
		EnterStory();
	}
	else
	{
		// 試合から戻ったときなど、寸劇を流さずにメニューへ入る。マップの初期状態のままにせず、最後のコマにしておく。
		SettleStory();
	}
	if (State != EState::Story)
	{
		EnterMenu();
	}
}

namespace
{
	const FName StoryTag(TEXT("TitleStory"));
	const FName LoopTag(TEXT("TitleLoop"));
	constexpr float MenuFadeSeconds = .96f;
	constexpr double StoryMenuLeadSeconds = 1.8;
	constexpr float BlackoutSeconds = 0.5f;
	// メニューを放っておくと寸劇を流す。
	constexpr float StoryIdleSeconds = 45.f;

	// タグの付いた、再生できるシーケンスのアクター。
	ALevelSequenceActor* FindSequence(const UObject* Context, FName Tag)
	{
		const UWorld* World = Context ? Context->GetWorld() : nullptr;
		if (!World) return nullptr;
		for (TActorIterator<ALevelSequenceActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(Tag) && It->GetSequencePlayer()) return *It;
		}
		return nullptr;
	}
}

float UTBTitleMenu::GetMenuOpacity() const
{
	return MenuFade;
}

void UTBTitleMenu::SetRoomCardsShown(bool bShown)
{
	bRoomCardsShown = bShown;
	FinderOverlay->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	TitleContent->SetVisibility(bShown ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
}

void UTBTitleMenu::EnterStory()
{
	ALevelSequenceActor* Actor = FindSequence(this, StoryTag);
	if (!Actor) return;
	if (auto* Memory = GetGameInstance()->GetSubsystem<UTBTitleStory>()) Memory->bShown = true;
	State = EState::Story;
	bOpeningOverlap = false;
	StorySkipHint->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (ALevelSequenceActor* Loop = FindSequence(this, LoopTag)) Loop->GetSequencePlayer()->Stop();
	MenuLayer->SetVisibility(ESlateVisibility::Collapsed);
	SetRoomCardsShown(false);
	HelpOverlay->SetVisibility(ESlateVisibility::Collapsed);
	TitleContent->SetIsEnabled(true);
	StoryActor = Actor;
	// メニューを隠すとクリックを受ける物が無くなり、クリックで飛ばせないので、透明な板を全面に出して受ける。
	BlackoutFade = 0.f;
	if (Blackout)
	{
		Blackout->SetRenderOpacity(0.f);
		Blackout->SetVisibility(ESlateVisibility::Visible);
	}
	Actor->GetSequencePlayer()->OnFinished.AddUniqueDynamic(this, &ThisClass::StoryFinished);
	// 飛ばしたあとは最後のコマにいるので、見直すときは頭から流す。
	Actor->GetSequencePlayer()->SetPlaybackPosition(
	    FMovieSceneSequencePlaybackParams(Actor->GetSequencePlayer()->GetStartTime().Time, EUpdatePositionMethod::Jump));
	Actor->GetSequencePlayer()->Play();
	SetKeyboardFocus();
}

void UTBTitleMenu::SettleStory()
{
	if (ALevelSequenceActor* Actor = FindSequence(this, StoryTag))
	{
		Actor->GetSequencePlayer()->GoToEndAndStop();
	}
}

void UTBTitleMenu::SkipStory()
{
	if (State != EState::Story) return;
	if (ALevelSequenceActor* Actor = StoryActor.Get())
	{
		Actor->GetSequencePlayer()->OnFinished.RemoveDynamic(this, &ThisClass::StoryFinished);
		// 寸劇は終わりの状態を残す設定なので、最後のコマ（タイトルの画）まで進めてから止める。
		Actor->GetSequencePlayer()->GoToEndAndStop();
	}
	EnterMenu();
	// 途中で切れた画をそのまま見せず、黒から明ける。
	BlackoutFade = 1.f;
	if (Blackout)
	{
		Blackout->SetRenderOpacity(1.f);
		Blackout->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UTBTitleMenu::StoryFinished()
{
	if (State == EState::Story) EnterMenu();
	else if (bOpeningOverlap)
	{
		bOpeningOverlap = false;
		StartLoop();
	}
}

void UTBTitleMenu::StartLoop()
{
	if (ALevelSequenceActor* Loop = FindSequence(this, LoopTag))
	{
		if (!Loop->GetSequencePlayer()->IsPlaying()) Loop->GetSequencePlayer()->Play();
	}
}

void UTBTitleMenu::NoteInput()
{
	if (State != EState::Menu) return;
	IdleSeconds = 0.f;
}

bool UTBTitleMenu::IsBusyInMenu() const
{
	// 部屋を探している・入ろうとしている途中、ヘルプやオプションを読んでいる途中に、寸劇で割り込まない。
	return bRoomCardsShown || (QuitOverlay && QuitOverlay->GetVisibility() == ESlateVisibility::Visible) || (Session && Session->IsBusy()) || (HelpOverlay && HelpOverlay->GetVisibility() == ESlateVisibility::Visible) ||
	       (Options.IsValid() && Options->IsInViewport());
}

void UTBTitleMenu::UpdateIdle(float DeltaTime)
{
	if (State != EState::Menu) return;
	// 通信やオプションの途中は数えず、終わってから45秒を数え直す。
	if (IsBusyInMenu())
	{
		IdleSeconds = 0.f;
		return;
	}
	IdleSeconds += DeltaTime;
	if (IdleSeconds >= StoryIdleSeconds)
	{
		EnterStory();
		// 寸劇が無ければ、また待ち直す。
		if (State == EState::Menu) IdleSeconds = 0.f;
	}
}

void UTBTitleMenu::EnterMenu(bool bBeforeStoryEnd)
{
	State = EState::Menu;
	if (auto* Memory = GetGameInstance()->GetSubsystem<UTBTitleStory>()) Memory->bShown = true;
	IdleSeconds = 0.f;
	if (FSlateApplication::IsInitialized()) LastCursor = FSlateApplication::Get().GetCursorPos();
	StorySkipHint->SetVisibility(ESlateVisibility::Collapsed);
	bOpeningOverlap = bBeforeStoryEnd;
	if (!bBeforeStoryEnd) StartLoop();
	if (ALevelSequenceActor* Loop = FindSequence(this, LoopTag)) Loop->GetSequencePlayer()->SetPlayRate(1.f);
	MenuFade = 0.f;
	MenuLayer->SetRenderOpacity(0.f);
	TitlePrintSheet->SetRenderTranslation(FVector2D(-680.f, 0));
	MenuLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (Blackout && BlackoutFade <= 0.f) Blackout->SetVisibility(ESlateVisibility::Collapsed);
	HostButton->SetKeyboardFocus();
}

FReply UTBTitleMenu::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	if (UTBLoadingScreenSubsystem::IsCovering(GetGameInstance())) return FReply::Handled();
	if (State == EState::Story)
	{
		SkipStory();
		return FReply::Handled();
	}
	NoteInput();
	return Super::NativeOnPreviewMouseButtonDown(Geometry, MouseEvent);
}

FReply UTBTitleMenu::NativeOnAnalogValueChanged(const FGeometry& Geometry, const FAnalogInputEvent& InputEvent)
{
	if (UTBLoadingScreenSubsystem::IsCovering(GetGameInstance())) return FReply::Handled();
	if (State == EState::Story && FMath::Abs(InputEvent.GetAnalogValue()) > .5f)
	{
		SkipStory();
		return FReply::Handled();
	}
	if (FMath::Abs(InputEvent.GetAnalogValue()) > .5f) NoteInput();
	return Super::NativeOnAnalogValueChanged(Geometry, InputEvent);
}

void UTBTitleMenu::NativeDestruct()
{
	if (Session) Session->Changed.RemoveDynamic(this, &ThisClass::Refresh);
	Super::NativeDestruct();
}

void UTBTitleMenu::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (LanguageRevision != TBText::GetRevision())
	{
		LanguageRevision = TBText::GetRevision();
		if (HelpOverlay->IsVisible()) RefreshHelpText();
		Refresh();
	}
	if (State == EState::Story && StoryActor.IsValid())
	{
		const auto* Player = StoryActor->GetSequencePlayer();
		const double Remaining = Player->GetEndTime().AsSeconds() - Player->GetCurrentTime().AsSeconds();
		if (Player->IsPlaying() && Remaining <= StoryMenuLeadSeconds) EnterMenu(true);
	}
	if (State == EState::Menu && MenuFade < 1.f)
	{
		MenuFade = FMath::Min(1.f, MenuFade + DeltaTime / MenuFadeSeconds);
		const float Eased = FMath::InterpEaseOut(0.f, 1.f, MenuFade, 3.f);
		MenuLayer->SetRenderOpacity(Eased);
		TitlePrintSheet->SetRenderTranslation(FVector2D(-680.f * (1.f - Eased), 0));
	}
	if (State == EState::Menu && MenuLayer->GetVisibility() == ESlateVisibility::Collapsed &&
	    !(Options.IsValid() && Options->IsInViewport()))
	{
		MenuLayer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (BlackoutFade > 0.f && Blackout)
	{
		BlackoutFade = FMath::Max(0.f, BlackoutFade - DeltaTime / BlackoutSeconds);
		Blackout->SetRenderOpacity(BlackoutFade);
		if (BlackoutFade <= 0.f) Blackout->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (State == EState::Menu && FSlateApplication::IsInitialized())
	{
		const FVector2D CursorPosition = FSlateApplication::Get().GetCursorPos();
		if (FVector2D::DistSquared(CursorPosition, LastCursor) > 4.f) NoteInput();
		LastCursor = CursorPosition;
	}
	UpdateIdle(DeltaTime);
	for (UButton* Button : {HostButton.Get(), FindButton.Get(), HelpButton.Get()})
	{
		const FVector2D Target = Button->IsPressed() ? FVector2D(5, 4) :
		    Button->IsHovered() ? FVector2D(5, -1) : FVector2D::ZeroVector;
		Button->SetRenderTranslation(Target);
	}
}

void UTBTitleMenu::Refresh()
{
	if (!Session) return;
	const bool Ready = !Session->IsBusy();
	HostButton->SetIsEnabled(Ready);
	FindButton->SetIsEnabled(Ready);
	RefreshRoomsButton->SetIsEnabled(Ready);
	const bool bRestoreRowFocus = bRoomCardsShown && RoomList->HasFocusedDescendants();
	RoomList->SetIsEnabled(Ready);
	RoomList->ClearChildren();
	SelectedRoom = Session->Rooms.IsEmpty() ? INDEX_NONE : FMath::Clamp(SelectedRoom, 0, Session->Rooms.Num() - 1);
	FCheckBoxStyle RowStyle;
	const FSlateRoundedBoxBrush Normal(TBPaper::Paper, 7.f, TBPaper::Outline.CopyWithNewOpacity(.55f), 2.f);
	const FSlateRoundedBoxBrush Hovered(TBPaper::Cream, 7.f, TBPaper::Outline, 2.f);
	const FSlateRoundedBoxBrush Selected(FMath::Lerp(TBPaper::Paper, TBPaper::Teal, .48f), 7.f, TBPaper::Outline, 2.5f);
	RowStyle.SetCheckBoxType(ESlateCheckBoxType::ToggleButton).SetPadding(FMargin(18, 12));
	RowStyle.SetUncheckedImage(Normal).SetUncheckedHoveredImage(Hovered).SetUncheckedPressedImage(Selected);
	RowStyle.SetCheckedImage(Selected).SetCheckedHoveredImage(Selected).SetCheckedPressedImage(Selected);
	for (int32 Index = 0; Index < Session->Rooms.Num(); ++Index)
	{
		auto* Row = WidgetTree->ConstructWidget<UCheckBox>();
		Row->SetWidgetStyle(RowStyle);
		auto* Caption = WidgetTree->ConstructWidget<UTextBlock>();
		Caption->SetText(Session->GetRoomLabel(Index));
		Caption->SetFont(FSlateFontInfo(PaperFont, 15));
		Caption->SetColorAndOpacity(TBPaper::Ink);
		Caption->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		Row->SetContent(Caption);
		Row->SetIsChecked(SelectedRoom == Index);
		Row->OnCheckStateChanged.AddDynamic(this, &ThisClass::RoomChecked);
		CastChecked<UScrollBoxSlot>(RoomList->AddChild(Row))->SetPadding(FMargin(0, 0, 8, 8));
	}
	if (bRestoreRowFocus)
	{
		UWidget* NextFocus = Ready ? RefreshRoomsButton.Get() : BackFromFinderButton.Get();
		if (Ready && SelectedRoom != INDEX_NONE) NextFocus = RoomList->GetChildAt(SelectedRoom);
		NextFocus->SetKeyboardFocus();
	}
	JoinButton->SetIsEnabled(Ready && SelectedRoom != INDEX_NONE);
	RoomHintText->SetText(FText::FromString(TBPaper::Status(Session->Status)));
	const bool bShowRooms = Ready && !Session->Rooms.IsEmpty();
	auto* HintSlot = CastChecked<UCanvasPanelSlot>(RoomHintText->Slot);
	HintSlot->SetPosition(bShowRooms ? FVector2D(652, 251) : FVector2D(447, 352));
	HintSlot->SetSize(bShowRooms ? FVector2D(174, 47) : FVector2D(385, 56));
	RoomHintText->SetJustification(bShowRooms ? ETextJustify::Right : ETextJustify::Center);
	RoomList->SetVisibility(bShowRooms ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	JoinButton->SetVisibility(bShowRooms ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	auto* RefreshSlot = CastChecked<UCanvasPanelSlot>(RefreshRoomsButton->Slot);
	RefreshSlot->SetPosition(FVector2D(bShowRooms ? 573 : 719, 437));
	CastChecked<UCanvasPanelSlot>(BackFromFinderButton->Slot)->SetPosition(FVector2D(bShowRooms ? 450 : 601, 437));
	// 普段の案内は出さない。通信中やエラーなど、知らせることがあるときだけ出す。
	const bool bIdle = Session->Status == TEXT("Create a room or find friends to begin.");
	StatusText->SetText(bIdle ? FText::GetEmpty() : FText::FromString(TBPaper::Status(Session->Status)));
}

void UTBTitleMenu::RoomChecked(bool bChecked)
{
	if (bChecked)
	{
		for (int32 Index = 0; Index < RoomList->GetChildrenCount(); ++Index)
		{
			if (Index != SelectedRoom && CastChecked<UCheckBox>(RoomList->GetChildAt(Index))->IsChecked())
			{
				SelectedRoom = Index;
				Hover();
				break;
			}
		}
	}
	for (int32 Index = 0; Index < RoomList->GetChildrenCount(); ++Index)
	{
		CastChecked<UCheckBox>(RoomList->GetChildAt(Index))->SetIsChecked(Index == SelectedRoom);
	}
}
namespace
{
	void PlayMenuClick(UGameInstance* GameInstance, ETBSoundEvent Event = ETBSoundEvent::UIClick)
	{
		if (GameInstance)
			if (auto* Audio = GameInstance->GetSubsystem<UTBAudioSubsystem>()) Audio->PlaySoundEvent(Event);
	}
}

void UTBTitleMenu::Host() { PlayMenuClick(GetGameInstance()); if (Session) Session->Host(); }
void UTBTitleMenu::Find()
{
	PlayMenuClick(GetGameInstance());
	SetRoomCardsShown(true);
	BackFromFinderButton->SetKeyboardFocus();
	if (Session) Session->Find();
}
void UTBTitleMenu::CloseFinder()
{
	PlayMenuClick(GetGameInstance(), ETBSoundEvent::UIBack);
	SetRoomCardsShown(false);
	if (FindButton->GetIsEnabled()) FindButton->SetKeyboardFocus();
	else SetKeyboardFocus();
}
void UTBTitleMenu::Join() { PlayMenuClick(GetGameInstance()); if (Session) Session->Join(SelectedRoom); }
void UTBTitleMenu::Hover() { PlayMenuClick(GetGameInstance(), ETBSoundEvent::UIHover); }

void UTBTitleMenu::OpenOptions()
{
	PlayMenuClick(GetGameInstance());
	TWeakObjectPtr<UButton> Button = OptionsButton;
	TWeakObjectPtr<UWidget> Title = MenuLayer;
	Options = UTBOptionsMenu::Open(GetOwningPlayer(), [Button, Title]
	{
		if (Title.IsValid()) Title->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (Button.IsValid()) Button->SetKeyboardFocus();
	});
	if (Options.IsValid()) MenuLayer->SetVisibility(ESlateVisibility::Collapsed);
}
