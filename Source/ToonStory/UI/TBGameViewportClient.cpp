// Copyright (c) 2026 Mamodou. All Rights Reserved.

#include "UI/TBGameViewportClient.h"
#include "Core/TBController.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Slate/SGameLayerManager.h"
#include "Widgets/SViewport.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#endif

namespace
{
	// Renderer warnings read this engine-wide flag after UGameViewportClient::Draw returns.
	// Hold the policy for the lifetime of all game viewports, including multiple PIE clients.
	class FTBScreenMessagePolicy
	{
	public:
		explicit FTBScreenMessagePolicy(bool& InEnabled) : Enabled(InEnabled) {}
		void Acquire(bool ShowWarnings)
		{
			if (Owners++ == 0) PreviousEnabled = Enabled;
			Apply(ShowWarnings);
		}
		void Apply(bool ShowWarnings)
		{
			if (Owners > 0) Enabled = ShowWarnings;
		}
		void Release()
		{
			if (Owners > 0 && --Owners == 0) Enabled = PreviousEnabled;
		}
	private:
		bool& Enabled;
		bool PreviousEnabled = false;
		int32 Owners = 0;
	};

	FTBScreenMessagePolicy GameScreenMessages(GAreScreenMessagesEnabled);
	int32 ShowGameEngineWarnings = 0;
	FAutoConsoleVariableRef CVarGameEngineWarnings(TEXT("tb.EngineWarnings"), ShowGameEngineWarnings,
	    TEXT("Show engine diagnostic overlays while a game viewport is active. Logs are unchanged. "
	         "This engine flag is process-wide, including editor views in the same PIE process."),
	    FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
	    {
		    GameScreenMessages.Apply(ShowGameEngineWarnings != 0);
	    }));

	class FTBUIShortcut : public IInputProcessor
	{
	public:
		explicit FTBUIShortcut(UTBGameViewportClient* InViewport) : Viewport(InViewport) {}
		virtual void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
		{
			auto* Client = Viewport.Get();
			const auto Widget = Client ? Client->GetGameViewportWidget() : nullptr;
			if (Event.GetKey() != EKeys::F9 || !Widget || (!Widget->HasKeyboardFocus() && !Widget->HasFocusedDescendants())) return false;
			if (!Event.IsRepeat()) Client->ToggleUI();
			return true;
		}
	private:
		TWeakObjectPtr<UTBGameViewportClient> Viewport;
	};
}

void UTBGameViewportClient::Init(FWorldContext& WorldContext, UGameInstance* OwningGameInstance, bool bCreateNewAudioDevice)
{
	if (!bOwnsScreenMessagePolicy)
	{
		GameScreenMessages.Acquire(ShowGameEngineWarnings != 0);
		bOwnsScreenMessagePolicy = true;
	}
	Super::Init(WorldContext, OwningGameInstance, bCreateNewAudioDevice);
	if (FSlateApplication::IsInitialized())
	{
		UIShortcut = MakeShared<FTBUIShortcut>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(UIShortcut, 0);
	}
}

void UTBGameViewportClient::DetachViewportClient()
{
	ReleaseShortcut();
	Super::DetachViewportClient();
	ReleaseScreenMessagePolicy();
}

void UTBGameViewportClient::BeginDestroy()
{
	ReleaseShortcut();
	ReleaseScreenMessagePolicy();
	Super::BeginDestroy();
}

void UTBGameViewportClient::ReleaseScreenMessagePolicy()
{
	if (!bOwnsScreenMessagePolicy) return;
	bOwnsScreenMessagePolicy = false;
	GameScreenMessages.Release();
}

void UTBGameViewportClient::ReleaseShortcut()
{
	if (UIShortcut && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(UIShortcut);
	UIShortcut.Reset();
	FocusBeforeHiding.Reset();
}

bool UTBGameViewportClient::IsUIHidden(const UWorld* World)
{
	const auto* Client = World ? Cast<UTBGameViewportClient>(World->GetGameViewport()) : nullptr;
	return Client && Client->IsUIHidden();
}

void UTBGameViewportClient::ToggleUI()
{
	bUIHidden = !bUIHidden;
	if (bUIHidden && GetWorld())
	{
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (auto* PC = Cast<ATBController>(It->Get()); PC && PC->IsLocalController()) PC->CloseChat();
	}
	if (bUIHidden && FSlateApplication::IsInitialized())
		FocusBeforeHiding = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (const auto Layers = GetGameLayerManager())
	{
		// Hidden/Collapsedはタイトル映像を進めるWidgetのTickも止めるため使わない。
		Layers->AsWidget()->SetRenderOpacity(bUIHidden ? 0.f : 1.f);
		Layers->AsWidget()->SetVisibility(bUIHidden ? EVisibility::HitTestInvisible : EVisibility::SelfHitTestInvisible);
	}
	if (FSlateApplication::IsInitialized())
	{
		auto& Slate = FSlateApplication::Get();
		if (bUIHidden) Slate.SetAllUserFocusToGameViewport();
		else
		{
			const auto ViewportWidget = GetGameViewportWidget();
			auto Restore = [&](const TSharedPtr<SWidget>& Widget)
			{
				FWidgetPath Path;
				if (!Widget || Widget == ViewportWidget || !Widget->SupportsKeyboardFocus() ||
				    !Slate.GeneratePathToWidgetUnchecked(Widget.ToSharedRef(), Path) ||
				    !Path.ContainsWidget(ViewportWidget.Get())) return false;
				for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
					if (!Path.Widgets[Index].Widget->IsEnabled()) return false;
				// Slate returns false when focus already matches; that is success here.
				return Slate.GetKeyboardFocusedWidget() == Widget || Slate.SetKeyboardFocus(Widget, EFocusCause::SetDirectly);
			};
			// A menu may have changed while hidden. Prefer its new focus, and never
			// revive a removed/collapsed widget (or a chat box closed above).
			if (!Restore(Slate.GetKeyboardFocusedWidget()) && !Restore(FocusBeforeHiding.Pin()))
				Slate.SetAllUserFocusToGameViewport();
			FocusBeforeHiding.Reset();
		}
	}
}

void UTBGameViewportClient::Draw(FViewport* InViewport, FCanvas* SceneCanvas)
{
	// Do not restore true on return: the render thread can draw its warning overlay later.
	GameScreenMessages.Apply(ShowGameEngineWarnings != 0);
	Super::Draw(InViewport, SceneCanvas);
	GameScreenMessages.Apply(ShowGameEngineWarnings != 0);
}

EMouseCursor::Type UTBGameViewportClient::GetCursor(FViewport* InViewport, int32 X, int32 Y)
{
	return bUIHidden ? EMouseCursor::None : Super::GetCursor(InViewport, X, Y);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBEngineWarningLifetimeTest, "ToonStory.UI.EngineWarnings.Lifetime",
                                EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBEngineWarningLifetimeTest::RunTest(const FString&)
{
	bool Enabled = true;
	FTBScreenMessagePolicy Policy(Enabled);
	Policy.Acquire(false);
	TestFalse(TEXT("First game viewport suppresses engine overlays"), Enabled);
	Policy.Acquire(false);
	Policy.Release();
	TestFalse(TEXT("Closing one of two PIE clients cannot restore warnings"), Enabled);
	Policy.Apply(true);
	TestTrue(TEXT("Diagnostics can be explicitly enabled"), Enabled);
	Policy.Apply(false);
	TestFalse(TEXT("Diagnostics can be hidden again"), Enabled);
	Policy.Release();
	TestTrue(TEXT("Last viewport restores the original editor state"), Enabled);
	Policy.Release();
	TestTrue(TEXT("Repeated cleanup cannot change the restored state"), Enabled);
	Enabled = false;
	Policy.Acquire(true);
	TestTrue(TEXT("Explicit opt-in works even if overlays were previously disabled"), Enabled);
	Policy.Release();
	TestFalse(TEXT("Previously disabled editor overlays remain disabled after PIE"), Enabled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTBEngineWarningRenderThreadTest, "ToonStory.UI.EngineWarnings.RenderThread",
                                EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FTBEngineWarningRenderThreadTest::RunTest(const FString&)
{
	UTBGameViewportClient* Client = nullptr;
	for (const auto& Context : GEngine->GetWorldContexts())
	{
		if (Context.World() && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
		{
			Client = Cast<UTBGameViewportClient>(Context.World()->GetGameViewport());
			if (Client) break;
		}
	}
	if (!TestNotNull(TEXT("A live game viewport owns the diagnostic policy"), Client)) return false;
	auto* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("tb.EngineWarnings"));
	if (!TestNotNull(TEXT("Engine warning opt-in exists"), Variable)) return false;
	const int32 OldValue = Variable->GetInt();
	ON_SCOPE_EXIT { Variable->SetWithCurrentPriority(OldValue); };
	auto ReadOnRenderThread = []()
	{
		bool Enabled = true;
		ENQUEUE_RENDER_COMMAND(TBReadScreenMessages)([&Enabled](FRHICommandListImmediate&)
		{
			Enabled = GAreScreenMessagesEnabled;
		});
		FlushRenderingCommands();
		return Enabled;
	};
	Variable->SetWithCurrentPriority(0);
	TestFalse(TEXT("Suppression persists outside viewport Draw"), GAreScreenMessagesEnabled);
	TestFalse(TEXT("A later render-thread command still observes suppression"), ReadOnRenderThread());
	Variable->SetWithCurrentPriority(1);
	TestTrue(TEXT("Opt-in also reaches the renderer"), ReadOnRenderThread());
	Variable->SetWithCurrentPriority(0);
	TestFalse(TEXT("Returning to normal presentation does not need another Draw call"), ReadOnRenderThread());
	// Observe actual frames as well: a scoped Draw guard can pass in-Draw checks but
	// restore the flag before the renderer consumes its warning-overlay pass.
	const auto CompletedDraws = MakeShared<int32>(0);
	const FDelegateHandle DrawHandle = Client->OnEndDraw().AddLambda([CompletedDraws] { ++*CompletedDraws; });
	const TWeakObjectPtr<UTBGameViewportClient> WeakClient(Client);
	const double Started = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
	    [this, WeakClient, CompletedDraws, DrawHandle, Started, OldValue, ReadOnRenderThread]()
	    {
		    auto* ActiveClient = WeakClient.Get();
		    if (!ActiveClient) { AddError(TEXT("Game viewport closed before frame verification")); return true; }
		    if (*CompletedDraws < 2 && FPlatformTime::Seconds() - Started < 5.0) return false;
		    ActiveClient->OnEndDraw().Remove(DrawHandle);
		    TestTrue(TEXT("At least two real viewport draws completed"), *CompletedDraws >= 2);
		    TestEqual(TEXT("Viewport Draw does not restore a transient diagnostic state"),
		              GAreScreenMessagesEnabled, OldValue != 0);
		    TestEqual(TEXT("Renderer observes the same policy after completed frames"),
		              ReadOnRenderThread(), OldValue != 0);
		    return true;
	    }));
	return true;
}
#endif
