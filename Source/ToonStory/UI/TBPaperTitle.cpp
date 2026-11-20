// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "UI/TBUIText.h"
#include "UI/TBTitleMenu.h"
#include "UI/Loading/TBLoadingScreenSubsystem.h"
#include "Input/TBKeyLabels.h"
#include "Core/TBSession.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Engine/GameInstance.h"
#include "TBPaperStyle.h"
#include "TBPaperButton.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScaleBox.h"
#include "Components/Image.h"
#include "Kismet/KismetSystemLibrary.h"
#include "InputCoreTypes.h"

namespace
{
	struct FHelpBinding
	{
		FName Mapping;
		const TCHAR* LabelKey;
	};
	const FHelpBinding CommonHelpBindings[] = {
		{TB::Keys::MoveForward, TEXT("Root.Help.Move")}, {NAME_None, TEXT("Root.Help.Look")},
		{TB::Keys::Jump, TEXT("Keys.Action.Common_Jump")}
	};
	const FHelpBinding ToyHelpBindings[] = {
		{TB::Keys::ToyInteract, TEXT("Keys.Action.Toy_Interact")}, {TB::Keys::ToySprint, TEXT("Keys.Action.Toy_Sprint")},
		{TB::Keys::ToyPush, TEXT("Keys.Action.Toy_Push")}, {TB::Keys::ToyTaunt, TEXT("Keys.Action.Toy_Taunt")}
	};
	const FHelpBinding HumanHelpBindings[] = {
		{TB::Keys::HumanInteract, TEXT("Keys.Action.Human_Interact")}, {TB::Keys::HumanGaze, TEXT("Keys.Action.Human_Gaze")},
		{TB::Keys::HumanDrop, TEXT("Keys.Action.Human_Drop")}, {TB::Keys::HumanCrouch, TEXT("Keys.Action.Human_Crouch")}
	};
	static_assert(UE_ARRAY_COUNT(ToyHelpBindings) == UE_ARRAY_COUNT(HumanHelpBindings));
}

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
	StatusText = Label(FText::FromString(TEXT("")), 17, TBPaper::Paper);
	StatusText->SetShadowColorAndOpacity(TBPaper::Ink);
	StatusText->SetShadowOffset(FVector2D(1, 2));
	Place(StatusText, 96, 855, 1100, 40);
	HostButton->OnClicked.AddDynamic(this, &ThisClass::Host);
	FindButton->OnClicked.AddDynamic(this, &ThisClass::Find);
	QuitButton->OnClicked.AddDynamic(this, &ThisClass::Quit);
	HelpButton->OnClicked.AddDynamic(this, &ThisClass::Help);
	OptionsButton->OnClicked.AddDynamic(this, &ThisClass::OpenOptions);
	TitleContent = Scale;
	auto NewPopup = [&]
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
		Dim->SetColorAndOpacity(TBPaper::Ink.CopyWithNewOpacity(.62f));
		FillPopup(Dim);
		auto* PopupScale = WidgetTree->ConstructWidget<UScaleBox>();
		PopupScale->SetStretch(EStretch::ScaleToFit);
		FillPopup(PopupScale);
		auto* PopupSize = WidgetTree->ConstructWidget<USizeBox>();
		PopupSize->SetWidthOverride(1280);
		PopupSize->SetHeightOverride(800);
		PopupScale->SetContent(PopupSize);
		Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		PopupSize->SetContent(Canvas);
		return Overlay;
	};
	auto Card = [&](float X, float Y, float W, float H, FLinearColor Color)
	{
		auto* Shadow = WidgetTree->ConstructWidget<UBorder>();
		Shadow->SetBrush(FSlateRoundedBoxBrush(TBPaper::Cream, 15.f, TBPaper::Outline, 2.f));
		Shadow->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Shadow, X, Y + 4, W, H);
		auto* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrush(FSlateRoundedBoxBrush(Color, 15.f, TBPaper::Outline, 2.5f));
		Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Panel, X, Y, W, H);
	};
	auto Sign = [&](const TCHAR* Name, const TCHAR* Key, float X, float Y, float Width, FLinearColor Tint)
	{
		auto* Button = AddButton(Name, TBText::Get(Key), X, Y, Width);
		Button->SetStyle(TBPaper::SignButtonStyle(Tint));
		auto* Caption = CastChecked<UTextBlock>(Button->GetContent());
		Caption->RemoveFromParent();
		TBPaper::FitSignCaption(*WidgetTree, *Button, *Caption);
		return Button;
	};
	auto HeadingSign = [&](const TCHAR* Key, float X, float Y, float Width)
	{
		auto* SignFace = WidgetTree->ConstructWidget<UBorder>();
		SignFace->SetBrush(TBPaper::SignButtonStyle(TBPaper::Gold).Normal);
		SignFace->SetPadding(FMargin(10, 3, 10, 6));
		SignFace->SetVisibility(ESlateVisibility::HitTestInvisible);
		SignFace->SetRenderTransformAngle(-1.f);
		auto* Caption = Label(TBText::Get(Key), 19, TBPaper::Ink, true);
		Caption->SetAutoWrapText(false);
		auto* Fit = WidgetTree->ConstructWidget<UScaleBox>();
		Fit->SetStretch(EStretch::ScaleToFit);
		Fit->SetStretchDirection(EStretchDirection::DownOnly);
		Fit->SetContent(Caption);
		SignFace->SetContent(Fit);
		Place(SignFace, X, Y, Width, 50);
	};
	FinderOverlay = NewPopup();
	Card(305, 188, 670, 424, TBPaper::Paper);
	HeadingSign(TEXT("Root.Title.Find"), 330, 212, 204);
	RoomHintText = Label(FText::GetEmpty(), 16, TBPaper::Muted);
	RoomHintText->SetJustification(ETextJustify::Center);
	Place(RoomHintText, 340, 380, 600, 62);
	RoomList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("Rooms"));
	RoomList->SetScrollbarThickness(FVector2D(6, 6));
	Place(RoomList, 333, 278, 616, 244);
	auto* FootRule = WidgetTree->ConstructWidget<UImage>();
	FootRule->SetColorAndOpacity(TBPaper::Outline.CopyWithNewOpacity(.24f));
	FootRule->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(FootRule, 316, 536, 648, 1);
	auto FinderButton = [&](const TCHAR* Name, const TCHAR* Key, float X, float Width, FLinearColor Tint)
	{
		UButton* Button = Sign(Name, Key, X, 552, Width, Tint);
		TBPaper::SignCaption(Button)->SetFont(FSlateFontInfo(LogoFont, 14));
		CastChecked<UCanvasPanelSlot>(Button->Slot)->SetSize(FVector2D(Width, 40));
		return Button;
	};
	BackFromFinderButton = FinderButton(TEXT("BackFromFinder"), TEXT("Root.Common.Back"), 334, 90, TBPaper::Paper);
	RefreshRoomsButton = FinderButton(TEXT("RefreshRooms"), TEXT("Root.Title.Refresh"), 682, 104, TBPaper::Paper);
	JoinButton = FinderButton(TEXT("Join"), TEXT("Root.Title.Join"), 797, 149, TBPaper::Gold);
	BackFromFinderButton->OnClicked.AddDynamic(this, &ThisClass::CloseFinder);
	RefreshRoomsButton->OnClicked.AddDynamic(this, &ThisClass::Find);
	JoinButton->OnClicked.AddDynamic(this, &ThisClass::Join);
	SetRoomCardsShown(false);

	HelpOverlay = NewPopup();
	const FLinearColor HelpPaper = FLinearColor::FromSRGBColor(FColor(255, 245, 222));
	const FLinearColor Rule = FLinearColor::FromSRGBColor(FColor(205, 167, 123));
	auto HelpPanel = [&](const FSlateBrush& Face, float X, float Y, float W, float H)
	{
		auto* Panel = WidgetTree->ConstructWidget<UBorder>();
		Panel->SetBrush(Face);
		Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Panel, X, Y, W, H);
		return Panel;
	};
	Card(294, 142, 692, 502, TBPaper::Paper);
	HeadingSign(TEXT("Root.Title.Help"), 316, 160, 195);
	auto HelpTab = [&](const TCHAR* Name, const TCHAR* TextKey, float X)
	{
		auto* Button = AddButton(Name, TBText::Get(TextKey), X, 227, 316);
		CastChecked<UCanvasPanelSlot>(Button->Slot)->SetSize(FVector2D(316, 41));
		auto* Caption = CastChecked<UTextBlock>(Button->GetContent());
		Caption->SetFont(FSlateFontInfo(LogoFont, 14));
		auto* Slot = CastChecked<UButtonSlot>(Caption->Slot);
		Slot->SetHorizontalAlignment(HAlign_Center);
		Slot->SetVerticalAlignment(VAlign_Center);
		return Button;
	};
	HelpToyTab = HelpTab(TEXT("HelpToyTab"), TEXT("Root.Common.Toy"), 318);
	HelpHumanTab = HelpTab(TEXT("HelpHumanTab"), TEXT("Root.Common.Human"), 646);
	HelpToyTab->OnClicked.AddDynamic(this, &ThisClass::ShowToyHelp);
	HelpHumanTab->OnClicked.AddDynamic(this, &ThisClass::ShowHumanHelp);
	HelpPanel(FSlateRoundedBoxBrush(HelpPaper, 8.f, Rule, 1.5f), 318, 278, 644, 126);
	HelpRoleIcon = WidgetTree->ConstructWidget<UImage>();
	HelpRoleIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
	Place(HelpRoleIcon, 335, 294, 33, 33);
	HelpGoal = Label(FText::GetEmpty(), 16, TBPaper::Ink, true, TEXT("HelpGoal"));
	Place(HelpGoal, 379, 295, 568, 33);
	HelpDescription = Label(FText::GetEmpty(), 13, TBPaper::Ink, false, TEXT("HelpDescription"));
	Place(HelpDescription, 338, 338, 606, 58);
	Place(Label(TBText::Get(TEXT("Root.Help.Controls")), 15, TBPaper::Ink, true), 318, 420, 630, 30);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(CommonHelpBindings) + UE_ARRAY_COUNT(ToyHelpBindings); ++Index)
	{
		const float X = 318 + (Index % 3) * 218, Y = 455 + (Index / 3) * 34;
		auto* Keycap = WidgetTree->ConstructWidget<UBorder>();
		Keycap->SetBrush(FSlateRoundedBoxBrush(HelpPaper, 5.f, TBPaper::Muted, 1.5f));
		Keycap->SetPadding(FMargin(3, 1));
		Keycap->SetHorizontalAlignment(HAlign_Center);
		Keycap->SetVerticalAlignment(VAlign_Center);
		auto* Fit = WidgetTree->ConstructWidget<UScaleBox>();
		Fit->SetStretch(EStretch::ScaleToFit);
		Fit->SetStretchDirection(EStretchDirection::DownOnly);
		auto* Key = Label(FText::GetEmpty(), 12, TBPaper::Ink);
		Key->SetAutoWrapText(false);
		HelpKeyLabels.Add(Key);
		Fit->SetContent(Key);
		Keycap->SetContent(Fit);
		Place(Keycap, X, Y, 78, 25);
		auto* Action = Label(FText::GetEmpty(), 12, TBPaper::Ink);
		HelpActions.Add(Action);
		Place(Action, X + 87, Y + 1, 121, 32);
	}
	HelpPanel(FSlateRoundedBoxBrush(Rule, 0.f), 310, 568, 660, 1);
	CloseHelpButton = Sign(TEXT("CloseHelp"), TEXT("Root.Help.Close"), 795, 579, 167, TBPaper::Gold);
	TBPaper::SignCaption(CloseHelpButton)->SetFont(FSlateFontInfo(LogoFont, 14));
	CastChecked<UCanvasPanelSlot>(CloseHelpButton->Slot)->SetSize(FVector2D(167, 47));
	CloseHelpButton->OnClicked.AddDynamic(this, &ThisClass::CloseHelp);
	HelpOverlay->SetVisibility(ESlateVisibility::Collapsed);
	RefreshHelpText();
}

void UTBTitleMenu::Quit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UTBTitleMenu::RefreshHelpText()
{
	const auto* PC = GetOwningPlayer();
	auto SetTabFace = [](UButton& Tab, bool Selected, const FLinearColor& TeamColor)
	{
		FButtonStyle Style = TBPaper::CompactButtonStyle();
		Style.SetNormal(FSlateRoundedBoxBrush(Selected ? TeamColor : TBPaper::Paper, 6.f,
		    Selected ? TBPaper::Outline : TBPaper::Muted, Selected ? 2.5f : 1.25f));
		Style.SetHovered(FSlateRoundedBoxBrush(FMath::Lerp(TeamColor, TBPaper::Paper, .3f), 6.f, TBPaper::Outline, 2.f));
		Style.SetPressed(FSlateRoundedBoxBrush(TeamColor, 6.f, TBPaper::Outline, 2.5f));
		Tab.SetStyle(Style);
	};
	SetTabFace(*HelpToyTab, !bHumanHelp, FLinearColor::FromSRGBColor(FColor(246, 193, 151)));
	SetTabFace(*HelpHumanTab, bHumanHelp, FLinearColor::FromSRGBColor(FColor(170, 218, 207)));
	HelpRoleIcon->SetBrushFromTexture(TBPaper::Art(bHumanHelp ? TEXT("Icon_PlayerRatio") : TEXT("Icon_Battery")));
	HelpGoal->SetText(TBText::Get(bHumanHelp ? TEXT("Root.Help.HumanGoal") : TEXT("Root.Help.ToyGoal")));
	const FName Interact = bHumanHelp ? TB::Keys::HumanInteract : TB::Keys::ToyInteract;
	HelpDescription->SetText(FText::Format(TBText::Get(bHumanHelp ? TEXT("Root.Help.Human") : TEXT("Root.Help.Toy")),
	    FText::FromString(TB::KeyLabel(PC, Interact))));
	for (int32 Index = 0; Index < HelpKeyLabels.Num(); ++Index)
	{
		const int32 RoleIndex = Index - UE_ARRAY_COUNT(CommonHelpBindings);
		const FHelpBinding& Binding = RoleIndex < 0 ? CommonHelpBindings[Index]
		    : (bHumanHelp ? HumanHelpBindings : ToyHelpBindings)[RoleIndex];
		const FName Mapping = Binding.Mapping;
		const FText Key = Mapping.IsNone() ? TBText::Get(TEXT("Root.Help.Mouse"))
		    : FText::FromString(Mapping == TB::Keys::MoveForward ? TB::MoveKeysLabel(PC) : TB::KeyLabel(PC, Mapping));
		HelpKeyLabels[Index]->SetText(Key);
		HelpActions[Index]->SetText(TBText::Get(Binding.LabelKey));
	}
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
	if (bRoomCardsShown && KeyEvent.GetKey() == EKeys::Escape)
	{
		CloseFinder();
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
