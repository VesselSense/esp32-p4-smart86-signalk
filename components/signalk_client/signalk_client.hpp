#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ---------------------------------------------------------------------------
// PathValue — a single instrument value keyed by SignalK path
// ---------------------------------------------------------------------------

struct PathValue {
    char     path[96]       = {};
    float    value          = 0.0f;
    bool     valid          = false;
    uint32_t last_update_ms = 0;
};

// ---------------------------------------------------------------------------
// InstrumentStore — flat array of path/value pairs, trivially copyable.
// Shared between WebSocket task (writer) and LVGL display task (reader).
// Access must be protected by the provided mutex.
// ---------------------------------------------------------------------------

static const int STORE_MAX_ENTRIES = 64;

struct InstrumentStore {
    PathValue entries[STORE_MAX_ENTRIES];
    int       count = 0;
    SemaphoreHandle_t mutex = nullptr;
};

// ---------------------------------------------------------------------------
// Store helpers (inline — used by both client and consumers)
// ---------------------------------------------------------------------------

// Find a path in the store. Returns nullptr if not found.
inline const PathValue* store_find(const InstrumentStore &s, const char *path)
{
    for (int i = 0; i < s.count; i++) {
        if (strcmp(s.entries[i].path, path) == 0) return &s.entries[i];
    }
    return nullptr;
}

// Mutable find (internal use by store_set).
inline PathValue* store_find_mut(InstrumentStore &s, const char *path)
{
    for (int i = 0; i < s.count; i++) {
        if (strcmp(s.entries[i].path, path) == 0) return &s.entries[i];
    }
    return nullptr;
}

// Add or update a value in the store. Caller must hold mutex.
inline void store_set(InstrumentStore &s, const char *path, float value, uint32_t now)
{
    PathValue *pv = store_find_mut(s, path);
    if (!pv) {
        if (s.count >= STORE_MAX_ENTRIES) return;  // full
        pv = &s.entries[s.count++];
        strncpy(pv->path, path, sizeof(pv->path) - 1);
        pv->path[sizeof(pv->path) - 1] = '\0';
    }
    pv->value          = value;
    pv->valid          = true;
    pv->last_update_ms = now;
}

// Remove a path from the store. Caller must hold mutex.
inline void store_remove(InstrumentStore &s, const char *path)
{
    for (int i = 0; i < s.count; i++) {
        if (strcmp(s.entries[i].path, path) == 0) {
            // Shift last entry into this slot
            if (i < s.count - 1) {
                s.entries[i] = s.entries[s.count - 1];
            }
            s.count--;
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Connection state
// ---------------------------------------------------------------------------

typedef enum {
    SK_STATE_WIFI_CONNECTING,
    SK_STATE_MDNS_SEARCHING,
    SK_STATE_WS_CONNECTING,
    SK_STATE_CONNECTED,
    SK_STATE_DISCONNECTED,
} signalk_conn_state_t;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

// Call after WiFi is connected. Starts the WebSocket client task.
void signalk_client_start(const char *uri);

// Stop the client (optional cleanup).
void signalk_client_stop(void);

// Returns true if currently connected to SignalK server.
bool signalk_client_is_connected(void);

// Get/set the connection state (set called from main.cpp for WiFi/mDNS phases).
signalk_conn_state_t signalk_client_get_state(void);
void signalk_client_set_state(signalk_conn_state_t state);

// Returns true if the client tore itself down after repeated failures
// and needs main.cpp to re-run mDNS discovery. Clears the flag on read.
bool signalk_client_needs_rediscovery(void);

// Copy current store snapshot. Returns false if mutex unavailable.
// dest->mutex is NOT set (caller owns the original mutex).
bool signalk_client_get_data(InstrumentStore *dest);

// Subscribe to a single path on the open WebSocket connection.
// Adds the path to the store (valid=false) so it has a slot ready for data.
void signalk_client_subscribe(const char *path, int period_ms);

// Unsubscribe from a single path and remove it from the store.
void signalk_client_unsubscribe(const char *path);

// Observer: watch a single path for value changes.
// When the path receives new data in parse_delta(), cb is invoked on the
// LVGL thread via lv_async_call(). Only one watch at a time.
// Call with path=NULL to clear the watch.
typedef void (*signalk_value_cb_t)(void *user_data);
void signalk_client_watch(const char *path, signalk_value_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif
