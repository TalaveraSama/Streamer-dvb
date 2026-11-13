#pragma once
// Compatibilidad con GStreamer < 1.20 (Ubuntu 20.04 trae 1.16).
// gst_element_request_pad_simple() es el nombre nuevo (1.20) de
// gst_element_get_request_pad(); en versiones anteriores no existe.
#include <gst/gst.h>

#if !GST_CHECK_VERSION(1, 20, 0)
static inline GstPad* gst_element_request_pad_simple(GstElement* element, const gchar* name) {
    return gst_element_get_request_pad(element, name);
}
#endif
