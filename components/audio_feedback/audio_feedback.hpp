#pragma once

// ---------------------------------------------------------------------------
// Simple audio feedback — mechanical click sound on button press.
// Uses ESP32-P4 BSP audio (ES8311 codec via I2S).
// Call audio_feedback_init() once at startup, then audio_feedback_click()
// from any task (thread-safe, non-blocking).
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// Initialize audio hardware. Call once after BSP display init.
void audio_feedback_init(void);

// Play a short mechanical click sound. Non-blocking.
void audio_feedback_click(void);

// Play a 1.5s rising sweep (500→1200Hz) for autopilot engage.
void audio_feedback_engage(void);

// Play a 1.5s falling sweep (1200→500Hz) for autopilot disengage.
void audio_feedback_disengage(void);

// Play a double low buzz for invalid/no-op action.
void audio_feedback_invalid(void);

#ifdef __cplusplus
}
#endif
