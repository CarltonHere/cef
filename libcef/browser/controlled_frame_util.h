// Copyright 2026 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#ifndef CEF_LIBCEF_BROWSER_CONTROLLED_FRAME_UTIL_H_
#define CEF_LIBCEF_BROWSER_CONTROLLED_FRAME_UTIL_H_
#pragma once

#include "cef/include/cef_values.h"

class CefBrowserHostBase;

namespace content {
class RenderFrameHost;
class WebContents;
}  // namespace content

namespace controlled_frame_util {

// Private CefBrowser creation extra_info key. This is intentionally not
// exposed through the public CEF API. The grant lasts for the browser lifetime,
// so the embedder must keep the primary owner on trusted shell content.
inline constexpr char kEnabledExtraInfoKey[] = "cef.controlled_frame.enabled";

// Returns true when Controlled Frame was explicitly requested for a browser.
bool IsRequested(CefRefPtr<CefDictionaryValue> extra_info);

// Returns true when Controlled Frame is enabled for an existing browser.
bool IsEnabled(const CefBrowserHostBase* browser);

// Returns true only for the primary main frame of an enabled owner browser.
// BrowserPlugin guests are always rejected to prevent nested guest creation.
bool IsEnabledOwnerFrame(content::RenderFrameHost* frame);

// Returns true only for a ControlledFrame guest belonging to an enabled owner.
bool IsEnabledGuest(content::WebContents* web_contents);

}  // namespace controlled_frame_util

#endif  // CEF_LIBCEF_BROWSER_CONTROLLED_FRAME_UTIL_H_
