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

constexpr char kEnabledExtraInfoKey[] = "cef.controlled_frame.enabled";
constexpr char kOwnerUrl[] = "https://tests/controlled_frame_owner.html";
constexpr char kExposureUrl[] = "https://tests/controlled_frame_exposure.html";
constexpr char kPopupUrl[] = "https://tests/controlled_frame_popup.html";
constexpr char kGuestUrl1[] = "https://guest.test/one";
constexpr char kGuestUrl2[] = "https://guest.test/two";

constexpr char kOwnerHtml[] = R"(
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

CefRefPtr<CefDictionaryValue> CreateEnabledExtraInfo() {
  auto extra_info = CefDictionaryValue::Create();
  extra_info->SetBool(kEnabledExtraInfoKey, true);
  return extra_info;
}

enum class ExposureMode {
  kAlloyDefaultDisabled,
  kChromeOptInRejected,
  kNestedFrameRejected,
  kPopupRejected,
};

class ControlledFrameExposureTestHandler : public TestHandler {
 public:
  explicit ControlledFrameExposureTestHandler(ExposureMode mode)
      : mode_(mode) {}

  void RunTest() override {
    SetUseViews(false);
    SetUseAlloyStyle(mode_ != ExposureMode::kChromeOptInRejected,
                     /*use_alloy_style_window=*/true);

    std::string html;
    if (mode_ == ExposureMode::kNestedFrameRejected) {
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
      AddResource(kPopupUrl,
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

    AddResource(kExposureUrl, html, "text/html");

    CefRefPtr<CefDictionaryValue> extra_info;
    if (mode_ != ExposureMode::kAlloyDefaultDisabled) {
      extra_info = CreateEnabledExtraInfo();
    }
    CreateBrowser(kExposureUrl, nullptr, extra_info);
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
    GrantPopupPermission(browser->GetHost()->GetRequestContext(), kExposureUrl);
    frame->ExecuteJavaScript("window.open('" + std::string(kPopupUrl) + "')",
                             kExposureUrl, 0);
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
    EXPECT_STREQ(kPopupUrl, target_url.ToString().c_str());
    extra_info = CreateEnabledExtraInfo();
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

    EXPECT_STREQ("cf:disabled", title_string.c_str());
    EXPECT_STREQ(
        mode_ == ExposureMode::kPopupRejected ? kPopupUrl : kExposureUrl,
        browser->GetMainFrame()->GetURL().ToString().c_str());
    DestroyTest();
  }

 private:
  const ExposureMode mode_;
  bool popup_requested_ = false;

  IMPLEMENT_REFCOUNTING(ControlledFrameExposureTestHandler);
};

class ControlledFrameNavigationTestHandler : public TestHandler {
 public:
  void RunTest() override {
    SetUseViews(false);
    SetUseAlloyStyle(/*use_alloy_style_browser=*/true,
                     /*use_alloy_style_window=*/true);

    AddResource(kOwnerUrl, kOwnerHtml, "text/html");
    AddResource(kGuestUrl1, "<!doctype html><title>guest-one</title>",
                "text/html");
    AddResource(kGuestUrl2, "<!doctype html><title>guest-two</title>",
                "text/html");
    CreateBrowser(kOwnerUrl, nullptr, CreateEnabledExtraInfo());
    SetTestTimeout();
  }

  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int http_status_code) override {
    if (!frame->IsMain() || finished_) {
      return;
    }

    const std::string url = frame->GetURL();
    if (url != kOwnerUrl) {
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
    if (title_string == "cf:navigation-ok") {
      finished_ = true;
      EXPECT_FALSE(browser->IsPopup());
      EXPECT_STREQ(kOwnerUrl,
                   browser->GetMainFrame()->GetURL().ToString().c_str());
      DestroyTest();
    } else if (title_string == "cf:unavailable" ||
               title_string.rfind("cf:abort:", 0) == 0) {
      finished_ = true;
      ADD_FAILURE() << "ControlledFrame owner reported " << title_string;
      DestroyTest();
    }
  }

 private:
  bool finished_ = false;

  IMPLEMENT_REFCOUNTING(ControlledFrameNavigationTestHandler);
};

void RunExposureTest(ExposureMode mode) {
  CefRefPtr<ControlledFrameExposureTestHandler> handler =
      new ControlledFrameExposureTestHandler(mode);
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

TEST(ControlledFrameTest, RejectsNestedOwner) {
  RunExposureTest(ExposureMode::kNestedFrameRejected);
}

TEST(ControlledFrameTest, RejectsPopupOwner) {
  RunExposureTest(ExposureMode::kPopupRejected);
}

TEST(ControlledFrameTest, AlloyWindowedGuestNavigationKeepsOwnerUrl) {
  CefRefPtr<ControlledFrameNavigationTestHandler> handler =
      new ControlledFrameNavigationTestHandler();
  handler->ExecuteTest();
  ReleaseAndWaitForDestructor(handler);
}

#endif  // defined(OS_WIN)
