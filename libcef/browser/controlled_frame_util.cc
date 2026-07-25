// Copyright 2026 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#include "cef/libcef/browser/controlled_frame_util.h"

#include "build/build_config.h"
#include "cef/libcef/browser/browser_guest_util.h"
#include "cef/libcef/browser/browser_host_base.h"
#include "cef/libcef/browser/browser_info.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/guest_view/web_view/web_view_guest.h"

namespace controlled_frame_util {

bool IsRequested(CefRefPtr<CefDictionaryValue> extra_info) {
#if BUILDFLAG(IS_WIN)
  return extra_info && extra_info->GetBool(kEnabledExtraInfoKey);
#else
  return false;
#endif
}

bool IsEnabled(const CefBrowserHostBase* browser) {
  if (!browser || !browser->IsAlloyStyle()) {
    return false;
  }

  const auto& browser_info = browser->browser_info();
  return !browser_info->is_popup() && !browser_info->config().is_windowless &&
         browser_info->config().controlled_frame_enabled;
}

bool IsEnabledOwnerFrame(content::RenderFrameHost* frame) {
  if (!frame || !frame->IsInPrimaryMainFrame()) {
    return false;
  }

  auto* web_contents = content::WebContents::FromRenderFrameHost(frame);
  if (!web_contents || web_contents->GetPrimaryMainFrame() != frame ||
      IsBrowserPluginGuest(web_contents)) {
    return false;
  }

  auto browser = CefBrowserHostBase::GetBrowserForContents(web_contents);
  return IsEnabled(browser.get());
}

bool IsEnabledGuest(content::WebContents* web_contents) {
  if (!web_contents || !IsBrowserPluginGuest(web_contents)) {
    return false;
  }

  auto* guest = extensions::WebViewGuest::FromWebContents(web_contents);
  return guest && guest->IsOwnedByControlledFrameEmbedder() &&
         IsEnabledOwnerFrame(guest->owner_rfh());
}

}  // namespace controlled_frame_util
