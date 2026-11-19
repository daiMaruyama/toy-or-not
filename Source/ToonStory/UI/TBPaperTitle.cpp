// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "UI/TBUIText.h"
#include "UI/TBTitleMenu.h"
#include "Input/TBKeyLabels.h"
#include "Core/TBSession.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Engine/GameInstance.h"
#include "TBPaperStyle.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScaleBox.h"
#include "Components/Image.h"
#include "Kismet/KismetSystemLibrary.h"
#include "InputCoreTypes.h"

void UTBTitleMenu::BuildPaperLayout()
{
	PaperFont = TBPaper::Font(this);
	LogoFont = TBPaper::Font(this, TBPaper::EFontRole::Heading);
	PaperTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/UI/PaperClub/Panel.Panel"));
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
	auto Label = [&](const FText& Text, int32 FontSize, FLinearColor Color, bool Logo = false)
	{
		auto* L = WidgetTree->ConstructWidget<UTextBlock>();
		L->SetText(Text);
		L->SetFont(FSlateFontInfo(Logo ? LogoFont : PaperFont, FontSize));
		L->SetColorAndOpacity(Color);
		L->SetAutoWrapText(true);
		return L;
	};
	// 仮のロゴ（文字）。本物のロゴは WBP の LogoImage に画像を設定すれば、文字は出さずにそちらを使う。
	// 置き場所は左上、横 6〜40%・縦 9〜54%（1600×900 で x 96〜640、y 81〜486）。
	// Retain authored WBP art; provide the same shipped logo in the native layout.
	if (!LogoImage)
	{
		LogoImage = WidgetTree->ConstructWidget<UImage>();
		LogoImage->SetBrushFromTexture(TBPaper::Art(TEXT("TitleLogo")));
		LogoImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(LogoImage, 80, 20, 560, 560);
	}
	const bool bLogoImage = LogoImage && LogoImage->GetBrush().GetResourceObject();
	if (LogoImage && !bLogoImage)
	{
		LogoImage->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (!bLogoImage)
	{
		auto* Logo = Label(FText::FromString(TEXT("TOY")), 90, TBPaper::Cream, true);
		Logo->SetShadowColorAndOpacity(FLinearColor(.16f, .05f, .025f, 1));
		Logo->SetShadowOffset(FVector2D(4, 5));
		Place(Logo, 100, 100, 520, 130);
		Logo->SetRenderTransformAngle(-5);
		auto* Story = Label(FText::FromString(TEXT("or NOT?")), 90, TBPaper::Accent, true);
		Story->SetShadowColorAndOpacity(FLinearColor(.16f, .05f, .025f, 1));
		Story->SetShadowOffset(FVector2D(4, 5));
		Place(Story, 112, 212, 520, 140);
		Story->SetRenderTransformAngle(-5);
	}
	// デザイナー素材の3状態ボタン。文字は画像に入っている。読み込めなければ nullptr（紙のボタンに戻す）。
	// X、Y、W は、画像の透明な余白を除いた絵そのものの左上と幅。Art は元画像での絵の範囲（左、上、右、下の画素）。
	auto TryImageButton = [&](const TCHAR* Name, const TCHAR* Base, FVector2D PixelSize, FVector4 Art, float X, float Y, float W) -> UButton*
	{
		auto Load = [&](const FString& Name)
		{
			return Cast<UTexture2D>(StaticLoadObject(UTexture2D::StaticClass(), nullptr,
			    *FString::Printf(TEXT("/Game/UI/Designer/Imported/%s.%s"), *Name, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn));
		};
		auto LoadState = [&](const TCHAR* Suffix) { return Load(FString::Printf(TEXT("T_UI_Button_%s_%s"), Base, Suffix)); };
		UTexture2D* Normal = LoadState(TEXT("Normal"));
		UTexture2D* Hover = LoadState(TEXT("Hover"));
		UTexture2D* Pressed = LoadState(TEXT("Pressed"));
		// 3状態の画像が無ければ、1枚絵（歯車・退室）として、乗せたときは明るく、押したときは暗くする。
		const bool bSingle = !Normal;
		FLinearColor HoverTint = FLinearColor::White, PressedTint = FLinearColor::White;
		if (bSingle)
		{
			Normal = Load(FString::Printf(TEXT("T_UI_Button_%s"), Base));
			Hover = Normal;
			Pressed = Normal;
			HoverTint = FLinearColor(1.35f, 1.35f, 1.35f, 1);
			PressedTint = FLinearColor(.6f, .6f, .6f, 1);
		}
		if (!Normal || !Hover || !Pressed)
		{
			return nullptr;
		}
		// 元の比率のまま、絵の幅が W になる倍率で描く。テクスチャの大きさは読み込み直後だと仮の値なので、元画像の寸法を使う。
		const float Scale = W / (Art.Z - Art.X);
		const float DrawW = PixelSize.X * Scale;
		const float DrawH = PixelSize.Y * Scale;
		auto Image = [&](UTexture2D* Texture, FLinearColor Tint = FLinearColor::White)
		{
			FSlateBrush Result;
			Result.SetResourceObject(Texture);
			Result.DrawAs = ESlateBrushDrawType::Image;
			Result.ImageSize = FVector2D(DrawW, DrawH);
			Result.TintColor = Tint;
			return Result;
		};
		auto* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
		FButtonStyle Style;
		Style.SetNormal(Image(Normal));
		Style.SetHovered(Image(Hover, HoverTint));
		Style.SetPressed(Image(Pressed, PressedTint));
		Style.SetDisabled(Image(Normal, FLinearColor(.46f, .46f, .43f)));
		B->SetStyle(Style);
		B->OnHovered.AddDynamic(this, &ThisClass::Hover);
		Place(B, X - Art.X * Scale, Y - Art.Y * Scale, DrawW, DrawH);
		return B;
	};
	auto AddButton = [&](const TCHAR* Name, const FText& Text, float X, float Y, float W, bool Secondary = false,
	                     const TCHAR* ImageBase = nullptr, FVector2D ImagePixels = FVector2D::ZeroVector,
	                     FVector4 ImageArt = FVector4(0, 0, 1, 1))
	{
		if (ImageBase)
		{
			if (auto* ImageButton = TryImageButton(Name, ImageBase, ImagePixels, ImageArt, X, Y, W))
			{
				return ImageButton;
			}
		}
		auto* B = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
		B->SetStyle(TBPaper::CompactButtonStyle(Secondary));
		B->OnHovered.AddDynamic(this, &ThisClass::Hover);
		auto* ButtonLabel = Label(Text, W < 250 ? 15 : 21, TBPaper::Ink, true);
		ButtonLabel->SetAutoWrapText(false);
		B->SetContent(ButtonLabel);
		Place(B, X, Y, W, 64);
		return B;
	};
	// 配置は参照画像（TitleSample.png、793×448）の割合を、1600×900 の座標にしたもの。
	// 数値は、画像の透明な余白を除いた絵の左上（X、Y）と幅（W）。
	const FVector4 WideArt(22, 28, 412, 146);
	const FVector4 IconArt(10, 12, 152, 150);
	// WBP に置かれたボタンはそのまま使う（見た目と位置は WBP のもの）。無ければここで作る。
	auto Adopt = [&](UButton* Button)
	{
		Button->OnHovered.AddDynamic(this, &ThisClass::Hover);
		return Button;
	};
	HostButton = HostButton ? Adopt(HostButton)
	                        : AddButton(TEXT("HostButton"), TBText::Get(TEXT("Root.Title.Host")), 201.6f, 529.2f, 332.8f, false, TEXT("HostRoom"), FVector2D(433, 174), WideArt);
	FindButton = FindButton ? Adopt(FindButton)
	                        : AddButton(TEXT("FindButton"), TBText::Get(TEXT("Root.Title.Find")), 201.6f, 640.9f, 326.4f, true, TEXT("FindRoom"), FVector2D(433, 174), WideArt);
	HelpButton = HelpButton ? Adopt(HelpButton)
	                        : AddButton(TEXT("HelpButton"), TBText::Get(TEXT("Root.Title.Help")), 262.4f, 770.f, 201.6f, true, TEXT("HowToPlay"), FVector2D(260, 105),
	                                    FVector4(12, 8, 250, 95));
	OptionsButton = OptionsButton ? Adopt(OptionsButton)
	                              : AddButton(TEXT("OptionsButton"), TBText::Get(TEXT("Root.Common.Options")), 1307.2f, 747.f, 110.4f, true, TEXT("Gear"), FVector2D(160, 160), IconArt);
	QuitButton = QuitButton ? Adopt(QuitButton)
	                        : AddButton(TEXT("QuitButton"), TBText::Get(TEXT("Root.Title.Quit")), 1446.4f, 747.f, 113.6f, true, TEXT("Leave"), FVector2D(160, 160), IconArt);
	// Preserve the original three-state artwork and authored WBP positions.
	// When copy changes (including localization), cover only its old lettering with
	// a clean strip sampled from the same artwork and put editable FText over it.
	TitleCaptions.Reset();
	auto EditableArtCaption = [&](UButton* Button, const TCHAR* Key, const TCHAR* Original,
	    FVector2D Pixels, FVector4 Bounds, float BlankColumn, int32 FontSize)
	{
		if (!Button->GetStyle().Normal.GetResourceObject()) return;
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormalPadding(FMargin(0));
		Style.SetPressedPadding(FMargin(0));
		Button->SetStyle(Style);
		auto* ScaleCaption = WidgetTree->ConstructWidget<UScaleBox>();
		ScaleCaption->SetStretch(EStretch::ScaleToFill);
		auto* Size = WidgetTree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride(Pixels.X);
		Size->SetHeightOverride(Pixels.Y);
		ScaleCaption->SetContent(Size);
		auto* Layer = WidgetTree->ConstructWidget<UCanvasPanel>();
		Size->SetContent(Layer);
		auto* Patch = WidgetTree->ConstructWidget<UImage>();
		FSlateBrush Brush = TBPaper::ImageBrush(Cast<UTexture2D>(Style.Normal.GetResourceObject()));
		Brush.SetUVRegion(FBox2f(FVector2f(BlankColumn / Pixels.X, Bounds.Y / Pixels.Y),
		    FVector2f((BlankColumn + 1) / Pixels.X, Bounds.W / Pixels.Y)));
		Patch->SetBrush(Brush);
		auto* PatchSlot = Layer->AddChildToCanvas(Patch);
		PatchSlot->SetPosition(FVector2D(Bounds.X, Bounds.Y));
		PatchSlot->SetSize(FVector2D(Bounds.Z - Bounds.X, Bounds.W - Bounds.Y));
		auto* CaptionScale = WidgetTree->ConstructWidget<UScaleBox>();
		CaptionScale->SetStretch(EStretch::ScaleToFit);
		auto* CaptionSlot = Layer->AddChildToCanvas(CaptionScale);
		CaptionSlot->SetPosition(FVector2D(Bounds.X + 4, Bounds.Y + 2));
		CaptionSlot->SetSize(FVector2D(Bounds.Z - Bounds.X - 8, Bounds.W - Bounds.Y - 4));
		auto* Caption = Label(TBText::Get(Key), FontSize, TBPaper::Ink, true);
		Caption->SetAutoWrapText(false);
		CaptionScale->SetContent(Caption);
		Button->SetContent(ScaleCaption);
		FTitleCaption Entry;
		Entry.Button = Button; Entry.Patch = Patch; Entry.Layer = ScaleCaption;
		Entry.Key = Key; Entry.Original = Original;
		TitleCaptions.Add(Entry);
	};
	EditableArtCaption(HostButton, TEXT("Root.Title.Host"), TEXT("部屋をつくる"),
	    FVector2D(433, 174), FVector4(139, 59, 385, 125), 392, 36);
	EditableArtCaption(FindButton, TEXT("Root.Title.Find"), TEXT("部屋をさがす"),
	    FVector2D(433, 174), FVector4(139, 59, 385, 125), 392, 36);
	EditableArtCaption(HelpButton, TEXT("Root.Title.Help"), TEXT("あそびかた"),
	    FVector2D(260, 105), FVector4(82, 36, 234, 77), 235, 25);
	UpdateTitleCaptions();
	auto* RoomCard = WidgetTree->ConstructWidget<UBorder>();
	RoomCard->SetBrush(TBPaper::Brush(PaperTexture, TBPaper::Paper));
	// 部屋をさがすの右側。ボタン（x 〜530）やアイコン（y 745〜）と重ならない。
	Place(RoomCard, 880, 330, 560, 328);
	RoomCard->SetPadding(FMargin(24));
	auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	RoomCard->SetContent(Column);
	Column->AddChildToVerticalBox(Label(TBText::Get(TEXT("Root.Title.Together")), 26, TBPaper::Teal, true));
	Column->AddChildToVerticalBox(Label(TBText::Get(TEXT("Root.Title.Choose")), 17, TBPaper::Muted))
	    ->SetPadding(FMargin(0, 8, 0, 0));
	RoomList = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("Rooms"));
	FComboBoxStyle RoomStyle;
	FComboButtonStyle RoomButton;
	RoomButton.SetButtonStyle(TBPaper::CompactButtonStyle(true));
	RoomButton.SetMenuBorderBrush(TBPaper::Brush(PaperTexture, TBPaper::Paper));
	RoomButton.SetMenuBorderPadding(FMargin(12));
	FSlateBrush Arrow = TBPaper::ImageBrush(TBPaper::Art(TEXT("Mark_ChevronDown")));
	Arrow.ImageSize = FVector2D(18, 12);
	RoomButton.SetDownArrowImage(Arrow);
	RoomStyle.SetComboButtonStyle(RoomButton);
	RoomList->SetWidgetStyle(RoomStyle);
	RoomList->SetContentPadding(FMargin(14, 10));
	FTableRowStyle RoomRow;
	RoomRow.SetEvenRowBackgroundBrush(TBPaper::Brush(PaperTexture, TBPaper::Paper));
	RoomRow.SetOddRowBackgroundBrush(TBPaper::Brush(PaperTexture, TBPaper::Cream));
	RoomRow.SetEvenRowBackgroundHoveredBrush(TBPaper::Brush(PaperTexture, TBPaper::Gold));
	RoomRow.SetOddRowBackgroundHoveredBrush(TBPaper::Brush(PaperTexture, TBPaper::Gold));
	RoomRow.SetSelectorFocusedBrush(TBPaper::Brush(PaperTexture, TBPaper::Lavender));
	RoomRow.SetActiveBrush(TBPaper::Brush(PaperTexture, TBPaper::Gold));
	RoomRow.SetInactiveBrush(TBPaper::Brush(PaperTexture, TBPaper::Cream));
	RoomList->SetItemStyle(RoomRow);
	RoomList->OnGenerateWidgetEvent.BindDynamic(this, &ThisClass::RoomLabel);
	Column->AddChildToVerticalBox(RoomList)->SetPadding(FMargin(0, 22, 0, 12));
	RoomHintText = Label(FText::FromString(TEXT("")), 16, TBPaper::Muted);
	Column->AddChildToVerticalBox(RoomHintText);
	JoinButton = AddButton(TEXT("Join"), TBText::Get(TEXT("Root.Title.Join")), 910, 560, 500);
	// 右のカードは、部屋をさがすときだけ出す。
	RoomCardWidgets = {RoomCard, JoinButton};
	SetRoomCardsShown(false);
	StatusText = Label(FText::FromString(TEXT("")), 17, TBPaper::Paper);
	StatusText->SetShadowColorAndOpacity(TBPaper::Ink);
	StatusText->SetShadowOffset(FVector2D(1, 2));
	Place(StatusText, 96, 855, 1100, 40);
	HostButton->OnClicked.AddDynamic(this, &ThisClass::Host);
	FindButton->OnClicked.AddDynamic(this, &ThisClass::Find);
	JoinButton->OnClicked.AddDynamic(this, &ThisClass::Join);
	QuitButton->OnClicked.AddDynamic(this, &ThisClass::Quit);
	HelpButton->OnClicked.AddDynamic(this, &ThisClass::Help);
	OptionsButton->OnClicked.AddDynamic(this, &ThisClass::OpenOptions);
	RoomList->OnSelectionChanged.AddDynamic(this, &ThisClass::SelectionChanged);
	TitleContent = Scale;
	auto* Overlay = WidgetTree->ConstructWidget<UCanvasPanel>();
	HelpOverlay = Overlay;
	Fill(Overlay);
	auto* Dim = WidgetTree->ConstructWidget<UImage>();
	Dim->SetColorAndOpacity(FLinearColor(0.015f, .025f, .02f, .78f));
	auto* DimSlot = Overlay->AddChildToCanvas(Dim);
	DimSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	DimSlot->SetOffsets(FMargin(0));
	auto* PopupScale = WidgetTree->ConstructWidget<UScaleBox>();
	PopupScale->SetStretch(EStretch::ScaleToFit);
	auto* PopupSlot = Overlay->AddChildToCanvas(PopupScale);
	PopupSlot->SetAnchors(FAnchors(0, 0, 1, 1));
	PopupSlot->SetOffsets(FMargin(0));
	auto* PopupSize = WidgetTree->ConstructWidget<USizeBox>();
	PopupSize->SetWidthOverride(1280);
	PopupSize->SetHeightOverride(800);
	PopupScale->SetContent(PopupSize);
	Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	PopupSize->SetContent(Canvas);
	auto Card = [&](float X, float Y, float W, float H, FLinearColor Color)
	{
		auto* Shadow = WidgetTree->ConstructWidget<UBorder>();
		Shadow->SetBrush(TBPaper::Brush(PaperTexture, TBPaper::Shadow));
		Shadow->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Shadow, X + 5, Y + 7, W, H);
		auto* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrush(TBPaper::Brush(PaperTexture, Color));
		Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Panel, X, Y, W, H);
	};
	auto Icon = [&](const TCHAR* Name, float X, float Y, float W, float H)
	{
		auto* Art = WidgetTree->ConstructWidget<UImage>();
		Art->SetBrushFromTexture(TBPaper::Art(Name));
		Art->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Art, X, Y, W, H);
	};
	Card(130, 36, 1020, 730, TBPaper::Paper);
	Icon(TEXT("Tape_Cream"), 563, 20, 154, 38);
	Place(Label(TBText::Get(TEXT("Root.Title.Help")), 34, TBPaper::Ink, true), 180, 72, 750, 56);
	Place(Label(TBText::Get(TEXT("Root.Help.Lead")), 20, TBPaper::Muted), 180, 130, 860, 42);
	Card(176, 194, 440, 258, TBPaper::Cream);
	Card(640, 194, 464, 258, TBPaper::Lavender);
	Icon(TEXT("Icon_Battery"), 196, 206, 56, 56);
	Place(Label(TBText::Get(TEXT("Root.Common.Toy")), 27, TBPaper::Accent, true), 264, 216, 300, 45);
	HelpToyText = Label(FText::FromString(TEXT("")), 19, TBPaper::Ink);
	Place(HelpToyText, 204, 276, 380, 166);
	Icon(TEXT("Icon_PlayerRatio"), 660, 206, 56, 56);
	Place(Label(TBText::Get(TEXT("Root.Common.Human")), 27, TBPaper::HumanBlue, true), 728, 216, 330, 45);
	HelpHumanText = Label(FText::FromString(TEXT("")), 19, TBPaper::Ink);
	Place(HelpHumanText, 668, 276, 404, 166);
	Place(Label(TBText::Get(TEXT("Root.Help.Controls")), 23, TBPaper::Ink, true), 180, 478, 840, 40);
	HelpControlsText = Label(FText::FromString(TEXT("")), 18, TBPaper::Ink);
	Place(HelpControlsText, 184, 526, 890, 130);
	CloseHelpButton = AddButton(TEXT("CloseHelp"), TBText::Get(TEXT("Root.Help.Close")), 465, 680, 350);
	CloseHelpButton->OnClicked.AddDynamic(this, &ThisClass::CloseHelp);
	HelpOverlay->SetVisibility(ESlateVisibility::Collapsed);
	RefreshHelpText();
}

void UTBTitleMenu::Quit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

UWidget* UTBTitleMenu::RoomLabel(FString Item)
{
	auto* Label = WidgetTree->ConstructWidget<UTextBlock>();
	Label->SetText(FText::FromString(Item));
	Label->SetFont(FSlateFontInfo(PaperFont, 17));
	Label->SetColorAndOpacity(TBPaper::Ink);
	Label->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	return Label;
}

void UTBTitleMenu::RefreshHelpText()
{
	const auto* PC = GetOwningPlayer();
	const FString Move = TB::MoveKeysLabel(PC), Jump = TB::KeyLabel(PC, TB::Keys::Jump);
	const FString Interact = TB::KeyLabel(PC, TB::Keys::HumanInteract), ToyInteract = TB::KeyLabel(PC, TB::Keys::ToyInteract);
	const FString Drop = TB::KeyLabel(PC, TB::Keys::HumanDrop);
	HelpToyText->SetText(FText::Format(TBText::Get(TEXT("Root.Help.Toy")),
	    FText::FromString(TB::KeyLabel(PC, TB::Keys::ToyPush)), FText::FromString(ToyInteract)));
	HelpHumanText->SetText(FText::Format(TBText::Get(TEXT("Root.Help.Human")),
	    FText::FromString(Interact), FText::FromString(Interact), FText::FromString(Drop)));
	HelpControlsText->SetText(FText::Format(TBText::Get(TEXT("Root.Help.Keys")),
	    FText::FromString(Move), FText::FromString(Jump), FText::FromString(TB::KeyLabel(PC, TB::Keys::ToySprint)),
	    FText::FromString(TB::KeyLabel(PC, TB::Keys::HumanCrouch)), FText::FromString(Interact), FText::FromString(ToyInteract),
	    FText::FromString(Drop), FText::FromString(TB::KeyLabel(PC, TB::Keys::ToyTaunt)), FText::FromString(TB::KeyLabel(PC, TB::Keys::ToyPush))));
}

void UTBTitleMenu::Help()
{
	RefreshHelpText();
	TitleContent->SetIsEnabled(false);
	HelpOverlay->SetVisibility(ESlateVisibility::Visible);
	CloseHelpButton->SetKeyboardFocus();
}

void UTBTitleMenu::CloseHelp()
{
	HelpOverlay->SetVisibility(ESlateVisibility::Collapsed);
	TitleContent->SetIsEnabled(true);
	HelpButton->SetKeyboardFocus();
}

FReply UTBTitleMenu::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	// 音量・再生などのメディアキーは UE のキーにない（無効なキーで届く）。これでは飛ばさない。
	if (State == EState::Story && KeyEvent.GetKey().IsValid())
	{
		SkipStory();
		return FReply::Handled();
	}
	NoteInput();
	if (HelpOverlay && HelpOverlay->GetVisibility() == ESlateVisibility::Visible && KeyEvent.GetKey() == EKeys::Escape)
	{
		CloseHelp();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(Geometry, KeyEvent);
}

void UTBTitleMenu::UpdateTitleCaptions()
{
	for (const FTitleCaption& Entry : TitleCaptions)
	{
		if (!Entry.Button.IsValid() || !Entry.Patch.IsValid() || !Entry.Layer.IsValid()) continue;
		const bool ChangedCopy = TBText::String(*Entry.Key) != Entry.Original;
		Entry.Layer->SetVisibility(ChangedCopy ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		if (!ChangedCopy) continue;
		const auto& Style = Entry.Button->GetStyle();
		const FSlateBrush& StateBrush = Entry.Button->IsPressed() ? Style.Pressed : Entry.Button->IsHovered() ? Style.Hovered : Style.Normal;
		FSlateBrush Brush = Entry.Patch->GetBrush();
		if (Brush.GetResourceObject() != StateBrush.GetResourceObject())
		{
			Brush.SetResourceObject(StateBrush.GetResourceObject());
			Entry.Patch->SetBrush(Brush);
		}
	}
}
