#pragma once

#include <gst/gst.h>

// GStreamer 1.20 renamed gst_element_get_request_pad to
// gst_element_request_pad_simple. Both return a requested pad owned by the
// caller; it must be released with gst_element_release_request_pad and unref'd.
inline GstPad* tvsRequestPad(GstElement* element, const char* name) {
#if GST_CHECK_VERSION(1, 20, 0)
    return gst_element_request_pad_simple(element, name);
#else
    return gst_element_get_request_pad(element, name);
#endif
}
