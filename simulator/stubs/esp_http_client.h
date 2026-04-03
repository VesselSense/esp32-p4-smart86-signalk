#pragma once
// Minimal stub for simulator — only needs the method enum
typedef enum {
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
} esp_http_client_method_t;
