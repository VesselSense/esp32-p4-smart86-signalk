#include "signalk_auth.hpp"
#include "audio_feedback.hpp"

static signalk_auth_state_t s_state = SK_AUTH_APPROVED;

void signalk_auth_init(const char *) {}
void signalk_auth_set_base_url(const char *) {}
void signalk_auth_request_access(void) { s_state = SK_AUTH_REQUESTING; }
signalk_auth_state_t signalk_auth_get_state(void) { return s_state; }
const char *signalk_auth_get_token(void) { return "stub-token"; }
bool signalk_auth_has_token(void) { return true; }
const char *signalk_auth_get_error(void) { return nullptr; }
bool signalk_auth_validate_token(void) { return true; }
void signalk_auth_clear_token(void) {}
int signalk_auth_api_call(int, const char *, const char *) { return 200; }
const char *signalk_auth_get_base_url(void) { return "http://localhost:3000"; }

// Audio stubs
void audio_feedback_init(void) {}
void audio_feedback_click(void) {}
void audio_feedback_engage(void) {}
void audio_feedback_disengage(void) {}
void audio_feedback_invalid(void) {}
