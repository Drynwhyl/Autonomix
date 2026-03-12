// Copyright Autonomix. All Rights Reserved.

#include "Viewport/AutonomixViewportActions.h"
#include "AutonomixCoreModule.h"
#include "AutonomixSettings.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "Misc/Base64.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

#define LOCTEXT_NAMESPACE "AutonomixViewportActions"

// ============================================================================
// Lifecycle
// ============================================================================

FAutonomixViewportActions::FAutonomixViewportActions() {}
FAutonomixViewportActions::~FAutonomixViewportActions() {}

// ============================================================================
// IAutonomixActionExecutor Interface
// ============================================================================

FName FAutonomixViewportActions::GetActionName() const { return FName(TEXT("Viewport")); }
FText FAutonomixViewportActions::GetDisplayName() const { return LOCTEXT("DisplayName", "Viewport Capture"); }
EAutonomixActionCategory FAutonomixViewportActions::GetCategory() const { return EAutonomixActionCategory::General; }
EAutonomixRiskLevel FAutonomixViewportActions::GetDefaultRiskLevel() const { return EAutonomixRiskLevel::Low; }
bool FAutonomixViewportActions::CanUndo() const { return false; }
bool FAutonomixViewportActions::UndoAction() { return false; }

TArray<FString> FAutonomixViewportActions::GetSupportedToolNames() const
{
	return { TEXT("capture_viewport") };
}

bool FAutonomixViewportActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	// No required params — all optional
	return true;
}

FAutonomixActionPlan FAutonomixViewportActions::PreviewAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionPlan Plan;
	Plan.Summary = TEXT("Capture editor viewport screenshot (read-only)");
	Plan.MaxRiskLevel = EAutonomixRiskLevel::Low;

	FAutonomixAction Action;
	Action.Description = Plan.Summary;
	Action.Category = EAutonomixActionCategory::General;
	Action.RiskLevel = EAutonomixRiskLevel::Low;
	Plan.Actions.Add(Action);
	return Plan;
}

FAutonomixActionResult FAutonomixViewportActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAutonomixActionResult Result;
	Result.bSuccess = false;
	return ExecuteCaptureViewport(Params, Result);
}

// ============================================================================
// capture_viewport
// ============================================================================

FAutonomixActionResult FAutonomixViewportActions::ExecuteCaptureViewport(
	const TSharedRef<FJsonObject>& Params,
	FAutonomixActionResult& Result)
{
	// Get optional parameters
	int32 MaxDimension = 1024;
	Params->TryGetNumberField(TEXT("max_dimension"), MaxDimension);
	MaxDimension = FMath::Clamp(MaxDimension, 256, 2048);

	int32 ViewportIndex = 0;
	Params->TryGetNumberField(TEXT("viewport_index"), ViewportIndex);

	// Get the level editor viewport
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<SLevelViewport> LevelViewport;

	TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
	if (!LevelEditor.IsValid())
	{
		Result.Errors.Add(TEXT("No Level Editor is active. Cannot capture viewport."));
		return Result;
	}

	// Get the active viewport (index 0 = primary viewport)
	TSharedPtr<SLevelViewport> ActiveViewport = LevelEditor->GetActiveViewportInterface();
	if (!ActiveViewport.IsValid())
	{
		Result.Errors.Add(TEXT("No active viewport found in the Level Editor."));
		return Result;
	}

	// Get the FViewport from the Slate viewport widget
	FViewport* Viewport = ActiveViewport->GetActiveViewport();
	if (!Viewport)
	{
		Result.Errors.Add(TEXT("Could not get FViewport from the active Level Viewport."));
		return Result;
	}

	// Read pixels from the viewport
	int32 Width = Viewport->GetSizeXY().X;
	int32 Height = Viewport->GetSizeXY().Y;

	if (Width <= 0 || Height <= 0)
	{
		Result.Errors.Add(TEXT("Viewport has zero dimensions. It may be minimized or not yet rendered."));
		return Result;
	}

	TArray<FColor> Pixels;
	if (!Viewport->ReadPixels(Pixels))
	{
		Result.Errors.Add(TEXT("Failed to read pixels from the viewport. The viewport may not have rendered yet."));
		return Result;
	}

	if (Pixels.Num() != Width * Height)
	{
		Result.Errors.Add(FString::Printf(TEXT("Pixel count mismatch: expected %d, got %d"), Width * Height, Pixels.Num()));
		return Result;
	}

	UE_LOG(LogAutonomix, Log, TEXT("ViewportActions: Captured viewport %dx%d (%d pixels)"), Width, Height, Pixels.Num());

	// Encode to Base64 PNG
	FString Base64PNG = EncodePixelsToPNGBase64(Pixels, Width, Height, MaxDimension);
	if (Base64PNG.IsEmpty())
	{
		Result.Errors.Add(TEXT("Failed to encode viewport capture to PNG."));
		return Result;
	}

	// Return the Base64 image data
	// The MainPanel/LLM layer detects the [IMAGE:base64:...] prefix and sends it as
	// an image content block to VLM-capable models.
	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(
		TEXT("[IMAGE:base64:data:image/png;base64,%s]\n\n"
			 "Viewport captured successfully (%dx%d, resized to max %dpx). "
			 "The image shows the current editor viewport."),
		*Base64PNG, Width, Height, MaxDimension);

	return Result;
}

// ============================================================================
// PNG Encoding
// ============================================================================

FString FAutonomixViewportActions::EncodePixelsToPNGBase64(
	const TArray<FColor>& Pixels,
	int32 Width,
	int32 Height,
	int32 MaxDimension)
{
	if (Pixels.Num() == 0 || Width <= 0 || Height <= 0) return FString();

	// Determine if we need to resize
	int32 OutWidth = Width;
	int32 OutHeight = Height;
	int32 LongestEdge = FMath::Max(Width, Height);

	TArray<FColor> ResizedPixels;
	const TArray<FColor>* PixelsToEncode = &Pixels;

	if (LongestEdge > MaxDimension)
	{
		float Scale = static_cast<float>(MaxDimension) / static_cast<float>(LongestEdge);
		OutWidth = FMath::Max(1, FMath::RoundToInt(Width * Scale));
		OutHeight = FMath::Max(1, FMath::RoundToInt(Height * Scale));

		// Simple bilinear downscale
		ResizedPixels.SetNumUninitialized(OutWidth * OutHeight);
		for (int32 Y = 0; Y < OutHeight; ++Y)
		{
			for (int32 X = 0; X < OutWidth; ++X)
			{
				float SrcX = static_cast<float>(X) * Width / OutWidth;
				float SrcY = static_cast<float>(Y) * Height / OutHeight;
				int32 SX = FMath::Clamp(FMath::FloorToInt(SrcX), 0, Width - 1);
				int32 SY = FMath::Clamp(FMath::FloorToInt(SrcY), 0, Height - 1);
				ResizedPixels[Y * OutWidth + X] = Pixels[SY * Width + SX];
			}
		}
		PixelsToEncode = &ResizedPixels;
	}

	// Use IImageWrapper to encode to PNG
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid())
	{
		UE_LOG(LogAutonomix, Error, TEXT("ViewportActions: Failed to create PNG ImageWrapper"));
		return FString();
	}

	// Set raw BGRA data
	TArray<uint8> RawData;
	RawData.SetNumUninitialized(OutWidth * OutHeight * 4);
	for (int32 i = 0; i < PixelsToEncode->Num(); ++i)
	{
		const FColor& C = (*PixelsToEncode)[i];
		RawData[i * 4 + 0] = C.B;
		RawData[i * 4 + 1] = C.G;
		RawData[i * 4 + 2] = C.R;
		RawData[i * 4 + 3] = C.A;
	}

	if (!ImageWrapper->SetRaw(RawData.GetData(), RawData.Num(), OutWidth, OutHeight, ERGBFormat::BGRA, 8))
	{
		UE_LOG(LogAutonomix, Error, TEXT("ViewportActions: Failed to set raw pixel data on ImageWrapper"));
		return FString();
	}

	// Get compressed PNG data
	TArray<uint8> PNGData;
	PNGData = ImageWrapper->GetCompressed(100);

	if (PNGData.Num() == 0)
	{
		UE_LOG(LogAutonomix, Error, TEXT("ViewportActions: PNG compression returned empty data"));
		return FString();
	}

	UE_LOG(LogAutonomix, Log, TEXT("ViewportActions: PNG encoded %dx%d -> %d bytes"), OutWidth, OutHeight, PNGData.Num());

	// Base64 encode
	return FBase64::Encode(PNGData);
}

#undef LOCTEXT_NAMESPACE
