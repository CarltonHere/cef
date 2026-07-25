// Copyright 2026 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#ifndef CEF_LIBCEF_BROWSER_CONTROLLED_FRAME_UTIL_H_
#define CEF_LIBCEF_BROWSER_CONTROLLED_FRAME_UTIL_H_
#pragma once

#include "cef/include/cef_values.h"

class GURL;

namespace content {
class WebContents;
}  // namespace content

namespace controlled_frame_util {

// Private CefBrowser creation extra_info key holding the origin that the
// embedder wants to host Controlled Frame. This is intentionally not exposed
// through the public CEF API. The grant lasts for the process lifetime, so the
// embedder must keep that origin on trusted shell content.
inline constexpr char kOwnerOriginExtraInfoKey[] =
    "cef.controlled_frame.owner_origin";

// Adds the requested owner origin to the registry consulted by
// ChromeContentBrowserClientCef::ShouldUrlUseApplicationIsolationLevel. Called
// at browser creation so that the grant is in place before the owner document
// starts loading.
void MaybeRegisterOwnerOrigin(CefRefPtr<CefDictionaryValue> extra_info);

// Returns true when |url| belongs to a registered owner origin. Registrations
// are never removed: Chromium requires a consistent WebExposedIsolationInfo
// within a BrowsingInstance, so an origin that was once isolated must not drop
// back to a non-isolated level while the process is alive.
bool IsOwnerOrigin(const GURL& url);

// Returns true only for a ControlledFrame guest belonging to an enabled owner.
bool IsEnabledGuest(content::WebContents* web_contents);

}  // namespace controlled_frame_util

#endif  // CEF_LIBCEF_BROWSER_CONTROLLED_FRAME_UTIL_H_
