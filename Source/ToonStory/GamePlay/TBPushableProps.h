// Copyright (c) 2026 Mamodou. All Rights Reserved.
#pragma once

class AStaticMeshActor;
class UStaticMeshComponent;

namespace TBPushableProps
{
	void ConfigureCharacterCollision(UStaticMeshComponent& Part);
	/** Enable reviewed small props in the two playrooms without modifying their authored levels. */
	bool TryEnable(AStaticMeshActor& Prop);
}
