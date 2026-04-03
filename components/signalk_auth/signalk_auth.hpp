#pragma once

#include <stdbool.h>

// ---------------------------------------------------------------------------
// SignalK device access request / token management.
// Uses esp_http_client for REST calls and NVS for persistent storage.
// ---------------------------------------------------------------------------

typedef enum {
    SK_AUTH_NONE,        // No token, no request in flight
    SK_AUTH_REQUESTING,  // POST sent, polling for admin approval
    SK_AUTH_APPROVED,    // Token obtained and stored in NVS
    SK_AUTH_DENIED,      // Admin denied the request
    SK_AUTH_ERROR,       // HTTP or network error
} signalk_auth_state_t;

#ifdef __cplusplus
extern "C" {
#endif

// Initialize auth module. Call once after nvs_flash_init().
// ws_uri is the discovered (or fallback) WebSocket URI — the HTTP base
// URL is derived internally (ws:// → http://, path stripped).
// Loads existing clientId and token from NVS if present.
void signalk_auth_init(const char *ws_uri);

// Update the server base URL (e.g. after mDNS re-discovery).
void signalk_auth_set_base_url(const char *ws_uri);

// Start the access request flow in a background FreeRTOS task.
// Non-blocking — returns immediately. The task:
//   1. Generates/loads clientId from NVS
//   2. POSTs to /signalk/v1/access/requests
//   3. Polls the returned href every 2s for up to 120s
//   4. Stores token in NVS on approval
void signalk_auth_request_access(void);

// Get current auth state (thread-safe).
signalk_auth_state_t signalk_auth_get_state(void);

// Get the stored token string. Returns NULL if no token.
// Caller must NOT free the returned pointer.
const char *signalk_auth_get_token(void);

// Convenience: true if a valid token is loaded.
bool signalk_auth_has_token(void);

// Get a human-readable error message (NULL if no error).
const char *signalk_auth_get_error(void);

// Validate the stored token against the server.
// Returns true if valid (HTTP 200), false if invalid/expired (401/403).
// On failure, clears the token and resets state to SK_AUTH_NONE.
bool signalk_auth_validate_token(void);

// Clear stored token and reset state to SK_AUTH_NONE.
void signalk_auth_clear_token(void);

// Send an authenticated HTTP request to a SignalK API path.
// method: HTTP_METHOD_PUT or HTTP_METHOD_POST (from esp_http_client.h)
// path: e.g. "/signalk/v2/api/vessels/self/autopilots/_default/engage"
// body: JSON string (NULL for POST with no body)
// Returns HTTP status code, or -1 on network error.
int signalk_auth_api_call(int method, const char *path, const char *body);

// Get the HTTP base URL derived from the WS URI (e.g. "http://192.168.1.100:3000").
// Returns NULL if not yet initialized.
const char *signalk_auth_get_base_url(void);

#ifdef __cplusplus
}
#endif
