// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/web_contents/web_contents_view_android.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/logging.h"
#include "cc/layers/layer.h"
#include "content/browser/accessibility/browser_accessibility_manager_android.h"
#include "content/browser/android/content_view_core_impl.h"
#include "content/browser/frame_host/interstitial_page_impl.h"
#include "content/browser/renderer_host/render_view_host_factory.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_android.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/common/drop_data.h"
#include "jni/DragEvent_jni.h"
#include "ui/android/overscroll_refresh_handler.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/display/screen.h"
#include "ui/events/android/drag_event_android.h"
#include "ui/events/android/motion_event_android.h"
#include "ui/gfx/android/java_bitmap.h"
#include "ui/gfx/image/image_skia.h"

using base::android::AttachCurrentThread;
using base::android::ConvertJavaStringToUTF16;
using base::android::ConvertUTF16ToJavaString;
using base::android::JavaRef;
using base::android::ScopedJavaLocalRef;

namespace content {

namespace {
void DisplayToScreenInfo(const display::Display& display, ScreenInfo* results) {
  results->rect = display.bounds();
  // TODO(husky): Remove any system controls from availableRect.
  results->available_rect = display.work_area();
  results->device_scale_factor = display.device_scale_factor();
  results->orientation_angle = display.RotationAsDegree();
  results->orientation_type =
      RenderWidgetHostViewBase::GetOrientationTypeForMobile(display);
  results->depth = display.color_depth();
  results->depth_per_component = display.depth_per_component();
  results->is_monochrome = display.is_monochrome();
}
}

// static
void WebContentsView::GetDefaultScreenInfo(ScreenInfo* results) {
  DisplayToScreenInfo(display::Screen::GetScreen()->GetPrimaryDisplay(),
                      results);
}

// static
void SynchronousCompositor::SetClientForWebContents(
    WebContents* contents,
    SynchronousCompositorClient* client) {
  DCHECK(contents);
  DCHECK(client);
  WebContentsViewAndroid* wcva = static_cast<WebContentsViewAndroid*>(
      static_cast<WebContentsImpl*>(contents)->GetView());
  DCHECK(!wcva->synchronous_compositor_client());
  wcva->set_synchronous_compositor_client(client);
  RenderWidgetHostViewAndroid* rwhv = static_cast<RenderWidgetHostViewAndroid*>(
      contents->GetRenderWidgetHostView());
  if (rwhv)
    rwhv->SetSynchronousCompositorClient(client);
}

WebContentsView* CreateWebContentsView(
    WebContentsImpl* web_contents,
    WebContentsViewDelegate* delegate,
    RenderViewHostDelegateView** render_view_host_delegate_view) {
  WebContentsViewAndroid* rv = new WebContentsViewAndroid(
      web_contents, delegate);
  *render_view_host_delegate_view = rv;
  return rv;
}

WebContentsViewAndroid::WebContentsViewAndroid(
    WebContentsImpl* web_contents,
    WebContentsViewDelegate* delegate)
    : web_contents_(web_contents),
      content_view_core_(NULL),
      delegate_(delegate),
      view_(this),
      synchronous_compositor_client_(nullptr) {}

WebContentsViewAndroid::~WebContentsViewAndroid() {
  if (view_.GetLayer())
    view_.GetLayer()->RemoveFromParent();
}

void WebContentsViewAndroid::SetContentViewCore(
    ContentViewCoreImpl* content_view_core) {
  content_view_core_ = content_view_core;
  RenderWidgetHostViewAndroid* rwhv = GetRenderWidgetHostViewAndroid();
  if (rwhv)
    rwhv->SetContentViewCore(content_view_core_);

  if (web_contents_->ShowingInterstitialPage()) {
    rwhv = static_cast<RenderWidgetHostViewAndroid*>(
        web_contents_->GetInterstitialPage()
            ->GetMainFrame()
            ->GetRenderViewHost()
            ->GetWidget()
            ->GetView());
    if (rwhv)
      rwhv->SetContentViewCore(content_view_core_);
  }
}

void WebContentsViewAndroid::SetOverscrollRefreshHandler(
    std::unique_ptr<ui::OverscrollRefreshHandler> overscroll_refresh_handler) {
  overscroll_refresh_handler_ = std::move(overscroll_refresh_handler);
  RenderWidgetHostViewAndroid* rwhv = GetRenderWidgetHostViewAndroid();
  if (rwhv)
    rwhv->OnOverscrollRefreshHandlerAvailable();

  if (web_contents_->ShowingInterstitialPage()) {
    rwhv = static_cast<RenderWidgetHostViewAndroid*>(
        web_contents_->GetInterstitialPage()
            ->GetMainFrame()
            ->GetRenderViewHost()
            ->GetWidget()
            ->GetView());
    if (rwhv)
      rwhv->OnOverscrollRefreshHandlerAvailable();
  }
}

ui::OverscrollRefreshHandler*
WebContentsViewAndroid::GetOverscrollRefreshHandler() const {
  return overscroll_refresh_handler_.get();
}

gfx::NativeView WebContentsViewAndroid::GetNativeView() const {
  return const_cast<gfx::NativeView>(&view_);
}

gfx::NativeView WebContentsViewAndroid::GetContentNativeView() const {
  RenderWidgetHostView* rwhv = web_contents_->GetRenderWidgetHostView();
  if (rwhv)
    return rwhv->GetNativeView();

  // TODO(sievers): This should return null.
  return GetNativeView();
}

RenderWidgetHostViewAndroid*
WebContentsViewAndroid::GetRenderWidgetHostViewAndroid() {
  return static_cast<RenderWidgetHostViewAndroid*>(
      web_contents_->GetRenderWidgetHostView());
}

gfx::NativeWindow WebContentsViewAndroid::GetTopLevelNativeWindow() const {
  return content_view_core_ ? content_view_core_->GetWindowAndroid() : nullptr;
}

void WebContentsViewAndroid::GetScreenInfo(ScreenInfo* result) const {
  // Since API 17 Android supports multiple displays with different properties.

  gfx::NativeView native_view = GetNativeView();
  display::Display display =
      native_view
          ? display::Screen::GetScreen()->GetDisplayNearestView(native_view)
          : display::Screen::GetScreen()->GetPrimaryDisplay();
  DisplayToScreenInfo(display, result);
}

void WebContentsViewAndroid::GetContainerBounds(gfx::Rect* out) const {
  *out = content_view_core_ ? gfx::Rect(content_view_core_->GetViewSize())
                            : gfx::Rect();
}

void WebContentsViewAndroid::SetPageTitle(const base::string16& title) {
  // Do nothing.
}

void WebContentsViewAndroid::SizeContents(const gfx::Size& size) {
  // TODO(klobag): Do we need to do anything else?
  RenderWidgetHostView* rwhv = web_contents_->GetRenderWidgetHostView();
  if (rwhv)
    rwhv->SetSize(size);
}

void WebContentsViewAndroid::Focus() {
  RenderWidgetHostViewAndroid* rwhv = GetRenderWidgetHostViewAndroid();
  if (web_contents_->ShowingInterstitialPage()) {
    web_contents_->GetInterstitialPage()->Focus();
  } else {
    rwhv->Focus();
  }
}

void WebContentsViewAndroid::SetInitialFocus() {
  if (web_contents_->FocusLocationBarByDefault())
    web_contents_->SetFocusToLocationBar(false);
  else
    Focus();
}

void WebContentsViewAndroid::StoreFocus() {
  NOTIMPLEMENTED();
}

void WebContentsViewAndroid::RestoreFocus() {
  NOTIMPLEMENTED();
}

DropData* WebContentsViewAndroid::GetDropData() const {
  NOTIMPLEMENTED();
  return NULL;
}

gfx::Rect WebContentsViewAndroid::GetViewBounds() const {
  if (content_view_core_)
    return gfx::Rect(content_view_core_->GetViewSize());

  return gfx::Rect();
}

void WebContentsViewAndroid::CreateView(
    const gfx::Size& initial_size, gfx::NativeView context) {
}

RenderWidgetHostViewBase* WebContentsViewAndroid::CreateViewForWidget(
    RenderWidgetHost* render_widget_host, bool is_guest_view_hack) {
  if (render_widget_host->GetView()) {
    // During testing, the view will already be set up in most cases to the
    // test view, so we don't want to clobber it with a real one. To verify that
    // this actually is happening (and somebody isn't accidentally creating the
    // view twice), we check for the RVH Factory, which will be set when we're
    // making special ones (which go along with the special views).
    DCHECK(RenderViewHostFactory::has_factory());
    return static_cast<RenderWidgetHostViewBase*>(
        render_widget_host->GetView());
  }
  // Note that while this instructs the render widget host to reference
  // |native_view_|, this has no effect without also instructing the
  // native view (i.e. ContentView) how to obtain a reference to this widget in
  // order to paint it. See ContentView::GetRenderWidgetHostViewAndroid for an
  // example of how this is achieved for InterstitialPages.
  RenderWidgetHostImpl* rwhi = RenderWidgetHostImpl::From(render_widget_host);
  RenderWidgetHostViewAndroid* rwhv =
      new RenderWidgetHostViewAndroid(rwhi, content_view_core_);
  rwhv->SetSynchronousCompositorClient(synchronous_compositor_client_);
  return rwhv;
}

RenderWidgetHostViewBase* WebContentsViewAndroid::CreateViewForPopupWidget(
    RenderWidgetHost* render_widget_host) {
  RenderWidgetHostImpl* rwhi = RenderWidgetHostImpl::From(render_widget_host);
  return new RenderWidgetHostViewAndroid(rwhi, NULL);
}

void WebContentsViewAndroid::RenderViewCreated(RenderViewHost* host) {
}

void WebContentsViewAndroid::RenderViewSwappedIn(RenderViewHost* host) {
}

void WebContentsViewAndroid::SetOverscrollControllerEnabled(bool enabled) {
}

void WebContentsViewAndroid::ShowContextMenu(
    RenderFrameHost* render_frame_host, const ContextMenuParams& params) {
  RenderWidgetHostViewAndroid* view = GetRenderWidgetHostViewAndroid();
  // See if context menu is handled by SelectionController as a selection menu.
  // If not, use the delegate to show it.
  if (view && view->ShowSelectionMenu(params))
    return;

  if (delegate_)
    delegate_->ShowContextMenu(render_frame_host, params);
}

void WebContentsViewAndroid::ShowPopupMenu(
    RenderFrameHost* render_frame_host,
    const gfx::Rect& bounds,
    int item_height,
    double item_font_size,
    int selected_item,
    const std::vector<MenuItem>& items,
    bool right_aligned,
    bool allow_multiple_selection) {
  if (content_view_core_) {
    content_view_core_->ShowSelectPopupMenu(
        render_frame_host, bounds, items, selected_item,
        allow_multiple_selection, right_aligned);
  }
}

void WebContentsViewAndroid::HidePopupMenu() {
  if (content_view_core_)
    content_view_core_->HideSelectPopupMenu();
}

void WebContentsViewAndroid::StartDragging(
    const DropData& drop_data,
    blink::WebDragOperationsMask allowed_ops,
    const gfx::ImageSkia& image,
    const gfx::Vector2d& image_offset,
    const DragEventSourceInfo& event_info,
    RenderWidgetHostImpl* source_rwh) {
  if (drop_data.text.is_null()) {
    // Need to clear drag and drop state in blink.
    OnDragEnded();
    return;
  }

  gfx::NativeView native_view = GetNativeView();
  if (!native_view) {
    // Need to clear drag and drop state in blink.
    OnDragEnded();
    return;
  }

  const SkBitmap* bitmap = image.bitmap();
  SkBitmap dummy_bitmap;

  if (image.size().IsEmpty()) {
    // An empty drag image is possible if the Javascript sets an empty drag
    // image on purpose.
    // Create a dummy 1x1 pixel image to avoid crashes when converting to java
    // bitmap.
    dummy_bitmap.allocN32Pixels(1, 1);
    dummy_bitmap.eraseColor(0);
    bitmap = &dummy_bitmap;
  }

  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jstring> jtext =
      ConvertUTF16ToJavaString(env, drop_data.text.string());

  if (!native_view->StartDragAndDrop(jtext, gfx::ConvertToJavaBitmap(bitmap))) {
    // Need to clear drag and drop state in blink.
    OnDragEnded();
    return;
  }

  if (content_view_core_)
    content_view_core_->HidePopupsAndPreserveSelection();
}

void WebContentsViewAndroid::UpdateDragCursor(blink::WebDragOperation op) {
  // Intentional no-op because Android does not have cursor.
}

bool WebContentsViewAndroid::OnDragEvent(const ui::DragEventAndroid& event) {
  switch (event.action()) {
    case JNI_DragEvent::ACTION_DRAG_ENTERED: {
      std::vector<DropData::Metadata> metadata;
      for (const base::string16& mime_type : event.mime_types()) {
        metadata.push_back(DropData::Metadata::CreateForMimeType(
            DropData::Kind::STRING, mime_type));
      }
      OnDragEntered(metadata, event.GetLocation(), event.GetScreenLocation());
      break;
    }
    case JNI_DragEvent::ACTION_DRAG_LOCATION:
      OnDragUpdated(event.GetLocation(), event.GetScreenLocation());
      break;
    case JNI_DragEvent::ACTION_DROP: {
      DropData drop_data;
      drop_data.did_originate_from_renderer = false;
      JNIEnv* env = AttachCurrentThread();
      base::string16 drop_content =
          ConvertJavaStringToUTF16(env, event.GetJavaContent());
      for (const base::string16& mime_type : event.mime_types()) {
        if (base::EqualsASCII(mime_type, ui::Clipboard::kMimeTypeURIList)) {
          drop_data.url = GURL(drop_content);
        } else if (base::EqualsASCII(mime_type, ui::Clipboard::kMimeTypeText)) {
          drop_data.text = base::NullableString16(drop_content, false);
        } else {
          drop_data.html = base::NullableString16(drop_content, false);
        }
      }

      OnPerformDrop(&drop_data, event.GetLocation(), event.GetScreenLocation());
      break;
    }
    case JNI_DragEvent::ACTION_DRAG_EXITED:
      OnDragExited();
      break;
    case JNI_DragEvent::ACTION_DRAG_ENDED:
      OnDragEnded();
      break;
    case JNI_DragEvent::ACTION_DRAG_STARTED:
      // Nothing meaningful to do.
      break;
  }
  return true;
}

// TODO(paulmeyer): The drag-and-drop calls on GetRenderViewHost()->GetWidget()
// in the following functions will need to be targeted to specific
// RenderWidgetHosts in order to work with OOPIFs. See crbug.com/647249.

void WebContentsViewAndroid::OnDragEntered(
    const std::vector<DropData::Metadata>& metadata,
    const gfx::Point& location,
    const gfx::Point& screen_location) {
  blink::WebDragOperationsMask allowed_ops =
      static_cast<blink::WebDragOperationsMask>(blink::kWebDragOperationCopy |
                                                blink::kWebDragOperationMove);
  web_contents_->GetRenderViewHost()->GetWidget()->
      DragTargetDragEnterWithMetaData(metadata, location, screen_location,
                                      allowed_ops, 0);
}

void WebContentsViewAndroid::OnDragUpdated(const gfx::Point& location,
                                           const gfx::Point& screen_location) {
  blink::WebDragOperationsMask allowed_ops =
      static_cast<blink::WebDragOperationsMask>(blink::kWebDragOperationCopy |
                                                blink::kWebDragOperationMove);
  web_contents_->GetRenderViewHost()->GetWidget()->DragTargetDragOver(
      location, screen_location, allowed_ops, 0);
}

void WebContentsViewAndroid::OnDragExited() {
  web_contents_->GetRenderViewHost()->GetWidget()->DragTargetDragLeave(
      gfx::Point(), gfx::Point());
}

void WebContentsViewAndroid::OnPerformDrop(DropData* drop_data,
                                           const gfx::Point& location,
                                           const gfx::Point& screen_location) {
  web_contents_->GetRenderViewHost()->GetWidget()->FilterDropData(drop_data);
  web_contents_->GetRenderViewHost()->GetWidget()->DragTargetDrop(
      *drop_data, location, screen_location, 0);
}

void WebContentsViewAndroid::OnDragEnded() {
  web_contents_->GetRenderViewHost()->GetWidget()->DragSourceSystemDragEnded();
}

void WebContentsViewAndroid::GotFocus() {
  // This is only used in the views FocusManager stuff but it bleeds through
  // all subclasses. http://crbug.com/21875
}

// This is called when we the renderer asks us to take focus back (i.e., it has
// iterated past the last focusable element on the page).
void WebContentsViewAndroid::TakeFocus(bool reverse) {
  if (web_contents_->GetDelegate() &&
      web_contents_->GetDelegate()->TakeFocus(web_contents_, reverse))
    return;
  web_contents_->GetRenderWidgetHostView()->Focus();
}

bool WebContentsViewAndroid::OnTouchEvent(const ui::MotionEventAndroid& event,
                                          bool for_touch_handle) {
  if (event.GetAction() == ui::MotionEventAndroid::ACTION_DOWN)
    content_view_core_->OnTouchDown(event.GetJavaObject());
  return false;  // let the children handle the actual event.
}

bool WebContentsViewAndroid::OnMouseEvent(const ui::MotionEventAndroid& event) {
  // Hover events can be intercepted when in accessibility mode.
  auto action = event.GetAction();
  if (action != ui::MotionEventAndroid::ACTION_HOVER_ENTER &&
      action != ui::MotionEventAndroid::ACTION_HOVER_EXIT &&
      action != ui::MotionEventAndroid::ACTION_HOVER_MOVE)
    return false;

  auto* manager = static_cast<BrowserAccessibilityManagerAndroid*>(
      web_contents_->GetRootBrowserAccessibilityManager());
  return manager && manager->OnHoverEvent(event);
}

void WebContentsViewAndroid::OnPhysicalBackingSizeChanged() {
  if (web_contents_->GetRenderWidgetHostView())
    web_contents_->SendScreenRects();
}

} // namespace content
