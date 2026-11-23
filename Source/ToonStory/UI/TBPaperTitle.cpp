// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "UI/TBUIText.h"
#include "UI/TBTitleMenu.h"
#include "UI/Loading/TBLoadingScreenSubsystem.h"
#include "Core/TBSession.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Engine/GameInstance.h"
#include "TBPaperStyle.h"
#include "TBPaperButton.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/Image.h"
#include "Kismet/KismetSystemLibrary.h"
#include "InputCoreTypes.h"

void UTBTitleMenu::BuildPaperLayout()
{
	PaperFont = TBPaper::Font(this);
	LogoFont = TBPaper::Font(this, TBPaper::EFontRole::Heading);
	// WBP が Root、TitleScale、TitleCanvas を置いていればそれを使う。足りなければ、ここで全部作る。
	auto* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	UScaleBox* Scale = TitleScale;
	UCanvasPanel* Canvas = TitleCanvas;
	if (Root && Scale && Canvas)
	{
		// 暗幕の上に重ねるので、いったん外して、あとで MenuLayer に入れ直す。
		Scale->RemoveFromParent();
		// 配置の基準の枠は 1600×900 に固定する。WBP の上書きが効いていなくても、中身の大きさで枠が変わらない。
		if (auto* Size = Cast<USizeBox>(Canvas->GetParent()))
		{
			Size->SetWidthOverride(1600);
			Size->SetHeightOverride(900);
		}
	}
	else
	{
		// 古いツリーは捨てるので、WBPのバインドも外す。
		HostButton = nullptr;
		FindButton = nullptr;
		HelpButton = nullptr;
		OptionsButton = nullptr;
		QuitButton = nullptr;
		LogoImage = nullptr;
		Root = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Root;
		Scale = WidgetTree->ConstructWidget<UScaleBox>();
		Scale->SetStretch(EStretch::ScaleToFit);
		auto* Size = WidgetTree->ConstructWidget<USizeBox>();
		// 配置の基準は 16:9 の 1600×900。画面の比率が違っても、この枠ごと収めるので切れない。
		Size->SetWidthOverride(1600);
		Size->SetHeightOverride(900);
		Scale->SetContent(Size);
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		Size->SetContent(Canvas);
	}
	auto Fill = [&](UWidget* Widget)
	{
		auto* Slot = Root->AddChildToCanvas(Widget);
		Slot->SetAnchors(FAnchors(0, 0, 1, 1));
		Slot->SetOffsets(FMargin(0));
	};
	auto Place = [&](UWidget* Widget, float X, float Y, float W, float H)
	{
		auto* Slot = Canvas->AddChildToCanvas(Widget);
		Slot->SetPosition(FVector2D(X, Y));
		Slot->SetSize(FVector2D(W, H));
	};
	// 後ろの3D（ループの映像）が主役なので、暗幕は敷かない。
	auto* Layer = WidgetTree->ConstructWidget<UCanvasPanel>();
	MenuLayer = Layer;
	Fill(Layer);
	auto* ScaleSlot = Layer->AddChildToCanvas(Scale);
	ScaleSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	ScaleSlot->SetOffsets(FMargin(0));
	auto Label = [&](const FText& Text, int32 FontSize, FLinearColor Color, bool Logo = false, FName Name = NAME_None)
	{
		auto* L = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		L->SetText(Text);
		L->SetFont(FSlateFontInfo(Logo ? LogoFont : PaperFont, FontSize));
		L->SetColorAndOpacity(Color);
		L->SetAutoWrapText(true);
		return L;
	};
	// Only the logo and controls slide in over the title sequence.
	Canvas->ClearChildren();
	Canvas->SetClipping(EWidgetClipping::ClipToBounds);
	auto* Sheet = WidgetTree->ConstructWidget<UCanvasPanel>();
	Place(Sheet, 0, 0, 1600, 900);
	TitlePrintSheet = Sheet;
	Canvas = Sheet;
	if (!LogoImage) LogoImage = WidgetTree->ConstructWidget<UImage>();
	LogoImage->SetRenderTransform(FWidgetTransform());
	LogoImage->SetColorAndOpacity(FLinearColor::White);
	LogoImage->SetBrushFromTexture(TBPaper::Art(TEXT("TitleSteamMark")));
	LogoImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(LogoImage, 63, 63, 415, 297);

	auto AddButton = [&](const TCHAR* Name, const FText& Text, float X, float Y, float W)
	{
		auto* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
		B->SetStyle(TBPaper::PrintButtonStyle(TBPaper::PrintCream));
		B->OnHovered.AddDynamic(this, &ThisClass::Hover);
		auto* Caption = Label(Text, 16, TBPaper::PrintInk, true);
		Caption->SetAutoWrapText(false);
		B->SetContent(Caption);
		Place(B, X, Y, W, 44);
		return B;
	};
	auto Rect = [&](UCanvasPanel* Parent, float X, float Y, float W, float H, FLinearColor Color)
	{
		auto* Face = WidgetTree->ConstructWidget<UImage>();
		Face->SetColorAndOpacity(Color);
		Face->SetVisibility(ESlateVisibility::HitTestInvisible);
		auto* Slot = Parent->AddChildToCanvas(Face);
		Slot->SetPosition(FVector2D(X,Y));
		Slot->SetSize(FVector2D(W,H));
		return Face;
	};
	auto TitleButton = [&](UButton* Bound, const TCHAR* Name, const FButtonStyle& Style)
	{
		auto* Button = Bound ? Bound : WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
		Button->SetVisibility(ESlateVisibility::Visible);
		Button->SetRenderTransform(FWidgetTransform());
		Button->SetColorAndOpacity(FLinearColor::White);
		Button->SetBackgroundColor(FLinearColor::White);
		Button->SetStyle(Style);
		Button->OnHovered.AddUniqueDynamic(this, &ThisClass::Hover);
		return Button;
	};
	auto CoverChoice = [&](UButton* Bound, const TCHAR* Name, const TCHAR* Key, float Y, FLinearColor Color)
	{
		constexpr float X = 80, W = 390, H = 61;
		auto* Shadow = Rect(Canvas, X + 4, Y + 5, W, H, TBPaper::PrintInk);
		Shadow->SetRenderShear(FVector2D(-5, 0));
		FButtonStyle Style = TBPaper::PrintButtonStyle(Color);
		Style.SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
		auto* Button = TitleButton(Bound, Name, Style);
		Button->SetRenderShear(FVector2D(-5, 0));
		auto* Content = WidgetTree->ConstructWidget<UCanvasPanel>();
		auto* ContentSlot = CastChecked<UButtonSlot>(Button->SetContent(Content));
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
		ContentSlot->SetPadding(FMargin(0));
		auto* Caption = Label(TBText::Get(Key), 20, TBPaper::PrintInk, true);
		Caption->SetAutoWrapText(false);
		Caption->SetRenderShear(FVector2D(5, 0));
		auto* Fit = WidgetTree->ConstructWidget<UScaleBox>();
		Fit->SetStretch(EStretch::ScaleToFit);
		Fit->SetStretchDirection(EStretchDirection::DownOnly);
		Fit->SetContent(Caption);
		CastChecked<UScaleBoxSlot>(Caption->Slot)->SetHorizontalAlignment(HAlign_Left);
		auto* TextSlot = Content->AddChildToCanvas(Fit);
		TextSlot->SetAnchors(FAnchors(0, 0, 1, 1));
		TextSlot->SetOffsets(FMargin(24, 3, 52, 3));
		Rect(Content, W - 39, H * .5f - 4, 12, 3, TBPaper::PrintInk)->SetRenderTransformAngle(45);
		Rect(Content, W - 39, H * .5f + 3, 12, 3, TBPaper::PrintInk)->SetRenderTransformAngle(-45);
		Place(Button, X, Y, W, H);
		return Button;
	};
	HostButton = CoverChoice(HostButton, TEXT("HostButton"), TEXT("Root.Title.Host"), 540, TBPaper::PrintHoney);
	FindButton = CoverChoice(FindButton, TEXT("FindButton"), TEXT("Root.Title.Find"), 613, TBPaper::PrintCream);
	HelpButton = CoverChoice(HelpButton, TEXT("HelpButton"), TEXT("Root.Title.Help"), 686, TBPaper::PrintCream);
	auto* Utilities = WidgetTree->ConstructWidget<UHorizontalBox>();
	Place(Utilities, 80, 798, 360, 39);
	auto Utility = [&](UButton* Bound, const TCHAR* Name, const TCHAR* Key)
	{
		FButtonStyle Style;
		Style.SetNormal(FSlateNoResource()).SetHovered(FSlateRoundedBoxBrush(TBPaper::PrintInk, 0.f));
		Style.SetPressed(FSlateRoundedBoxBrush(TBPaper::PrintSea, 0.f));
		Style.SetNormalPadding(FMargin(0, 2)).SetPressedPadding(FMargin(0, 3, 0, 1));
		auto* Button = TitleButton(Bound, Name, Style);
		auto* Caption = Label(TBText::Get(Key), 14, TBPaper::PrintCream);
		Caption->SetAutoWrapText(false);
		auto Font = Caption->GetFont();
		Font.OutlineSettings.OutlineSize = 1;
		Font.OutlineSettings.OutlineColor = TBPaper::PrintInk;
		Caption->SetFont(Font);
		Caption->SetShadowColorAndOpacity(TBPaper::PrintInk);
		Caption->SetShadowOffset(FVector2D(0, 1));
		auto* CaptionSlot = CastChecked<UButtonSlot>(Button->SetContent(Caption));
		CaptionSlot->SetPadding(FMargin(0));
		CaptionSlot->SetVerticalAlignment(VAlign_Center);
		auto* Link = WidgetTree->ConstructWidget<UBorder>();
		Link->SetBrush(FSlateRoundedBoxBrush(TBPaper::PrintCream.CopyWithNewOpacity(.5f), 0.f));
		// Draw the fine underline behind the link, sized by its translated caption.
		auto* Stack = WidgetTree->ConstructWidget<UVerticalBox>();
		Stack->AddChildToVerticalBox(Button)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		auto* Rule = WidgetTree->ConstructWidget<USizeBox>();
		Rule->SetHeightOverride(1.f);
		Link->SetVisibility(ESlateVisibility::HitTestInvisible);
		Rule->SetContent(Link);
		Stack->AddChildToVerticalBox(Rule);
		Utilities->AddChildToHorizontalBox(Stack)->SetPadding(FMargin(0, 0, 26, 0));
		return Button;
	};
	OptionsButton = Utility(OptionsButton, TEXT("OptionsButton"), TEXT("Options.Title"));
	QuitButton = Utility(QuitButton, TEXT("QuitButton"), TEXT("Root.Title.QuitShort"));
	StatusText = Label(FText::FromString(TEXT("")), 17, TBPaper::Paper);
	StatusText->SetShadowColorAndOpacity(TBPaper::Ink);
	StatusText->SetShadowOffset(FVector2D(1, 2));
	Place(StatusText, 80, 752, 390, 46);
	HostButton->OnClicked.AddDynamic(this, &ThisClass::Host);
	FindButton->OnClicked.AddDynamic(this, &ThisClass::Find);
	QuitButton->OnClicked.AddDynamic(this, &ThisClass::AskQuit);
	HelpButton->OnClicked.AddDynamic(this, &ThisClass::Help);
	OptionsButton->OnClicked.AddDynamic(this, &ThisClass::OpenOptions);
	TitleContent = Scale;
	auto NewPopup = [&](float Width = 1280, float Height = 800, FLinearColor Background = TBPaper::Ink.CopyWithNewOpacity(.62f))
	{
		auto* Overlay = WidgetTree->ConstructWidget<UCanvasPanel>();
		Fill(Overlay);
		auto FillPopup = [&](UWidget* Widget)
		{
			auto* Slot = Overlay->AddChildToCanvas(Widget);
			Slot->SetAnchors(FAnchors(0, 0, 1, 1));
			Slot->SetOffsets(FMargin(0));
		};
		auto* Dim = WidgetTree->ConstructWidget<UImage>();
		Dim->SetColorAndOpacity(Background);
		FillPopup(Dim);
		auto* PopupScale = WidgetTree->ConstructWidget<UScaleBox>();
		PopupScale->SetStretch(EStretch::ScaleToFit);
		FillPopup(PopupScale);
		auto* PopupSize = WidgetTree->ConstructWidget<USizeBox>();
		PopupSize->SetWidthOverride(Width);
		PopupSize->SetHeightOverride(Height);
		PopupScale->SetContent(PopupSize);
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		PopupSize->SetContent(Canvas);
		return Overlay;
	};
	auto Face = [&](const FSlateBrush& Brush, float X, float Y, float W, float H)
	{
		auto* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrush(Brush);
		Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Panel, X, Y, W, H);
		return Panel;
	};
	auto DialogButton = [&](const TCHAR* Name, const TCHAR* Key, float X, float Y, float W, FLinearColor Color)
	{
		auto* Button = AddButton(Name, TBText::Get(Key), X, Y, W);
		Button->SetStyle(TBPaper::PrintButtonStyle(Color));
		CastChecked<UTextBlock>(Button->GetContent())->SetFont(FSlateFontInfo(LogoFont, 13));
		auto* Slot = CastChecked<UButtonSlot>(Button->GetContent()->Slot);
		Slot->SetHorizontalAlignment(HAlign_Center);
		Slot->SetVerticalAlignment(VAlign_Center);
		return Button;
	};
	FinderOverlay = NewPopup(1280, 720, TBPaper::PrintInk.CopyWithNewOpacity(.63f));
	Face(FSlateRoundedBoxBrush(TBPaper::PrintInk, 0.f), 427, 227, 430, 282);
	Face(FSlateRoundedBoxBrush(TBPaper::PrintCream, 1.f, TBPaper::PrintInk, 2.f), 424, 223, 430, 282);
	Face(FSlateRoundedBoxBrush(TBPaper::PrintHoney, 1.f, TBPaper::PrintInk, 2.f), 450, 248, 195, 45)->SetRenderTransformAngle(-1.f);
	auto* FindCaption = Label(TBText::Get(TEXT("Root.Title.Find")), 18, TBPaper::PrintInk, true);
	FindCaption->SetJustification(ETextJustify::Center);
	Place(FindCaption, 451, 249, 192, 43);
	RoomHintText = Label(FText::GetEmpty(), 13, TBPaper::PrintInk);
	RoomHintText->SetJustification(ETextJustify::Center);
	Place(RoomHintText, 447, 352, 385, 56);
	RoomList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("Rooms"));
	RoomList->SetScrollbarThickness(FVector2D(5, 5));
	Place(RoomList, 446, 304, 385, 123);
	BackFromFinderButton = DialogButton(TEXT("BackFromFinder"), TEXT("Root.Common.Back"), 450, 437, 105, TBPaper::PrintCream);
	RefreshRoomsButton = DialogButton(TEXT("RefreshRooms"), TEXT("Root.Title.Refresh"), 573, 437, 110, TBPaper::PrintHoney);
	JoinButton = DialogButton(TEXT("Join"), TEXT("Root.Title.Join"), 697, 437, 132, TBPaper::PrintAqua);
	BackFromFinderButton->OnClicked.AddDynamic(this, &ThisClass::CloseFinder);
	RefreshRoomsButton->OnClicked.AddDynamic(this, &ThisClass::Find);
	JoinButton->OnClicked.AddDynamic(this, &ThisClass::Join);
	SetRoomCardsShown(false);

	QuitOverlay = NewPopup(1280, 720, TBPaper::PrintInk.CopyWithNewOpacity(.63f));
	Face(FSlateRoundedBoxBrush(TBPaper::PrintInk, 0.f), 432, 280, 424, 164);
	Face(FSlateRoundedBoxBrush(TBPaper::PrintCream, 1.f, TBPaper::PrintInk, 2.f), 428, 276, 424, 164);
	auto* QuitPrompt = Label(TBText::Get(TEXT("Root.Title.ConfirmQuit")), 18, TBPaper::PrintInk, true);
	Place(QuitPrompt, 453, 303, 374, 48);
	CancelQuitButton = DialogButton(TEXT("CancelQuit"), TEXT("Root.Common.Back"), 573, 369, 114, TBPaper::PrintCream);
	auto* End = DialogButton(TEXT("ConfirmQuit"), TEXT("Root.Title.QuitShort"), 701, 369, 124, TBPaper::PrintHoney);
	CancelQuitButton->OnClicked.AddDynamic(this, &ThisClass::CancelQuit);
	End->OnClicked.AddDynamic(this, &ThisClass::Quit);
	QuitOverlay->SetVisibility(ESlateVisibility::Collapsed);

	HelpOverlay = NewPopup(1600, 900, FLinearColor::FromSRGBColor(FColor(235, 224, 199)));
	auto* Paper = WidgetTree->ConstructWidget<UImage>();
	Paper->SetBrushFromTexture(TBPaper::Art(TEXT("GuidePaper")));
	Paper->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(Paper, 0, 0, 1600, 900);

	auto* Heading = Label(TBText::Get(TEXT("Root.Title.Help")), 30, TBPaper::PrintCream, true);
	Heading->SetAutoWrapText(false);
	auto* FitHeading = WidgetTree->ConstructWidget<UScaleBox>();
	FitHeading->SetStretch(EStretch::ScaleToFit);
	FitHeading->SetStretchDirection(EStretchDirection::DownOnly);
	FitHeading->SetContent(Heading);
	auto* HeadingSlot = CastChecked<UScaleBoxSlot>(Heading->Slot);
	HeadingSlot->SetHorizontalAlignment(HAlign_Left);
	HeadingSlot->SetVerticalAlignment(VAlign_Center);
	Place(FitHeading, 118, 23, 235, 58);
	auto GuideButton = [&](const TCHAR* Name, const TCHAR* Key, float X, float Y, float Width)
	{
		auto* Button = AddButton(Name, TBText::Get(Key), X, Y, Width);
		CastChecked<UCanvasPanelSlot>(Button->Slot)->SetSize(FVector2D(Width, 44));
		auto* Caption = CastChecked<UTextBlock>(Button->GetContent());
		Caption->SetFont(FSlateFontInfo(LogoFont, 15));
		Caption->SetColorAndOpacity(TBPaper::PrintInk);
		auto* Slot = CastChecked<UButtonSlot>(Caption->Slot);
		Slot->SetHorizontalAlignment(HAlign_Center);
		Slot->SetVerticalAlignment(VAlign_Center);
		return Button;
	};
	HelpToyTab = GuideButton(TEXT("HelpToyTab"), TEXT("Root.Help.ToyTab"), 406, 31, 118);
	HelpHumanTab = GuideButton(TEXT("HelpHumanTab"), TEXT("Root.Help.HumanTab"), 536, 31, 118);
	HelpToyTab->OnClicked.AddDynamic(this, &ThisClass::ShowToyHelp);
	HelpHumanTab->OnClicked.AddDynamic(this, &ThisClass::ShowHumanHelp);

	HelpGoal = Label(FText::GetEmpty(), 16, TBPaper::PrintInk, true, TEXT("HelpGoal"));
	HelpGoal->SetAutoWrapText(false);
	auto* FitGoal = WidgetTree->ConstructWidget<UScaleBox>();
	FitGoal->SetStretch(EStretch::ScaleToFit);
	FitGoal->SetStretchDirection(EStretchDirection::DownOnly);
	FitGoal->SetContent(HelpGoal);
	auto* GoalSlot = CastChecked<UScaleBoxSlot>(HelpGoal->Slot);
	GoalSlot->SetHorizontalAlignment(HAlign_Right);
	GoalSlot->SetVerticalAlignment(VAlign_Center);
	Place(FitGoal, 698, 24, 802, 57);

	auto* Margin = WidgetTree->ConstructWidget<UBorder>();
	Margin->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White, 0.f, FLinearColor::FromSRGBColor(FColor(221, 218, 212)), 1.f));
	Margin->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(Margin, 563.5f, 111.7f, 473, 705.4f);
	HelpComic = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("HelpComic"));
	HelpComic->SetVisibility(ESlateVisibility::HitTestInvisible);
	auto* FitComic = WidgetTree->ConstructWidget<UScaleBox>();
	FitComic->SetStretch(EStretch::ScaleToFit);
	FitComic->SetContent(HelpComic);
	Place(FitComic, 574, 111.7f, 452, 705.4f);
	auto* Credit = Label(TBText::Get(TEXT("Root.Help.BudgetNote")), 12, TBPaper::Muted, false, TEXT("HelpCredit"));
	Place(Credit, 100, 844, 1020, 35);
	CloseHelpButton = GuideButton(TEXT("CloseHelp"), TEXT("Root.Help.Close"), 1364, 834, 136);
	CloseHelpButton->SetStyle(TBPaper::PrintButtonStyle(TBPaper::PrintHoney));
	CloseHelpButton->OnClicked.AddDynamic(this, &ThisClass::CloseHelp);
	HelpOverlay->SetVisibility(ESlateVisibility::Collapsed);
	RefreshHelpText();
}

void UTBTitleMenu::AskQuit()
{
	TitleContent->SetIsEnabled(false);
	QuitOverlay->SetVisibility(ESlateVisibility::Visible);
	CancelQuitButton->SetKeyboardFocus();
}

void UTBTitleMenu::CancelQuit()
{
	QuitOverlay->SetVisibility(ESlateVisibility::Collapsed);
	TitleContent->SetIsEnabled(true);
	QuitButton->SetKeyboardFocus();
}

void UTBTitleMenu::Quit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UTBTitleMenu::RefreshHelpText()
{
	HelpToyTab->SetStyle(TBPaper::PrintButtonStyle(bHumanHelp ? TBPaper::PrintCream : TBPaper::PrintHoney));
	HelpHumanTab->SetStyle(TBPaper::PrintButtonStyle(bHumanHelp ? TBPaper::PrintAqua : TBPaper::PrintCream));
	HelpGoal->SetText(TBText::Get(bHumanHelp ? TEXT("Root.Help.HumanGoal") : TEXT("Root.Help.ToyGoal")));
	HelpComic->SetBrushFromTexture(TBPaper::Art(bHumanHelp ? TEXT("GuideHuman") : TEXT("GuideToy")), true);
}

void UTBTitleMenu::ShowToyHelp()
{
	bHumanHelp = false;
	RefreshHelpText();
	HelpToyTab->SetKeyboardFocus();
}

void UTBTitleMenu::ShowHumanHelp()
{
	bHumanHelp = true;
	RefreshHelpText();
	HelpHumanTab->SetKeyboardFocus();
}

void UTBTitleMenu::Help()
{
	TitleContent->SetVisibility(ESlateVisibility::Collapsed);
	HelpOverlay->SetVisibility(ESlateVisibility::Visible);
	ShowToyHelp();
}

void UTBTitleMenu::CloseHelp()
{
	HelpOverlay->SetVisibility(ESlateVisibility::Collapsed);
	TitleContent->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	HelpButton->SetKeyboardFocus();
}

FReply UTBTitleMenu::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (UTBLoadingScreenSubsystem::IsCovering(GetGameInstance())) return FReply::Handled();
	// 音量・再生などのメディアキーは UE のキーにない（無効なキーで届く）。これでは飛ばさない。
	if (State == EState::Story && KeyEvent.GetKey().IsValid())
	{
		SkipStory();
		return FReply::Handled();
	}
	NoteInput();
	if (HelpOverlay && HelpOverlay->GetVisibility() == ESlateVisibility::Visible)
	{
		const FKey Key = KeyEvent.GetKey();
		if (Key == EKeys::Escape)
		{
			CloseHelp();
			return FReply::Handled();
		}
		if (HelpToyTab->HasKeyboardFocus() || HelpHumanTab->HasKeyboardFocus())
		{
			if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left) { ShowToyHelp(); return FReply::Handled(); }
			if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right) { ShowHumanHelp(); return FReply::Handled(); }
		}
	}
	if (QuitOverlay->GetVisibility() == ESlateVisibility::Visible && KeyEvent.GetKey() == EKeys::Escape)
	{
		CancelQuit();
		return FReply::Handled();
	}
	if (bRoomCardsShown && KeyEvent.GetKey() == EKeys::Escape)
	{
		CloseFinder();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
}
