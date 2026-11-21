// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "TBChatBox.h"
#include "UI/TBUIText.h"
#include "Components/ButtonSlot.h"
#include "TBPaperStyle.h"
#include "Audio/TBAudioSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/TBChat.h"
#include "Core/TBController.h"
#include "Data/TBRuleData.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"

namespace
{
	constexpr float PanelX = 29, PanelW = 438, LobbyBottom = 783, ResultsBottom = 790;
}

void UTBChatBox::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	PaperFont = TBPaper::Font(this);
	// 板の外のクリックはHUDのボタンへ通す。
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Root;
	auto* Scale = WidgetTree->ConstructWidget<UScaleBox>();
	Scale->SetStretch(EStretch::ScaleToFit);
	auto* ScaleSlot = Root->AddChildToCanvas(Scale);
	ScaleSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	ScaleSlot->SetOffsets(FMargin(0));
	auto* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(1440);
	Size->SetHeightOverride(810);
	auto* BoardSlot = CastChecked<UScaleBoxSlot>(Scale->SetContent(Size));
	BoardSlot->SetHorizontalAlignment(HAlign_Left);
	BoardSlot->SetVerticalAlignment(VAlign_Bottom);
	auto* Board = WidgetTree->ConstructWidget<UCanvasPanel>();
	Size->SetContent(Board);
	for (UWidget* Layer : TArray<UWidget*>{Root, Scale, Size, Board})
	{
		Layer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	Panel = WidgetTree->ConstructWidget<UVerticalBox>();
	PanelSlot = Board->AddChildToCanvas(Panel);
	LogBack = WidgetTree->ConstructWidget<UBorder>();
	LogBack->SetPadding(FMargin(9, 5));
	LogBack->SetVerticalAlignment(VAlign_Bottom);
	Panel->AddChildToVerticalBox(LogBack)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Scroll = WidgetTree->ConstructWidget<UScrollBox>();
	Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	// 消えた行の場所を残すので、閉じていてもスクロール位置が0を超える。上下の影が罫線に見えるので描かない。
	FScrollBoxStyle ScrollStyle = Scroll->GetWidgetStyle();
	ScrollStyle.SetTopShadowBrush(FSlateNoResource()).SetBottomShadowBrush(FSlateNoResource());
	Scroll->SetWidgetStyle(ScrollStyle);
	auto* LogOverlay = WidgetTree->ConstructWidget<UOverlay>();
	LogBack->SetContent(LogOverlay);
	EmptyLog = WidgetTree->ConstructWidget<UTextBlock>();
	EmptyLog->SetText(TBText::Get(TEXT("Lobby.Joined")));
	EmptyLog->SetFont(FSlateFontInfo(PaperFont, 11));
	EmptyLog->SetColorAndOpacity(TBPaper::PrintCream);
	LogOverlay->AddChildToOverlay(EmptyLog)->SetVerticalAlignment(VAlign_Bottom);
	LogOverlay->AddChildToOverlay(Scroll);
	LineBox = WidgetTree->ConstructWidget<UVerticalBox>();
	Scroll->AddChild(LineBox);

	InputBack = WidgetTree->ConstructWidget<UBorder>();
	InputBack->SetBrush(FSlateRoundedBoxBrush(TBPaper::PrintInk.CopyWithNewOpacity(.95f), 0.f, TBPaper::PrintCream, 1.f));
	InputBack->SetPadding(FMargin(8, 5));
	Panel->AddChildToVerticalBox(InputBack);
	auto* InputSize = WidgetTree->ConstructWidget<USizeBox>();
	InputSize->SetHeightOverride(29);
	InputBack->SetContent(InputSize);
	auto* InputCanvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	InputSize->SetContent(InputCanvas);
	InputPlate = WidgetTree->ConstructWidget<UBorder>();
	InputPlate->SetBrush(FSlateRoundedBoxBrush(TBPaper::PrintCream, 0.f, TBPaper::PrintInk, 1.f));
	InputPlate->SetPadding(FMargin(8, 1));
	InputPlate->SetVerticalAlignment(VAlign_Center);
	auto* InputPlateSlot = InputCanvas->AddChildToCanvas(InputPlate);
	InputPlateSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	InputPlateSlot->SetOffsets(FMargin(0, 0, 73, 0));
	Input = WidgetTree->ConstructWidget<UEditableText>();
	Input->SetFont(FSlateFontInfo(PaperFont, 12));
	FEditableTextStyle Style = Input->WidgetStyle;
	Style.SetColorAndOpacity(TBPaper::Ink);
	Input->SetWidgetStyle(Style);
	Input->SetHintText(TBText::Get(TEXT("Root.Chat.Placeholder")));
	// 早すぎて送れなかったときも、打ち直さずに続けられるようにフォーカスを残す。
	Input->SetClearKeyboardFocusOnCommit(false);
	Input->OnTextChanged.AddDynamic(this, &UTBChatBox::TextChanged);
	Input->OnTextCommitted.AddDynamic(this, &UTBChatBox::TextCommitted);
	InputPlate->SetContent(Input);
	SendButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SendChatButton"));
	FButtonStyle PrintButton;
	PrintButton.SetNormal(FSlateRoundedBoxBrush(TBPaper::PrintAqua, 0.f, TBPaper::PrintInk, 1.f));
	PrintButton.SetHovered(FSlateRoundedBoxBrush(TBPaper::PrintHoney, 0.f, TBPaper::PrintInk, 1.f));
	PrintButton.SetPressed(FSlateRoundedBoxBrush(TBPaper::PrintStock, 0.f, TBPaper::PrintInk, 1.f));
	PrintButton.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0, 1, 0, 0));
	SendButton->SetStyle(PrintButton);
	// クリックで入力欄からフォーカスを奪わない。キーボードでは従来どおりEnterで送れる。
	// UButton has no public setter in UE 5.8; this is set before its Slate widget is built.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	SendButton->IsFocusable = false;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	auto* SendText = WidgetTree->ConstructWidget<UTextBlock>();
	SendText->SetText(TBText::Get(TEXT("Root.Chat.Send")));
	auto* ChatHeading = TBPaper::Font(this, TBPaper::EFontRole::Heading);
	SendText->SetFont(FSlateFontInfo(ChatHeading, 11));
	SendText->SetColorAndOpacity(TBPaper::PrintInk);
	auto* SendCaption = CastChecked<UButtonSlot>(SendButton->SetContent(SendText));
	SendCaption->SetHorizontalAlignment(HAlign_Center);
	SendCaption->SetVerticalAlignment(VAlign_Center);
	SendButton->OnClicked.AddDynamic(this, &UTBChatBox::SendClicked);
	auto* SendSlot = InputCanvas->AddChildToCanvas(SendButton);
	SendSlot->SetAnchors(FAnchors(1, 0, 1, 1));
	SendSlot->SetAlignment(FVector2D(1, 0));
	SendSlot->SetOffsets(FMargin(0, 0, 64, 0));
	// 履歴が薄れても入口は残す。キーボードと同じController経路で開く。
	OpenButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("OpenChatButton"));
	PrintButton.SetNormal(FSlateRoundedBoxBrush(TBPaper::PrintCream, 0.f, TBPaper::PrintInk, 1.f));
	OpenButton->SetStyle(PrintButton);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	OpenButton->IsFocusable = false;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	auto* OpenText = WidgetTree->ConstructWidget<UTextBlock>();
	OpenText->SetText(TBText::Get(TEXT("Root.Chat.Open")));
	OpenText->SetFont(FSlateFontInfo(PaperFont, 11));
	OpenText->SetColorAndOpacity(TBPaper::PrintQuiet);
	auto* OpenCaption = CastChecked<UButtonSlot>(OpenButton->SetContent(OpenText));
	OpenCaption->SetPadding(FMargin(9, 0));
	OpenCaption->SetHorizontalAlignment(HAlign_Left);
	OpenCaption->SetVerticalAlignment(VAlign_Center);
	OpenButton->OnClicked.AddDynamic(this, &UTBChatBox::OpenClicked);
	auto* OpenSlot = InputCanvas->AddChildToCanvas(OpenButton);
	OpenSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	OpenSlot->SetOffsets(FMargin(0, 0, 73, 0));
	SetResultsLayout(false);
	SetOpen(false);
}

void UTBChatBox::SetResultsLayout(bool bInResults)
{
	if (bLayoutApplied && bResults == bInResults)
	{
		return;
	}
	bLayoutApplied = true;
	bResults = bInResults;
	UpdatePanelLayout();
}

void UTBChatBox::UpdatePanelLayout()
{
	const bool Compact = bResults && !bOpen;
	const float Height = bOpen ? 157.f : 90.f;
	PanelSlot->SetSize(FVector2D(PanelW, Height));
	PanelSlot->SetPosition(FVector2D(PanelX, (bResults ? ResultsBottom : LobbyBottom) - Height));
	auto* OpenSlot = CastChecked<UCanvasPanelSlot>(OpenButton->Slot);
	OpenSlot->SetOffsets(FMargin(0, 0, Compact ? PanelW - 137.f : 73.f, 0));
	auto* Caption = CastChecked<UTextBlock>(OpenButton->GetContent());
	Caption->SetText(TBText::Get(Compact ? TEXT("Root.Chat.Open") : TEXT("Root.Chat.EnterPrompt")));
	InputPlate->SetVisibility(Compact ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	SendButton->SetVisibility(Compact ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (Compact)
	{
		LogBack->SetBrush(FSlateNoResource());
		InputBack->SetBrush(FSlateNoResource());
	}
	else
	{
		LogBack->SetBrush(FSlateRoundedBoxBrush(TBPaper::PrintInk.CopyWithNewOpacity(.9f), 0.f, TBPaper::PrintCream, 1.f));
		InputBack->SetBrush(FSlateRoundedBoxBrush(TBPaper::PrintInk.CopyWithNewOpacity(.95f), 0.f, TBPaper::PrintCream, 1.f));
	}
}

void UTBChatBox::SetOpen(bool bInOpen)
{
	bOpen = bInOpen;
	UpdatePanelLayout();
	Panel->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible);
	LogBack->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
	InputBack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Input->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	OpenButton->SetVisibility(bOpen ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (bOpen)
	{
		Input->SetKeyboardFocus();
	}
	RequestScrollToEnd();
}

// ScrollToEndは次のTickで、その時点の高さを使って消費される。行を出した直後は高さがまだ古いので、数フレーム続けて頼む。
void UTBChatBox::RequestScrollToEnd()
{
	ScrollRetryFrames = 3;
}

// 履歴は後ろに足すか、前から消すか、全部消すかだけ。消えた行を前から外し、増えた行を後ろに足す。
void UTBChatBox::Sync(const UTBChatLog& Log)
{
	BuiltRevision = Log.GetRevision();
	const auto& Lines = Log.GetLines();
	const uint64 FirstId = Lines.IsEmpty() ? MAX_uint64 : Lines[0].Id;
	int32 Removed = 0;
	while (Removed < LineIds.Num() && LineIds[Removed] < FirstId)
	{
		LineBox->RemoveChild(LineWidgets[Removed]);
		++Removed;
	}
	LineWidgets.RemoveAt(0, Removed);
	LineIds.RemoveAt(0, Removed);
	LineOpacity.RemoveAt(0, Removed);
	const uint64 LastId = LineIds.IsEmpty() ? 0 : LineIds.Last();
	for (const FTBChatLine& Line : Lines)
	{
		if (Line.Id > LastId)
		{
			AddLine(Line);
		}
	}
	RequestScrollToEnd();
}

void UTBChatBox::AddLine(const FTBChatLine& Line)
{
	auto MakeText = [this](const FString& Value, FLinearColor Color)
	{
		auto* Block = WidgetTree->ConstructWidget<UTextBlock>();
		Block->SetText(FText::FromString(Value));
		Block->SetFont(FSlateFontInfo(PaperFont, 11));
		Block->SetColorAndOpacity(Color);
		Block->SetShadowColorAndOpacity(TBPaper::Ink.CopyWithNewOpacity(.7f));
		Block->SetShadowOffset(FVector2D(0, 1));
		Block->SetAutoWrapText(true);
		Block->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
		return Block;
	};
	auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	if (!Line.bNotice)
	{
		// 履歴に陣営情報は無い。青いラベルは送信者の表示で、陣営色ではない。
		auto* NameSize = WidgetTree->ConstructWidget<USizeBox>();
		NameSize->SetWidthOverride(94);
		auto* NamePlate = WidgetTree->ConstructWidget<UBorder>();
		NamePlate->SetBrush(FSlateNoResource());
		NamePlate->SetPadding(FMargin(1, 0));
		NameSize->SetContent(NamePlate);
		auto* Name = MakeText(Line.Name, TBPaper::PrintAqua);
		Name->SetAutoWrapText(false);
		Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		Name->SetClipping(EWidgetClipping::ClipToBounds);
		NamePlate->SetContent(Name);
		Row->AddChildToHorizontalBox(NameSize)->SetVerticalAlignment(VAlign_Top);
	}
	auto* Text = MakeText(Line.bNotice ? TBText::TranslateNotice(Line.Text) : Line.Text,
	    TBPaper::PrintCream);
	Text->SetWrapTextAt(Line.bNotice ? PanelW - 22 : PanelW - 124);
	auto* TextSlot = Row->AddChildToHorizontalBox(Text);
	TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TextSlot->SetPadding(FMargin(Line.bNotice ? 0 : 6, 0, 0, 0));
	LineBox->AddChildToVerticalBox(Row)->SetPadding(FMargin(0, 1));
	LineWidgets.Add(Row);
	LineIds.Add(Line.Id);
	LineOpacity.Add(-1.f); // まだ何も入れていない
}

// 開いている間は全部。閉じている間は新しい発言だけを出し、最後の1秒で薄れさせる。変わった行にだけ入れる。
// 消えた行はCollapsedにせず、透明のまま場所を残す。高さが減ると、ScrollToEndが古い高さで処理される数フレームに、残りの行がずれるため。
void UTBChatBox::RefreshLines(const UTBChatLog& Log, double Now)
{
	if (BuiltLanguageRevision != TBText::GetRevision())
	{
		LineBox->ClearChildren();
		LineWidgets.Reset();
		LineIds.Reset();
		LineOpacity.Reset();
	}
	if (Log.GetRevision() != BuiltRevision || BuiltLanguageRevision != TBText::GetRevision())
	{
		Sync(Log);
		BuiltLanguageRevision = TBText::GetRevision();
	}
	const double ShowSeconds = TB::Rules().Chat.ShowSeconds;
	const auto& Lines = Log.GetLines();
	EmptyLog->SetVisibility(!bResults && (Lines.IsEmpty() || (!bOpen && Now - Lines.Last().Time >= ShowSeconds))
	    ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	for (int32 Index = 0; Index < LineWidgets.Num() && Index < Lines.Num(); ++Index)
	{
		const float Opacity = bOpen ? 1.f : FMath::Clamp(float(ShowSeconds - (Now - Lines[Index].Time)), 0.f, 1.f);
		if (Opacity == LineOpacity[Index])
		{
			continue;
		}
		if (LineOpacity[Index] < 0)
		{
			LineWidgets[Index]->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		LineWidgets[Index]->SetRenderOpacity(Opacity);
		LineOpacity[Index] = Opacity;
	}
}

void UTBChatBox::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	const auto* Log = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTBChatLog>() : nullptr;
	if (!Log)
	{
		return;
	}
	RefreshLines(*Log, FPlatformTime::Seconds());
	if (ScrollRetryFrames > 0)
	{
		--ScrollRetryFrames;
		Scroll->ScrollToEnd();
	}
}

void UTBChatBox::TextChanged(const FText& Text)
{
	const FString Value = Text.ToString();
	const int32 MaxLength = TB::Rules().Chat.MaxLength;
	if (Value.Len() > MaxLength)
	{
		Input->SetText(FText::FromString(TB::Chat::Limit(Value, MaxLength)));
	}
}

void UTBChatBox::TextCommitted(const FText& Text, ETextCommit::Type Method)
{
	auto* PC = Cast<ATBController>(GetOwningPlayer());
	// 閉じたあとのフォーカス移動でも呼ばれる。
	if (!bOpen || !PC)
	{
		return;
	}
	if (Method != ETextCommit::OnEnter)
	{
		PC->CloseChat(); // ほかをクリックした。書きかけは残す
		return;
	}
	const FString Message = TB::Chat::Clean(Text.ToString());
	if (!Message.IsEmpty() && !PC->SendChat(Message))
	{
		if (auto* Audio = PC->GetGameInstance()->GetSubsystem<UTBAudioSubsystem>())
		{
			Audio->PlaySoundEvent(ETBSoundEvent::UIError);
		}
		return;
	}
	Input->SetText(FText::GetEmpty());
	PC->CloseChat();
}

void UTBChatBox::SendClicked()
{
	if (!bOpen) { OpenClicked(); return; }
	if (!Input) return;
	TextCommitted(Input->GetText(), ETextCommit::OnEnter);
	// 連続送信制限などで送れなかった場合は、入力を残してすぐ修正できるようにする。
	if (bOpen) Input->SetKeyboardFocus();
}

void UTBChatBox::OpenClicked()
{
	if (auto* PC = Cast<ATBController>(GetOwningPlayer())) PC->OpenChat();
}

FReply UTBChatBox::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (bOpen && KeyEvent.GetKey() == EKeys::Escape)
	{
		Input->SetText(FText::GetEmpty());
		if (auto* PC = Cast<ATBController>(GetOwningPlayer()))
		{
			PC->CloseChat();
		}
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
}

// 打っている間のキーは、入力欄が使わなかったものもゲーム（準備・開始など）へ渡さない。
FReply UTBChatBox::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	return bOpen ? FReply::Handled() : Super::NativeOnKeyDown(Geometry, KeyEvent);
}

FReply UTBChatBox::NativeOnKeyUp(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	return bOpen ? FReply::Handled() : Super::NativeOnKeyUp(Geometry, KeyEvent);
}
