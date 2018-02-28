// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "content/shell/browser/shell.h"
#include "ui/base/touch/touch_device.h"

namespace content {

namespace {

class InteractionMediaQueriesDynamicTest : public ContentBrowserTest {
 public:
  InteractionMediaQueriesDynamicTest() = default;
  ~InteractionMediaQueriesDynamicTest() override = default;
};

}  //  namespace

#if defined(OS_WIN) || defined(OS_LINUX)
IN_PROC_BROWSER_TEST_F(InteractionMediaQueriesDynamicTest,
                       PointerMediaQueriesDynamic) {
  GURL test_url = GetTestUrl("", "interaction-mq-dynamic.html");
  const base::string16 kSuccessTitle(base::ASCIIToUTF16("SUCCESS"));
  TitleWatcher title_watcher(shell()->web_contents(), kSuccessTitle);
  NavigateToURL(shell(), test_url);
  RenderViewHost* rvh = shell()->web_contents()->GetRenderViewHost();
  ui::SetAvailablePointerAndHoverTypesForTesting(ui::POINTER_TYPE_COARSE,
                                                 ui::HOVER_TYPE_HOVER);
  rvh->OnWebkitPreferencesChanged();
  EXPECT_EQ(kSuccessTitle, title_watcher.WaitAndGetTitle());
}
#endif

}  //  namespace content
