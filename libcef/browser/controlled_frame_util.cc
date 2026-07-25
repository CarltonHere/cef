// Copyright 2026 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#include "cef/libcef/browser/controlled_frame_util.h"

#include <optional>
#include <set>
#include <string>

#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "build/build_config.h"
#include "cef/libcef/browser/browser_guest_util.h"
#include "cef/libcef/browser/browser_host_base.h"
#include "cef/libcef/browser/browser_info.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/guest_view/web_view/web_view_guest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace controlled_frame_util {

namespace {

class OwnerOriginRegistry {
 public:
  static OwnerOriginRegistry& Get() {
    static base::NoDestructor<OwnerOriginRegistry> instance;
    return *instance;
  }

  void Add(const url::Origin& origin) {
    base::AutoLock lock(lock_);
    origins_.insert(origin);
  }

  bool Contains(const url::Origin& origin) const {
    base::AutoLock lock(lock_);
    return origins_.find(origin) != origins_.end();
  }

 private:
  mutable base::Lock lock_;
  std::set<url::Origin> origins_ GUARDED_BY(lock_);
};

// Returns the owner origin requested for a browser, if any.
std::optional<url::Origin> GetRequestedOwnerOrigin(
    CefRefPtr<CefDictionaryValue> extra_info) {
#if BUILDFLAG(IS_WIN)
  if (!extra_info || !extra_info->HasKey(kOwnerOriginExtraInfoKey)) {
    return std::nullopt;
  }

  const std::string value =
      extra_info->GetString(kOwnerOriginExtraInfoKey).ToString();
  if (value.empty()) {
    return std::nullopt;
  }

  auto origin = url::Origin::Create(GURL(value));
  if (origin.opaque()) {
    return std::nullopt;
  }
  return origin;
#else
  return std::nullopt;
#endif
}

// Returns true when Controlled Frame is enabled for an existing browser.
bool IsEnabled(const CefBrowserHostBase* browser) {
  if (!browser || !browser->IsAlloyStyle()) {
    return false;
  }

  const auto& browser_info = browser->browser_info();
  return !browser_info->is_popup() && !browser_info->config().is_windowless &&
         GetRequestedOwnerOrigin(browser_info->extra_info()).has_value();
}

// Returns true only for the primary main frame of an enabled owner browser.
// BrowserPlugin guests are always rejected to prevent nested guest creation.
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

}  // namespace

void MaybeRegisterOwnerOrigin(CefRefPtr<CefDictionaryValue> extra_info) {
  if (auto origin = GetRequestedOwnerOrigin(extra_info)) {
    OwnerOriginRegistry::Get().Add(*origin);
  }
}

bool IsOwnerOrigin(const GURL& url) {
  auto origin = url::Origin::Create(url);
  if (origin.opaque()) {
    return false;
  }
  return OwnerOriginRegistry::Get().Contains(origin);
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
