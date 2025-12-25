#pragma once

struct AudioReactiveState
{
	bool active{ false };
	float mouthOpen{ 0.0f };
	float beatStrength{ 0.0f };
	float bpm{ 0.0f };
};