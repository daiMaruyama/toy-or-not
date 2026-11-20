// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "UI/TBOptionsMenu.h"
#include "Audio/TBAudioSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/TBGameUserSettings.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "Input/TBKeyLabels.h"
#include "Input/TBKeyPrompt.h"
#include "Input/TBKeyRemap.h"
#include "InputCoreTypes.h"
#include "TBKeyRow.h"
#include "TBOptionRow.h"
#include "TBOptionsPages.h"
#include "TBPaperButton.h"
#include "TBPaperStyle.h"
#include "TBUIText.h"

UTBOptionsMenu* UTBOptionsMenu::Open(APlayerController* Player, TFunction<void()> OnClosed)
{
	auto* Menu = Player ? CreateWidget<UTBOptionsMenu>(Player) : nullptr;
	if (!Menu)
	{
		return nullptr;
	}
	Menu->Closed = MoveTemp(OnClosed);
	Menu->AddToViewport(50);
	Menu->SetKeyboardFocus();
	return Menu;
}

void UTBOptionsMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	if (auto* Settings = UTBGameUserSettings::Get())
	{
		Build(*Settings);
		ShowPage(0);
		LanguageChangedHandle = TBText::OnLanguageChanged().AddUObject(this, &ThisClass::RefreshLocalizedText);
	}
}

void UTBOptionsMenu::Build(UTBGameUserSettings& Settings)
{
	PaperFont = TBPaper::Font(this);
	HeadingFont = TBPaper::Font(this, TBPaper::EFontRole::Heading);
	NumericFont = TBPaper::Font(this, TBPaper::EFontRole::Numeric);
	auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Root;
	auto Fill = [&](UWidget* Widget)
	{
		auto* Slot = Root->AddChildToCanvas(Widget);
		Slot->SetAnchors(FAnchors(0, 0, 1, 1));
		Slot->SetOffsets(FMargin(0));
	};
	auto* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>();
	Blur->SetBlurStrength(2.f);
	Blur->SetVisibility(ESlateVisibility::HitTestInvisible);
	Fill(Blur);
	auto* Dim = WidgetTree->ConstructWidget<UImage>();
	Dim->SetColorAndOpacity(FLinearColor::FromSRGBColor(FColor(56, 45, 38)).CopyWithNewOpacity(.44f));
	Fill(Dim);
	auto* Scale = WidgetTree->ConstructWidget<UScaleBox>();
	Scale->SetStretch(EStretch::ScaleToFit);
	Fill(Scale);
	auto* Size = WidgetTree->ConstructWidget<USizeBox>();
	Size->SetWidthOverride(1440);
	Size->SetHeightOverride(850);
	Scale->SetContent(Size);
	auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	Size->SetContent(Canvas);
	auto Place = [](UCanvasPanel* Panel, UWidget* Widget, float X, float Y, float W, float H)
	{
		auto* Slot = Panel->AddChildToCanvas(Widget);
		Slot->SetPosition(FVector2D(X, Y));
		Slot->SetSize(FVector2D(W, H));
	};
	auto Label = [this](const FText& Text, int32 FontSize, FLinearColor Color) { return MakeLabel(Text, FontSize, Color); };
	auto AddButton = [&](UCanvasPanel* Panel, const FText& Text, float X, float Y, float W, FLinearColor Tint = TBPaper::Paper)
	{
		auto* Button = WidgetTree->ConstructWidget<UButton>();
		Button->SetStyle(TBPaper::SignButtonStyle(Tint));
		auto* Caption = Label(Text, 11, TBPaper::Ink);
		Caption->SetFont(FSlateFontInfo(HeadingFont, 11));
		Caption->SetJustification(ETextJustify::Center);
		TBPaper::FitSignCaption(*WidgetTree, *Button, *Caption);
		Place(Panel, Button, X, Y, W, 40);
		return Button;
	};
	const FLinearColor CardPaper = FLinearColor::FromSRGBColor(FColor(255, 240, 210));
	const FLinearColor CardEdge = FLinearColor::FromSRGBColor(FColor(72, 43, 26));
	const FLinearColor RuleColor = FLinearColor::FromSRGBColor(FColor(200, 168, 128));
	const FLinearColor FooterColor = FLinearColor::FromSRGBColor(FColor(240, 220, 183));
	const FSlateRoundedBoxBrush CardBrush(CardPaper, 14.f, CardEdge, 3.f);
	auto PanelFace = [&](UCanvasPanel* Panel, const FSlateBrush& Brush, float X, float Y, float Width, float Height)
	{
		auto* Face = WidgetTree->ConstructWidget<UBorder>();
		Face->SetBrush(Brush);
		Face->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Panel, Face, X, Y, Width, Height);
	};
	PanelFace(Canvas, FSlateRoundedBoxBrush(TBPaper::Shadow, 14.f), 342, 152, 760, 560);
	PanelFace(Canvas, FSlateRoundedBoxBrush(FooterColor, 14.f, CardEdge, 2.f), 340, 149, 760, 560);
	PanelFace(Canvas, CardBrush, 340, 145, 760, 560);
	PanelFace(Canvas, FSlateRoundedBoxBrush(FooterColor, FVector4(0, 0, 11, 11)), 343, 633, 754, 69);
	PanelFace(Canvas, FSlateRoundedBoxBrush(RuleColor, 0.f), 343, 633, 754, 1);
	auto* SettingsIcon = WidgetTree->ConstructWidget<UImage>();
	SettingsIcon->SetBrushFromTexture(TBPaper::Art(TEXT("Icon_Settings")));
	SettingsIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
	SettingsIcon->SetRenderTransformAngle(-8.f);
	Place(Canvas, SettingsIcon, 369, 167, 34, 34);
	auto* Title = Label(TBText::Get(TEXT("Options.Title")), 21, TBPaper::Ink);
	Title->SetFont(FSlateFontInfo(HeadingFont, 21));
	// Let each translation determine where the header rule begins.
	auto* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	auto* TitleSlot = Header->AddChildToHorizontalBox(Title);
	TitleSlot->SetVerticalAlignment(VAlign_Center);
	auto* HeaderRule = WidgetTree->ConstructWidget<UBorder>();
	HeaderRule->SetBrush(FSlateRoundedBoxBrush(RuleColor, 1.f));
	auto* RuleHeight = WidgetTree->ConstructWidget<USizeBox>();
	RuleHeight->SetHeightOverride(2);
	RuleHeight->SetContent(HeaderRule);
	auto* HeaderRuleSlot = Header->AddChildToHorizontalBox(RuleHeight);
	HeaderRuleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	HeaderRuleSlot->SetPadding(FMargin(16, 0, 0, 0));
	HeaderRuleSlot->SetVerticalAlignment(VAlign_Center);
	Place(Canvas, Header, 413, 163, 659, 45);
	const TCHAR* TabKeys[] = {TEXT("Options.Audio"), TEXT("Options.Display"), TEXT("Options.Controls"), TEXT("Options.Language")};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(TabKeys); ++Index)
	{
		auto* Tab = AddButton(Canvas, TBText::Get(TabKeys[Index]), 369 + Index * 178, 226, 170);
		Tabs.Add(Tab);
	}
	Tabs[0]->OnClicked.AddDynamic(this, &ThisClass::ShowAudio);
	Tabs[1]->OnClicked.AddDynamic(this, &ThisClass::ShowDisplay);
	Tabs[2]->OnClicked.AddDynamic(this, &ThisClass::ShowControls);
	Tabs[3]->OnClicked.AddDynamic(this, &ThisClass::ShowLanguage);
	for (int32 Index = 0; Index < Tabs.Num(); ++Index)
	{
		auto* Box = WidgetTree->ConstructWidget<UVerticalBox>();
		Pages.Add(Box);
		// 画面設定の長い説明も、キー一覧と同じスクロール領域に収める。
		auto* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
		Scroll->SetScrollbarThickness(FVector2D(6, 6));
		Scroll->SetScrollbarPadding(FMargin(10, 0, 0, 0));
		FScrollBoxStyle ScrollStyle = Scroll->GetWidgetStyle();
		ScrollStyle.SetTopShadowBrush(FSlateNoResource()).SetBottomShadowBrush(FSlateNoResource());
		Scroll->SetWidgetStyle(ScrollStyle);
		FScrollBarStyle BarStyle = Scroll->GetWidgetBarStyle();
		BarStyle.SetNormalThumbImage(FSlateRoundedBoxBrush(TBPaper::Teal, 3.f));
		BarStyle.SetHoveredThumbImage(FSlateRoundedBoxBrush(TBPaper::Teal, 3.f));
		BarStyle.SetDraggedThumbImage(FSlateRoundedBoxBrush(TBPaper::Teal, 3.f));
		BarStyle.SetVerticalBackgroundImage(FSlateRoundedBoxBrush(TBPaper::Outline.CopyWithNewOpacity(.08f), 3.f));
		Scroll->SetWidgetBarStyle(BarStyle);
		Scroll->AddChild(Box);
		Place(Canvas, Scroll, 366, 278, 708, 343);
		PageRoots.Add(Scroll);
		if (Index == TBOptionsPage::Controls) ControlsScroll = Scroll;
	}
	ResetButton = AddButton(Canvas, TBText::Get(TEXT("Options.ResetAll")), 367, 647, 132);
	ResetButton->OnClicked.AddDynamic(this, &ThisClass::ResetPage);
	CloseButton = AddButton(Canvas, TBText::Get(TEXT("Options.Close")), 965, 647, 107, TBPaper::Teal);
	CloseButton->OnClicked.AddDynamic(this, &ThisClass::Close);

	TBOptionsPages::BuildAudio(*this, Settings);
	TBOptionsPages::BuildDisplay(*this, Settings);
	TBOptionsPages::BuildControls(*this, Settings);
	auto* Languages = WidgetTree->ConstructWidget<UCanvasPanel>();
	auto* LanguageSize = WidgetTree->ConstructWidget<USizeBox>();
	LanguageSize->SetHeightOverride(100);
	LanguageSize->SetContent(Languages);
	Pages[TBOptionsPage::Language]->AddChildToVerticalBox(LanguageSize);
	auto* LanguageHeading = Label(TBText::Get(TEXT("Options.DisplayLanguage")), 11, TBPaper::Ink);
	LanguageHeading->SetFont(FSlateFontInfo(HeadingFont, 11));
	Place(Languages, LanguageHeading, 6, 8, 420, 22);
	JapaneseButton = AddButton(Languages, TBText::Get(TEXT("Language.Japanese")), 3, 38, 339);
	EnglishButton = AddButton(Languages, TBText::Get(TEXT("Language.English")), 352, 38, 339);
	JapaneseButton->OnClicked.AddDynamic(this, &ThisClass::SelectJapanese);
	EnglishButton->OnClicked.AddDynamic(this, &ThisClass::SelectEnglish);
	// The bundled display face has no check character. Draw the selection mark at UI resolution.
	auto* Check = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LanguageSelectionMark"));
	Check->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(Languages, Check, 131, 49, 15, 15);
	auto Stroke = [&](float X, float Y, float Length, float Angle)
	{
		auto* Line = WidgetTree->ConstructWidget<UBorder>();
		Line->SetBrush(FSlateRoundedBoxBrush(TBPaper::Ink, 1.f));
		Line->SetRenderTransformAngle(Angle);
		Place(Check, Line, X, Y, Length, 2.3f);
	};
	Stroke(0, 8, 6.5f, 45.f);
	Stroke(3.2f, 6.6f, 12.f, -46.f);
	LanguageCheck = Check;

	// 解像度と画面モードを変えたあとの確認。
	auto* Confirm = WidgetTree->ConstructWidget<UCanvasPanel>();
	Place(Canvas, Confirm, 0, 0, 1440, 850);
	auto* ConfirmDim = WidgetTree->ConstructWidget<UImage>();
	ConfirmDim->SetColorAndOpacity(TBPaper::Ink.CopyWithNewOpacity(.66f));
	Place(Confirm, ConfirmDim, 0, 0, 1440, 850);
	PanelFace(Confirm, CardBrush, 492, 318, 456, 214);
	PanelFace(Confirm, FSlateRoundedBoxBrush(FooterColor, FVector4(0, 0, 11, 11)), 495, 457, 450, 72);
	auto* ConfirmHeading = Label(TBText::Get(TEXT("Options.ConfirmTitle")), 17, TBPaper::Ink);
	ConfirmHeading->SetFont(FSlateFontInfo(HeadingFont, 17));
	Place(Confirm, ConfirmHeading, 517, 338, 406, 36);
	ConfirmText = Label(FText::GetEmpty(), 14, TBPaper::Ink);
	ConfirmText->SetAutoWrapText(true);
	Place(Confirm, ConfirmText, 517, 386, 406, 62);
	KeepButton = AddButton(Confirm, TBText::Get(TEXT("Options.Keep")), 762, 473, 161, TBPaper::Teal);
	KeepButton->OnClicked.AddDynamic(this, &ThisClass::KeepVideoMode);
	AddButton(Confirm, TBText::Get(TEXT("Options.Revert")), 628, 473, 125)->OnClicked.AddDynamic(this, &ThisClass::RevertVideoMode);
	ConfirmOverlay = Confirm;
	ConfirmOverlay->SetVisibility(ESlateVisibility::Collapsed);

	// キーの付け替えの板。暗い幕を押せる（Visible）にして、後ろの行やボタンを押せなくする。
	auto* KeyLayer = WidgetTree->ConstructWidget<UCanvasPanel>();
	Place(Canvas, KeyLayer, 0, 0, 1440, 850);
	auto* KeyDim = WidgetTree->ConstructWidget<UImage>();
	KeyDim->SetColorAndOpacity(TBPaper::Ink.CopyWithNewOpacity(.66f));
	KeyDim->SetVisibility(ESlateVisibility::Visible);
	Place(KeyLayer, KeyDim, 0, 0, 1440, 850);
	PanelFace(KeyLayer, CardBrush, 492, 293, 456, 264);
	PanelFace(KeyLayer, FSlateRoundedBoxBrush(FooterColor, FVector4(0, 0, 11, 11)), 495, 482, 450, 72);
	auto* KeyHeading = Label(TBText::Get(TEXT("Keys.Title")), 17, TBPaper::Ink);
	KeyHeading->SetFont(FSlateFontInfo(HeadingFont, 17));
	Place(KeyLayer, KeyHeading, 517, 313, 406, 36);
	KeyPromptText = Label(FText::GetEmpty(), 14, TBPaper::Ink);
	KeyPromptText->SetAutoWrapText(true);
	Place(KeyLayer, KeyPromptText, 517, 358, 406, 64);
	KeyErrorText = Label(FText::GetEmpty(), 12, TBPaper::Danger);
	KeyErrorText->SetAutoWrapText(true);
	Place(KeyLayer, KeyErrorText, 517, 430, 406, 46);
	KeySwapButton = AddButton(KeyLayer, TBText::Get(TEXT("Keys.Swap")), 773, 498, 150, TBPaper::Teal);
	KeySwapButton->OnClicked.AddDynamic(this, &ThisClass::SwapKey);
	KeyCancelButton = AddButton(KeyLayer, TBText::Get(TEXT("Keys.Cancel")), 635, 498, 128);
	KeyCancelButton->OnClicked.AddDynamic(this, &ThisClass::CancelKey);
	KeyCloseButton = AddButton(KeyLayer, TBText::Get(TEXT("Keys.Close")), 795, 498, 128, TBPaper::Teal);
	KeyCloseButton->OnClicked.AddDynamic(this, &ThisClass::CancelKey);
	KeyOverlay = KeyLayer;
	KeyOverlay->SetVisibility(ESlateVisibility::Collapsed);
}

UTBOptionRow* UTBOptionsMenu::AddRow(int32 PageIndex, const FText& Label, bool bBar)
{
	auto* Row = CreateWidget<UTBOptionRow>(this);
	Row->Build(Label, HeadingFont, NumericFont, bBar);
	// 変えたあとの音量・設定で鳴らす。ほかの行の表示も変わることがあるので、全部の行を更新する。
	Row->Changed = [this]
	{
		PlayClick();
		RefreshRows();
	};
	Pages[PageIndex]->AddChildToVerticalBox(Row);
	Rows.Add(Row);
	return Row;
}

void UTBOptionsMenu::AddNote(int32 PageIndex, const FText& Text)
{
	auto* Note = MakeLabel(Text, 11, TBPaper::Muted);
	Note->SetAutoWrapText(true);
	Note->SetWrapTextAt(672);
	Pages[PageIndex]->AddChildToVerticalBox(Note)->SetPadding(FMargin(8, 10, 8, 8));
}

void UTBOptionsMenu::AddHeading(int32 PageIndex, const FText& Text)
{
	auto* Heading = MakeLabel(Text, 12, TBPaper::Ink);
	Heading->SetColorAndOpacity(FLinearColor::FromSRGBColor(FColor(35, 107, 96)));
	Heading->SetFont(FSlateFontInfo(HeadingFont, 12));
	Pages[PageIndex]->AddChildToVerticalBox(Heading)->SetPadding(FMargin(6, 16, 0, 5));
}

UTBKeyRow* UTBOptionsMenu::AddKeyRow(int32 PageIndex, const FText& Label, FName MappingName)
{
	auto* Row = CreateWidget<UTBKeyRow>(this);
	Row->Build(Label, MappingName, PaperFont, HeadingFont);
	Row->Pressed = [this, MappingName] { BeginKeyCapture(MappingName); };
	Row->Reset = [this, MappingName] { BeginKeyReset(MappingName); };
	Pages[PageIndex]->AddChildToVerticalBox(Row);
	KeyRows.Add(Row);
	return Row;
}

UTextBlock* UTBOptionsMenu::MakeLabel(const FText& Text, int32 FontSize, FLinearColor Color)
{
	auto* Block = WidgetTree->ConstructWidget<UTextBlock>();
	Block->SetText(Text);
	Block->SetFont(FSlateFontInfo(PaperFont, FontSize));
	Block->SetColorAndOpacity(Color);
	return Block;
}

void UTBOptionsMenu::ShowPage(int32 Index)
{
	Page = FMath::Clamp(Index, 0, Pages.Num() - 1);
	for (int32 Other = 0; Other < PageRoots.Num(); ++Other)
	{
		PageRoots[Other]->SetVisibility(Other == Page ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		Tabs[Other]->SetStyle(TBPaper::SignButtonStyle(Other == Page ? TBPaper::Gold : TBPaper::Paper));
		Tabs[Other]->SetRenderTransformAngle(Other == Page ? -.9f : 0.f);
		Tabs[Other]->SetRenderTranslation(FVector2D(0, Other == Page ? -2.f : 0.f));
	}
	if (Page == TBOptionsPage::Controls && ControlsScroll)
	{
		ControlsScroll->ScrollToStart();
	}
	if (Page == TBOptionsPage::Language)
	{
		const bool bEnglish = TBText::GetLanguage() == TEXT("en");
		JapaneseButton->SetStyle(TBPaper::SignButtonStyle(bEnglish ? TBPaper::Paper : TBPaper::Teal));
		EnglishButton->SetStyle(TBPaper::SignButtonStyle(bEnglish ? TBPaper::Teal : TBPaper::Paper));
		LanguageCheck->SetRenderTranslation(FVector2D(bEnglish ? 349.f : 0.f, 0));
	}
	RefreshRows();
}

void UTBOptionsMenu::ShowAudio()
{
	if (IsCapturing()) return;
	PlayClick();
	ShowPage(0);
}

void UTBOptionsMenu::ShowDisplay()
{
	if (IsCapturing()) return;
	PlayClick();
	ShowPage(1);
}

void UTBOptionsMenu::ShowControls()
{
	if (IsCapturing()) return;
	PlayClick();
	ShowPage(2);
}

void UTBOptionsMenu::ShowLanguage()
{
	if (IsCapturing()) return;
	PlayClick();
	ShowPage(TBOptionsPage::Language);
}

void UTBOptionsMenu::SelectJapanese() { SelectLanguage(TEXT("ja")); }
void UTBOptionsMenu::SelectEnglish() { SelectLanguage(TEXT("en")); }

void UTBOptionsMenu::SelectLanguage(const TCHAR* Code)
{
	auto* Settings = UTBGameUserSettings::Get();
	if (!Settings || IsCapturing() || IsConfirming()) return;
	Settings->SetLanguage(Code);
	Settings->CommitLanguage();
	PlayClick();
}

void UTBOptionsMenu::RefreshLocalizedText()
{
	ShowPage(Page);
}

void UTBOptionsMenu::RefreshRows()
{
	for (UTBOptionRow* Row : Rows)
	{
		Row->Refresh();
	}
	for (UTBKeyRow* Row : KeyRows)
	{
		Row->Refresh();
	}
}

void UTBOptionsMenu::ResetPage()
{
	auto* Settings = UTBGameUserSettings::Get();
	if (!Settings || IsConfirming() || IsCapturing())
	{
		return;
	}
	if (Page == TBOptionsPage::Audio)
	{
		Settings->ResetVolumes();
		Settings->CommitVolumes(GetOwningPlayer() ? GetOwningPlayer()->GetGameInstance() : nullptr);
	}
	else if (Page == TBOptionsPage::Display)
	{
		Settings->ResetDisplay();
		Settings->CommitDisplay();
	}
	else if (Page == TBOptionsPage::Language)
	{
		Settings->SetLanguage(TEXT("ja"));
		Settings->CommitLanguage();
	}
	else
	{
		Settings->ResetControls();
		Settings->SaveSettings();
		if (TB::ResetAll(GetOwningPlayer()).Result == TB::ERemapResult::OK)
		{
			SaveKeys();
		}
	}
	PlayClick();
	RefreshRows();
}

void UTBOptionsMenu::ChangeVideoMode(TFunctionRef<void(UTBGameUserSettings&)> Change)
{
	auto* Settings = UTBGameUserSettings::Get();
	if (!Settings || IsCapturing())
	{
		return;
	}
	PreviousResolution = Settings->GetScreenResolution();
	PreviousWindowMode = Settings->GetFullscreenMode();
	Change(*Settings);
	Settings->ApplyResolutionSettings(false);
	ConfirmUntil = FPlatformTime::Seconds() + TB::ConfigData().Display.VideoModeConfirmSeconds;
	ConfirmOverlay->SetVisibility(ESlateVisibility::Visible);
	SetBackgroundEnabled(false);
	KeepButton->SetKeyboardFocus();
}

bool UTBOptionsMenu::IsConfirming() const
{
	return ConfirmOverlay && ConfirmOverlay->GetVisibility() == ESlateVisibility::Visible;
}

void UTBOptionsMenu::KeepVideoMode()
{
	if (auto* Settings = UTBGameUserSettings::Get())
	{
		Settings->ConfirmVideoMode();
		Settings->SaveSettings();
	}
	ConfirmOverlay->SetVisibility(ESlateVisibility::Collapsed);
	SetBackgroundEnabled(true);
	PlayClick();
	RefreshRows();
	SetKeyboardFocus();
}

void UTBOptionsMenu::RestoreVideoMode(UTBGameUserSettings& Settings) const
{
	Settings.SetScreenResolution(PreviousResolution);
	Settings.SetFullscreenMode(PreviousWindowMode);
	Settings.ApplyResolutionSettings(false);
	Settings.ConfirmVideoMode();
}

void UTBOptionsMenu::RevertVideoMode()
{
	if (auto* Settings = UTBGameUserSettings::Get())
	{
		RestoreVideoMode(*Settings);
	}
	ConfirmOverlay->SetVisibility(ESlateVisibility::Collapsed);
	SetBackgroundEnabled(true);
	PlayClick(true);
	RefreshRows();
	SetKeyboardFocus();
}

void UTBOptionsMenu::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (!IsConfirming())
	{
		return;
	}
	const double Left = ConfirmUntil - FPlatformTime::Seconds();
	if (Left <= 0)
	{
		RevertVideoMode();
		return;
	}
	ConfirmText->SetText(FText::Format(TBText::Get(TEXT("Options.ConfirmCountdown")), FMath::CeilToInt(Left)));
}

void UTBOptionsMenu::BeginKeyCapture(FName MappingName)
{
	if (IsConfirming() || IsCapturing())
	{
		return;
	}
	CaptureRow = MappingName;
	PendingKey = EKeys::Invalid;
	PlayClick();
	SetKeyOverlay(EKeyCapture::Waiting, TB::WaitingText(MappingName));
	// 後ろを押せなくしてから、待っている間のキーを受けるメニューにフォーカスを移す。
	SetBackgroundEnabled(false);
	SetKeyboardFocus();
}

TFunction<void()> UTBOptionsMenu::KeySaveOverride;

void UTBOptionsMenu::SaveKeys() const
{
	if (KeySaveOverride)
	{
		KeySaveOverride();
		return;
	}
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	auto* Input = LocalPlayer ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer) : nullptr;
	if (auto* User = Input ? Input->GetUserSettings() : nullptr)
	{
		User->SaveSettings();
	}
}

void UTBOptionsMenu::BeginKeyReset(FName MappingName)
{
	if (IsConfirming() || IsCapturing())
	{
		return;
	}
	TB::FKeyboardKey Row;
	if (!TB::FindKeyboardKey(GetOwningPlayer(), MappingName, Row))
	{
		return;
	}
	const TB::FKeyPrompt Prompt = TB::PromptForReset(TB::ResetToDefault(GetOwningPlayer(), MappingName), Row.DefaultKey);
	PlayClick();
	if (Prompt.Kind == TB::EKeyPromptKind::Done)
	{
		SaveKeys();
		RefreshRows();
		return;
	}
	// 戻せなかったとき。かぶりが1行なら入れ替えを聞き、それ以外はお知らせ。
	CaptureRow = MappingName;
	PendingKey = Row.DefaultKey;
	bResetting = true;
	SetBackgroundEnabled(false);
	SetKeyboardFocus();
	ApplyKeyPrompt(Prompt.Kind == TB::EKeyPromptKind::AskSwap ? Prompt : TB::FKeyPrompt{TB::EKeyPromptKind::Notice, Prompt.Text},
	               Row.DefaultKey);
}

void UTBOptionsMenu::ReceiveCapturedKey(const FKey& Key)
{
	if (KeyCapture == EKeyCapture::None)
	{
		return;
	}
	// このキーを押したままのリピートとKeyUpは、メニューの先読みで止める（マウスのボタンにKeyUpは来ない）。
	if (!Key.IsMouseButton())
	{
		HeldTriggerKey = Key;
	}
	// Escとパッドの下のボタン（B）。お知らせは閉じてキー待ちに戻り、それ以外は取り消す。
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right)
	{
		if (KeyCapture == EKeyCapture::Notice)
		{
			ReturnToWaiting();
		}
		else
		{
			CancelKeyCapture();
		}
		return;
	}
	if (KeyCapture != EKeyCapture::Waiting)
	{
		return;
	}
	ApplyKeyPrompt(TB::PromptForRemap(TB::Remap(GetOwningPlayer(), CaptureRow, Key), Key), Key);
}

void UTBOptionsMenu::ConfirmKeySwap()
{
	if (KeyCapture != EKeyCapture::AskSwap)
	{
		return;
	}
	const TB::FKeyPrompt Prompt = TB::PromptForSwap(TB::SwapRemap(GetOwningPlayer(), CaptureRow, PendingKey));
	if (Prompt.Kind == TB::EKeyPromptKind::Redo)
	{
		// 相手がいなくなっていた。普通の付け替えをやり直す。
		ApplyKeyPrompt(TB::PromptForRemap(TB::Remap(GetOwningPlayer(), CaptureRow, PendingKey), PendingKey), PendingKey);
		return;
	}
	ApplyKeyPrompt(Prompt, PendingKey);
}

void UTBOptionsMenu::ApplyKeyPrompt(const TB::FKeyPrompt& Prompt, const FKey& Key)
{
	switch (Prompt.Kind)
	{
	case TB::EKeyPromptKind::Done:
		SaveKeys();
		CancelKeyCapture(false);
		RefreshRows();
		break;
	case TB::EKeyPromptKind::Retry:
		SetKeyOverlay(EKeyCapture::Waiting, TB::WaitingText(CaptureRow), Prompt.Text);
		break;
	case TB::EKeyPromptKind::AskSwap:
		PendingKey = Key;
		SetKeyOverlay(EKeyCapture::AskSwap, Prompt.Text);
		// 押し間違いで入れ替わらないよう、初めは「やめる」にフォーカスを置く。
		KeyCancelButton->SetKeyboardFocus();
		break;
	default:
		SetKeyOverlay(EKeyCapture::Notice, Prompt.Text);
		KeyCloseButton->SetKeyboardFocus();
		break;
	}
}

void UTBOptionsMenu::CancelKeyCapture(bool bBack)
{
	if (KeyCapture == EKeyCapture::None)
	{
		return;
	}
	KeyCapture = EKeyCapture::None;
	bResetting = false;
	KeyOverlay->SetVisibility(ESlateVisibility::Collapsed);
	SetBackgroundEnabled(true);
	PlayClick(bBack);
	// 押した行の「変える」にフォーカスを戻す。
	if (auto* Row = KeyRows.FindByPredicate([this](const UTBKeyRow* Other) { return Other->GetMappingName() == CaptureRow; }))
	{
		(*Row)->FocusButton();
	}
	else
	{
		SetKeyboardFocus();
	}
}

void UTBOptionsMenu::ReturnToWaiting()
{
	if (KeyCapture == EKeyCapture::None)
	{
		return;
	}
	if (bResetting)
	{
		CancelKeyCapture();
		return;
	}
	PlayClick(true);
	SetKeyOverlay(EKeyCapture::Waiting, TB::WaitingText(CaptureRow));
	SetKeyboardFocus();
}

void UTBOptionsMenu::SetBackgroundEnabled(bool bEnabled)
{
	for (UWidget* Root : PageRoots)
	{
		Root->SetIsEnabled(bEnabled);
	}
	for (UButton* Tab : Tabs)
	{
		Tab->SetIsEnabled(bEnabled);
	}
	ResetButton->SetIsEnabled(bEnabled);
	CloseButton->SetIsEnabled(bEnabled);
}

bool UTBOptionsMenu::IsBackgroundEnabled() const
{
	return CloseButton && CloseButton->GetIsEnabled();
}

void UTBOptionsMenu::SetKeyOverlay(EKeyCapture State, const FString& Text, const FString& Error)
{
	KeyCapture = State;
	KeyPromptText->SetText(FText::FromString(Text));
	KeyErrorText->SetText(FText::FromString(Error));
	const bool bCancelable = State == EKeyCapture::Waiting || State == EKeyCapture::AskSwap;
	KeySwapButton->SetVisibility(State == EKeyCapture::AskSwap ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	KeyCancelButton->SetVisibility(bCancelable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	KeyCloseButton->SetVisibility(State == EKeyCapture::Notice ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	KeyOverlay->SetVisibility(ESlateVisibility::Visible);
}

FString UTBOptionsMenu::GetKeyPromptText() const
{
	return KeyPromptText ? KeyPromptText->GetText().ToString() : FString();
}

FString UTBOptionsMenu::GetKeyErrorText() const
{
	return KeyErrorText ? KeyErrorText->GetText().ToString() : FString();
}

void UTBOptionsMenu::SwapKey()
{
	ConfirmKeySwap();
}

void UTBOptionsMenu::CancelKey()
{
	// お知らせは閉じてキー待ちに戻る。待ちと入れ替えの確認では、取り消す。
	if (KeyCapture == EKeyCapture::Notice)
	{
		ReturnToWaiting();
		return;
	}
	CancelKeyCapture();
}

FReply UTBOptionsMenu::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent)
{
	// 待っている間は、マウスのボタンも次のキーとして受け、ゲームや後ろの画面へ渡さない。
	// 「やめる」ボタンの上のクリックだけは割り当てに使わず、ボタンに渡す。
	if (KeyCapture == EKeyCapture::Waiting &&
	    !KeyCancelButton->GetCachedGeometry().IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
	{
		ReceiveCapturedKey(MouseEvent.GetEffectingButton());
		return FReply::Handled();
	}
	return Super::NativeOnPreviewMouseButtonDown(Geometry, MouseEvent);
}

FReply UTBOptionsMenu::NativeOnKeyUp(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (HeldTriggerKey.IsValid() && KeyEvent.GetKey() == HeldTriggerKey)
	{
		HeldTriggerKey = EKeys::Invalid;
		return FReply::Handled();
	}
	// 待ちと確認の間に離したキーは、ゲーム側へ渡さない。
	return IsCapturing() ? FReply::Handled() : Super::NativeOnKeyUp(Geometry, KeyEvent);
}

FReply UTBOptionsMenu::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	const FKey Key = KeyEvent.GetKey();
	if (HeldTriggerKey.IsValid() && Key == HeldTriggerKey)
	{
		// 状態を変えたキーを押したままのリピートは止める。押し直したら（リピートでない）新しい押下として扱う。
		if (KeyEvent.IsRepeat())
		{
			return FReply::Handled();
		}
		HeldTriggerKey = EKeys::Invalid;
	}
	if (IsCapturing())
	{
		// 確認とお知らせでは、ボタンを選ぶ・押すキー（左右、Enter、Space、パッドのAと十字）だけ通す。Tabは通さない。
		// 押したままのリピートは通さない。
		const bool bPassToButtons = KeyCapture != EKeyCapture::Waiting && !KeyEvent.IsRepeat() &&
		                            (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Left || Key == EKeys::Right ||
		                             Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Gamepad_DPad_Left ||
		                             Key == EKeys::Gamepad_DPad_Right);
		if (bPassToButtons)
		{
			return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
		}
		if (!KeyEvent.IsRepeat())
		{
			ReceiveCapturedKey(Key);
		}
		return FReply::Handled();
	}
	if (KeyEvent.GetKey() == EKeys::Escape)
	{
		if (Rows.ContainsByPredicate([](const UTBOptionRow* Row) { return Row->IsChoiceOpen(); }))
		{
			return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
		}
		if (IsConfirming())
		{
			RevertVideoMode();
		}
		else
		{
			Close();
		}
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
}

void UTBOptionsMenu::PlayClick(bool bBack) const
{
	const auto* Player = GetOwningPlayer();
	if (auto* Audio = Player && Player->GetGameInstance() ? Player->GetGameInstance()->GetSubsystem<UTBAudioSubsystem>() : nullptr)
	{
		Audio->PlaySoundEvent(bBack ? ETBSoundEvent::UIBack : ETBSoundEvent::UIClick);
	}
}

void UTBOptionsMenu::Close()
{
	if (IsCapturing())
	{
		return;
	}
	PlayClick(true);
	Dismiss();
}

void UTBOptionsMenu::NativeDestruct()
{
	TBText::OnLanguageChanged().Remove(LanguageChangedHandle);
	LanguageChangedHandle.Reset();
	// 確認していない解像度・画面モードは、閉じるときに元に戻す。ステージの移動で画面ごと消えたときも、ここを通る。
	if (IsConfirming())
	{
		if (auto* Settings = UTBGameUserSettings::Get())
		{
			RestoreVideoMode(*Settings);
		}
		ConfirmOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
	Super::NativeDestruct();
}

void UTBOptionsMenu::Dismiss()
{
	RemoveFromParent();
	if (Closed)
	{
		const auto Callback = MoveTemp(Closed);
		Closed = nullptr;
		Callback();
	}
}
