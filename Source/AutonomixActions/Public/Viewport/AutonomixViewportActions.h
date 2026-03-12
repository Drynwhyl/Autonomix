// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AutonomixInterfaces.h"

/**
 * FAutonomixViewportActions
 *
 * Provides the capture_viewport tool — gives the AI "eyes" by capturing
 * the active editor viewport as a screenshot and encoding it to Base64 PNG.
 *
 * The captured image is returned as the tool_result content, which the LLM client
 * layer can then include as an image content block for Vision-Language Models
 * (Claude Opus/Sonnet with vision, GPT-4o, Gemini Pro Vision).
 *
 * The AI can:
 *   - Visually inspect UMG widget layouts it just built
 *   - Check lighting and material appearance in-viewport
 *   - Verify actor placement and level design
 *   - Identify misaligned UI elements and fix them autonomously
 *
 * IMPLEMENTATION:
 *   Uses FViewport::ReadPixels() on the active level editor viewport to capture
 *   the current frame, then encodes to PNG via IImageWrapper and Base64.
 *
 * NOTES:
 *   - Read-only tool: Low risk (no modifications to the project)
 *   - Image is resized to max 1024px on longest edge to control token usage
 *   - Works best with VLM-capable models; non-vision models ignore the image
 */
class AUTONOMIXACTIONS_API FAutonomixViewportActions : public IAutonomixActionExecutor
{
public:
	FAutonomixViewportActions();
	virtual ~FAutonomixViewportActions();

	// IAutonomixActionExecutor interface
	virtual FName GetActionName() const override;
	virtual FText GetDisplayName() const override;
	virtual EAutonomixActionCategory GetCategory() const override;
	virtual EAutonomixRiskLevel GetDefaultRiskLevel() const override;
	virtual FAutonomixActionPlan PreviewAction(const TSharedRef<FJsonObject>& Params) override;
	virtual FAutonomixActionResult ExecuteAction(const TSharedRef<FJsonObject>& Params) override;
	virtual bool CanUndo() const override;
	virtual bool UndoAction() override;
	virtual TArray<FString> GetSupportedToolNames() const override;
	virtual bool ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const override;

private:
	/**
	 * Capture the active editor viewport and return a Base64-encoded PNG.
	 *
	 * @param Params   JSON with optional "max_dimension" (int, default 1024),
	 *                 "viewport_index" (int, default 0)
	 * @param Result   Action result to populate
	 * @return         Populated result with Base64 PNG in ResultMessage
	 */
	FAutonomixActionResult ExecuteCaptureViewport(const TSharedRef<FJsonObject>& Params, FAutonomixActionResult& Result);

	/**
	 * Encode raw pixel data to a Base64 PNG string.
	 *
	 * @param Pixels        Raw BGRA8 pixel data
	 * @param Width         Image width
	 * @param Height        Image height
	 * @param MaxDimension  Maximum width/height for downscaling
	 * @return              Base64-encoded PNG string, empty on failure
	 */
	static FString EncodePixelsToPNGBase64(const TArray<FColor>& Pixels, int32 Width, int32 Height, int32 MaxDimension);
};
