// Source/GeminiBridge/Private/GeminiSettings.cpp

#include "GeminiSettings.h"

UGeminiSettings::UGeminiSettings()
{
	// デフォルト値の設定
	DefaultModel = TEXT("gemini-1.5-flash");
}

const UGeminiSettings* UGeminiSettings::GetGeminiSettings()
{
	return GetDefault<UGeminiSettings>();
}