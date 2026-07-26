// Copyright 2026 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#include <string>

#include "include/cef_values.h"
#include "tests/ceftests/test_handler.h"
#include "tests/ceftests/test_util.h"
#include "tests/gtest/include/gtest/gtest.h"

#if defined(OS_WIN)

namespace {

constexpr char kOwnerOriginExtraInfoKey[] = "cef.controlled_frame.owner_origin";
constexpr char kGuestUrl1[] = "https://guest.test/one";
constexpr char kGuestUrl2[] = "https://guest.test/two";

// Controlled Frame requires the owner document to reach the "isolated
// application" web exposed isolation level, which requires these headers in
// addition to the embedder opt-in.
ResourceContent::HeaderMap IsolationHeaders() {
  return {{"Cross-Origin-Opener-Policy", "same-origin"},
          {"Cross-Origin-Embedder-Policy", "require-corp"}};
}

CefRefPtr<CefDictionaryValue> CreateOwnerExtraInfo(const std::string& origin) {
  auto extra_info = CefDictionaryValue::Create();
  extra_info->SetString(kOwnerOriginExtraInfoKey, origin);
  return extra_info;
}

constexpr char kNavigationOwnerHtml[] = R"(
<!doctype html>
<meta charset="utf-8">
<title>cf:ready</title>
<style>controlledframe { display: block; width: 320px; height: 240px; }</style>
<body>
<script>
const firstUrl = 'https://guest.test/one';
const secondUrl = 'https://guest.test/two';
const guest = document.createElement('controlledframe');
if (!('src' in guest)) {
  document.title = 'cf:unavailable';
} else {
  let committedUrl = '';
  let firstLoaded = false;
  guest.setAttribute('partition', 'persist:cef-controlled-frame-test');
  guest.addEventListener('loadcommit', event => {
    if (event.isTopLevel) {
      committedUrl = event.url;
    }
  });
  guest.addEventListener('loadstop', () => {
    if (committedUrl === firstUrl && !firstLoaded) {
      firstLoaded = true;
      guest.setAttribute('src', secondUrl);
    } else if (committedUrl === secondUrl && firstLoaded) {
      document.title = 'cf:navigation-ok';
    }
  });
  guest.addEventListener('loadabort', event => {
    document.title = 'cf:abort:' + event.reason;
  });
  guest.setAttribute('src', firstUrl);
  document.body.appendChild(guest);
}
</script>
</body>
)";

constexpr char kScriptOwnerHtml[] = R"(
<!doctype html>
<meta charset="utf-8">
<title>cf:ready</title>
<style>controlledframe { display: block; width: 320px; height: 240px; }</style>
<body>
<script>
const guest = document.createElement('controlledframe');
if (!('src' in guest)) {
  document.title = 'cf:unavailable';
} else {
  let done = false;
  guest.setAttribute('partition', 'persist:cef-controlled-frame-test');
  guest.addEventListener('loadstop', async () => {
    if (done) {
      return;
    }
    done = true;
    try {
      const result = await guest.executeScript({ code: 'document.title' });
      const value = Array.isArray(result) ? result[0] : result;
      document.title = 'cf:script:' + value;
    } catch (error) {
      document.title = 'cf:script-error:' + error;
    }
  });
  guest.addEventListener('loadabort', event => {
    document.title = 'cf:abort:' + event.reason;
  });
  guest.setAttribute('src', 'https://guest.test/one');
  document.body.appendChild(guest);
}
</script>
</body>
)";

enum class ExposureMode {
  kAlloyDefaultDisabled,
  kChromeOptInRejected,
  kNestedFrameAllowed,
  kPopupRejected,
};

// Each mode uses a distinct owner origin. The application isolation grant is
// keyed by origin and is never revoked, so sharing an origin across tests
// would leak the grant from one test into the next.
std::string OwnerOriginForMode(ExposureMode mode) {
  switch (mode) {
    case ExposureMode::kAlloyDefaultDisabled:
      return "https://cf-default.test";
    case ExposureMode::kChromeOptInRejected:
      return "https://cf-chrome.test";
    case ExposureMode::kNestedFrameAllowed:
      return "https://cf-nested.test";
    case ExposureMode::kPopupRejected:
      return "https://cf-popup.test";
  }
  return std::string();
}

class ControlledFrameExposureTestHandler : public TestHandler {
 public:
  explicit ControlledFrameExposureTestHandler(ExposureMode mode)
      : mode_(mode),
        owner_origin_(OwnerOriginForMode(mode)),
        owner_url_(owner_origin_ + "/owner.html"),
        popup_url_(owner_origin_ + "/popup.html") {}

  void RunTest() override {
    SetUseViews(false);
    SetUseAlloyStyle(mode_ != ExposureMode::kChromeOptInRejected,
                     /*use_alloy_style_window=*/true);

    std::string html;
    if (mode_ == ExposureMode::kNestedFrameAllowed) {
      html = R"(
<!doctype html>
<title>cf:ready</title>
<iframe id="child"></iframe>
<script>
addEventListener('message', event => {
  document.title = 'cf:' + (event.data ? 'exposed' : 'disabled');
});
document.querySelector('#child').srcdoc =
  `<script>parent.postMessage(
    'src' in document.createElement('controlledframe'), '*')<\/script>`;
</script>
)";
    } else if (mode_ == ExposureMode::kPopupRejected) {
      html = "<!doctype html><title>cf:ready</title>";
      // Deliberately served without the isolation headers: a same-origin
      // document that does not opt into cross-origin isolation must not reach
      // the application isolation level.
      AddResource(popup_url_,
                  R"(<!doctype html><script>
const exposed = 'src' in document.createElement('controlledframe');
document.title = 'cf:' + (exposed ? 'exposed' : 'disabled');
</script>)",
                  "text/html");
    } else {
      html = R"(
<!doctype html><script>
const exposed = 'src' in document.createElement('controlledframe');
document.title = 'cf:' + (exposed ? 'exposed' : 'disabled');
</script>
)";
    }

    AddResource(owner_url_, html, "text/html", IsolationHeaders());

    CefRefPtr<CefDictionaryValue> extra_info;
    if (mode_ != ExposureMode::kAlloyDefaultDisabled) {
      extra_info = CreateOwnerExtraInfo(owner_origin_);
    }
    CreateBrowser(owner_url_, nullptr, extra_info);
    SetTestTimeout();
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int http_status_code) override {
    if (mode_ != ExposureMode::kPopupRejected || browser->IsPopup() ||
        !frame->IsMain() || popup_requested_) {
      return;
    }

    popup_requested_ = true;
    GrantPopupPermission(browser->GetHost()->GetRequestContext(), owner_url_);
    frame->ExecuteJavaScript("window.open('" + popup_url_ + "')", owner_url_,
                             0);
  }

  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     int popup_id,
                     const CefString& target_url,
                     const CefString& target_frame_name,
                     cef_window_open_disposition_t target_disposition,
                     bool user_gesture,
                     const CefPopupFeatures& popup_features,
                     CefWindowInfo& window_info,
                     CefRefPtr<CefClient>& client,
                     CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info,
                     bool* no_javascript_access) override {
    EXPECT_EQ(ExposureMode::kPopupRejected, mode_);
    EXPECT_STREQ(popup_url_.c_str(), target_url.ToString().c_str());
    extra_info = CreateOwnerExtraInfo(owner_origin_);
    return false;
  }

  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override {
    const std::string title_string = title;
    if (title_string != "cf:disabled" && title_string != "cf:exposed") {
      return;
    }
    if (mode_ == ExposureMode::kPopupRejected && !browser->IsPopup()) {
      return;
    }

    EXPECT_STREQ(mode_ == ExposureMode::kNestedFrameAllowed ? "cf:exposed"
                                                            : "cf:disabled",
                 title_string.c_str());
    EXPECT_STREQ(
        mode_ == ExposureMode::kPopupRejected ? popup_url_.c_str()
                                              : owner_url_.c_str(),
        browser->GetMainFrame()->GetURL().ToString().c_str());
    DestroyTest();
  }

 private:
  const ExposureMode mode_;
  const std::string owner_origin_;
  const std::string owner_url_;
  const std::string popup_url_;
  bool popup_requested_ = false;

  IMPLEMENT_REFCOUNTING(ControlledFrameExposureTestHandler);
};

// Drives a real guest through the owner page and completes when the owner
// reports |success_title|.
class ControlledFrameGuestTestHandler : public TestHandler {
 public:
  ControlledFrameGuestTestHandler(const std::string& owner_origin,
                                  const std::string& owner_html,
                                  const std::string& success_title)
      : owner_origin_(owner_origin),
        owner_url_(owner_origin + "/owner.html"),
        owner_html_(owner_html),
        success_title_(success_title) {}

  void RunTest() override {
    SetUseViews(false);
    SetUseAlloyStyle(/*use_alloy_style_browser=*/true,
                     /*use_alloy_style_window=*/true);

    AddResource(owner_url_, owner_html_, "text/html", IsolationHeaders());
    AddResource(kGuestUrl1, "<!doctype html><title>guest-one</title>",
                "text/html");
    AddResource(kGuestUrl2, "<!doctype html><title>guest-two</title>",
                "text/html");
    CreateBrowser(owner_url_, nullptr, CreateOwnerExtraInfo(owner_origin_));
    SetTestTimeout();
  }

  // Model an embedder that pins the owner document (like a Tauri shell): any
  // navigation reported for this browser that isn't the owner URL is
  // cancelled. Guest navigations are surfaced through the Controlled Frame
  // API instead of OnBeforeBrowse (see throttle_handler.cc); if one were
  // reported here it would resolve to the owner main frame, get cancelled,
  // and the guest would fail with loadabort.
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override {
    return request->GetURL().ToString() != owner_url_;
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int http_status_code) override {
    if (!frame->IsMain() || finished_) {
      return;
    }

    const std::string url = frame->GetURL();
    if (url != owner_url_) {
      finished_ = true;
      ADD_FAILURE() << "ControlledFrame navigation replaced the owner URL with "
                    << url;
      DestroyTest();
    }
  }

  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override {
    if (finished_) {
      return;
    }

    const std::string title_string = title;
    if (title_string == success_title_) {
      finished_ = true;
      EXPECT_FALSE(browser->IsPopup());
      EXPECT_STREQ(owner_url_.c_str(),
                   browser->GetMainFrame()->GetURL().ToString().c_str());
      DestroyTest();
    } else if (title_string == "cf:unavailable" ||
               title_string.rfind("cf:abort:", 0) == 0 ||
               title_string.rfind("cf:script-error:", 0) == 0 ||
               (title_string.rfind("cf:script:", 0) == 0 &&
                title_string != success_title_)) {
      finished_ = true;
      ADD_FAILURE() << "ControlledFrame owner reported " << title_string;
      DestroyTest();
    }
  }

 private:
  const std::string owner_origin_;
  const std::string owner_url_;
  const std::string owner_html_;
  const std::string success_title_;
  bool finished_ = false;

  IMPLEMENT_REFCOUNTING(ControlledFrameGuestTestHandler);
};

void RunExposureTest(ExposureMode mode) {
  CefRefPtr<ControlledFrameExposureTestHandler> handler =
      new ControlledFrameExposureTestHandler(mode);
  handler->ExecuteTest();
  ReleaseAndWaitForDestructor(handler);
}

void RunGuestTest(const std::string& owner_origin,
                  const std::string& owner_html,
                  const std::string& success_title) {
  CefRefPtr<ControlledFrameGuestTestHandler> handler =
      new ControlledFrameGuestTestHandler(owner_origin, owner_html,
                                          success_title);
  handler->ExecuteTest();
  ReleaseAndWaitForDestructor(handler);
}

}  // namespace

TEST(ControlledFrameTest, DisabledByDefault) {
  RunExposureTest(ExposureMode::kAlloyDefaultDisabled);
}

TEST(ControlledFrameTest, RejectsChromeOwner) {
  RunExposureTest(ExposureMode::kChromeOptInRejected);
}

// A same-origin child of an isolated owner inherits the isolation level and
// the default-enabled `controlled-frame` permissions policy, so it gets the
// API. This matches the behavior of a real Isolated Web App in Chrome.
TEST(ControlledFrameTest, AllowsSameOriginNestedFrame) {
  RunExposureTest(ExposureMode::kNestedFrameAllowed);
}

TEST(ControlledFrameTest, RejectsPopupOwner) {
  RunExposureTest(ExposureMode::kPopupRejected);
}

TEST(ControlledFrameTest, AlloyWindowedGuestNavigationKeepsOwnerUrl) {
  RunGuestTest("https://cf-owner.test", kNavigationOwnerHtml,
               "cf:navigation-ok");
}

TEST(ControlledFrameTest, GuestExecuteScript) {
  RunGuestTest("https://cf-script.test", kScriptOwnerHtml,
               "cf:script:guest-one");
}

#endif  // defined(OS_WIN)
