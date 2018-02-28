// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/android/ime_adapter_android.h"

#include <android/input.h>
#include <algorithm>
#include <vector>

#include "base/android/jni_android.h"
#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "content/browser/frame_host/render_frame_host_impl.h"
#include "content/browser/renderer_host/render_view_host_delegate.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_view_android.h"
#include "content/common/input_messages.h"
#include "content/common/view_messages.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/native_web_keyboard_event.h"
#include "content/public/browser/web_contents.h"
#include "jni/ImeAdapter_jni.h"
#include "third_party/WebKit/public/platform/WebInputEvent.h"
#include "third_party/WebKit/public/platform/WebTextInputType.h"
#include "third_party/WebKit/public/web/WebCompositionUnderline.h"

using base::android::AttachCurrentThread;
using base::android::ConvertJavaStringToUTF16;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;

namespace content {
namespace {

// Maps a java KeyEvent into a NativeWebKeyboardEvent.
// |java_key_event| is used to maintain a globalref for KeyEvent.
// |type| will determine the WebInputEvent type.
// type, |modifiers|, |time_ms|, |key_code|, |unicode_char| is used to create
// WebKeyboardEvent. |key_code| is also needed ad need to treat the enter key
// as a key press of character \r.
NativeWebKeyboardEvent NativeWebKeyboardEventFromKeyEvent(
    JNIEnv* env,
    const base::android::JavaRef<jobject>& java_key_event,
    int type,
    int modifiers,
    jlong time_ms,
    int key_code,
    int scan_code,
    bool is_system_key,
    int unicode_char) {
  return NativeWebKeyboardEvent(env, java_key_event,
                                static_cast<blink::WebInputEvent::Type>(type),
                                modifiers, time_ms / 1000.0, key_code,
                                scan_code, unicode_char, is_system_key);
}

}  // anonymous namespace

bool RegisterImeAdapter(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

jlong Init(JNIEnv* env,
           const JavaParamRef<jobject>& obj,
           const JavaParamRef<jobject>& jweb_contents) {
  WebContents* web_contents = WebContents::FromJavaWebContents(jweb_contents);
  DCHECK(web_contents);
  auto* ime_adapter = new ImeAdapterAndroid(env, obj, web_contents);
  ime_adapter->Initialize();
  return reinterpret_cast<intptr_t>(ime_adapter);
}

// Callback from Java to convert BackgroundColorSpan data to a
// blink::WebCompositionUnderline instance, and append it to |underlines_ptr|.
void AppendBackgroundColorSpan(JNIEnv*,
                               const JavaParamRef<jclass>&,
                               jlong underlines_ptr,
                               jint start,
                               jint end,
                               jint background_color) {
  DCHECK_GE(start, 0);
  DCHECK_GE(end, 0);
  // Do not check |background_color|.
  std::vector<blink::WebCompositionUnderline>* underlines =
      reinterpret_cast<std::vector<blink::WebCompositionUnderline>*>(
          underlines_ptr);
  underlines->push_back(blink::WebCompositionUnderline(
      static_cast<unsigned>(start), static_cast<unsigned>(end),
      SK_ColorTRANSPARENT, false, static_cast<unsigned>(background_color)));
}

// Callback from Java to convert UnderlineSpan data to a
// blink::WebCompositionUnderline instance, and append it to |underlines_ptr|.
void AppendUnderlineSpan(JNIEnv*,
                         const JavaParamRef<jclass>&,
                         jlong underlines_ptr,
                         jint start,
                         jint end) {
  DCHECK_GE(start, 0);
  DCHECK_GE(end, 0);
  std::vector<blink::WebCompositionUnderline>* underlines =
      reinterpret_cast<std::vector<blink::WebCompositionUnderline>*>(
          underlines_ptr);
  underlines->push_back(blink::WebCompositionUnderline(
      static_cast<unsigned>(start), static_cast<unsigned>(end), SK_ColorBLACK,
      false, SK_ColorTRANSPARENT));
}

ImeAdapterAndroid::ImeAdapterAndroid(JNIEnv* env,
                                     const JavaParamRef<jobject>& obj,
                                     WebContents* web_contents)
    : RenderWidgetHostConnector(web_contents), rwhva_(nullptr) {
  java_ime_adapter_ = JavaObjectWeakGlobalRef(env, obj);
}

ImeAdapterAndroid::~ImeAdapterAndroid() {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ime_adapter_.get(env);
  if (!obj.is_null())
    Java_ImeAdapter_destroy(env, obj);
}

void ImeAdapterAndroid::UpdateRenderProcessConnection(
    RenderWidgetHostViewAndroid* old_rwhva,
    RenderWidgetHostViewAndroid* new_rwhva) {
  if (old_rwhva)
    old_rwhva->set_ime_adapter(nullptr);
  if (new_rwhva) {
    new_rwhva->set_ime_adapter(this);
    if (!old_rwhva && new_rwhva) {
      JNIEnv* env = AttachCurrentThread();
      ScopedJavaLocalRef<jobject> obj = java_ime_adapter_.get(env);
      if (!obj.is_null())
        Java_ImeAdapter_onConnectedToRenderProcess(env, obj);
    }
  }
  rwhva_ = new_rwhva;
}

void ImeAdapterAndroid::UpdateState(const TextInputState& state) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ime_adapter_.get(env);
  if (obj.is_null())
    return;

  ScopedJavaLocalRef<jstring> jstring_text =
      ConvertUTF8ToJavaString(env, state.value);
  Java_ImeAdapter_updateState(env, obj, static_cast<int>(state.type),
                              state.flags, state.mode, state.show_ime_if_needed,
                              jstring_text, state.selection_start,
                              state.selection_end, state.composition_start,
                              state.composition_end, state.reply_to_request);
}

void ImeAdapterAndroid::UpdateFrameInfo(
    const gfx::SelectionBound& selection_start,
    float dip_scale,
    float content_offset_ypix) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ime_adapter_.get(env);
  if (obj.is_null())
    return;

  // The CursorAnchorInfo API in Android only supports zero width selection
  // bounds.
  const jboolean has_insertion_marker =
      selection_start.type() == gfx::SelectionBound::CENTER;
  const jboolean is_insertion_marker_visible = selection_start.visible();
  const jfloat insertion_marker_horizontal =
      has_insertion_marker ? selection_start.edge_top().x() : 0.0f;
  const jfloat insertion_marker_top =
      has_insertion_marker ? selection_start.edge_top().y() : 0.0f;
  const jfloat insertion_marker_bottom =
      has_insertion_marker ? selection_start.edge_bottom().y() : 0.0f;

  Java_ImeAdapter_updateFrameInfo(
      env, obj, dip_scale, content_offset_ypix, has_insertion_marker,
      is_insertion_marker_visible, insertion_marker_horizontal,
      insertion_marker_top, insertion_marker_bottom);
}

bool ImeAdapterAndroid::SendKeyEvent(
    JNIEnv* env,
    const JavaParamRef<jobject>&,
    const JavaParamRef<jobject>& original_key_event,
    int type,
    int modifiers,
    jlong time_ms,
    int key_code,
    int scan_code,
    bool is_system_key,
    int unicode_char) {
  if (!rwhva_)
    return false;
  NativeWebKeyboardEvent event = NativeWebKeyboardEventFromKeyEvent(
      env, original_key_event, type, modifiers, time_ms, key_code, scan_code,
      is_system_key, unicode_char);
  rwhva_->SendKeyEvent(event);
  return true;
}

void ImeAdapterAndroid::SetComposingText(JNIEnv* env,
                                         const JavaParamRef<jobject>& obj,
                                         const JavaParamRef<jobject>& text,
                                         const JavaParamRef<jstring>& text_str,
                                         int relative_cursor_pos) {
  RenderWidgetHostImpl* rwhi = GetFocusedWidget();
  if (!rwhi)
    return;

  base::string16 text16 = ConvertJavaStringToUTF16(env, text_str);

  std::vector<blink::WebCompositionUnderline> underlines =
      GetUnderlinesFromSpans(env, obj, text, text16);

  // Default to plain underline if we didn't find any span that we care about.
  if (underlines.empty()) {
    underlines.push_back(blink::WebCompositionUnderline(
        0, text16.length(), SK_ColorBLACK, false, SK_ColorTRANSPARENT));
  }

  // relative_cursor_pos is as described in the Android API for
  // InputConnection#setComposingText, whereas the parameters for
  // ImeSetComposition are relative to the start of the composition.
  if (relative_cursor_pos > 0)
    relative_cursor_pos = text16.length() + relative_cursor_pos - 1;

  rwhi->ImeSetComposition(text16, underlines, gfx::Range::InvalidRange(),
                          relative_cursor_pos, relative_cursor_pos);
}

void ImeAdapterAndroid::CommitText(JNIEnv* env,
                                   const JavaParamRef<jobject>& obj,
                                   const JavaParamRef<jobject>& text,
                                   const JavaParamRef<jstring>& text_str,
                                   int relative_cursor_pos) {
  RenderWidgetHostImpl* rwhi = GetFocusedWidget();
  if (!rwhi)
    return;

  base::string16 text16 = ConvertJavaStringToUTF16(env, text_str);

  std::vector<blink::WebCompositionUnderline> underlines =
      GetUnderlinesFromSpans(env, obj, text, text16);

  // relative_cursor_pos is as described in the Android API for
  // InputConnection#commitText, whereas the parameters for
  // ImeConfirmComposition are relative to the end of the composition.
  if (relative_cursor_pos > 0)
    relative_cursor_pos--;
  else
    relative_cursor_pos -= text16.length();

  rwhi->ImeCommitText(text16, underlines, gfx::Range::InvalidRange(),
                      relative_cursor_pos);
}

void ImeAdapterAndroid::FinishComposingText(JNIEnv* env,
                                            const JavaParamRef<jobject>&) {
  RenderWidgetHostImpl* rwhi = GetFocusedWidget();
  if (!rwhi)
    return;

  rwhi->ImeFinishComposingText(true);
}

void ImeAdapterAndroid::CancelComposition() {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ime_adapter_.get(env);
  if (!obj.is_null())
    Java_ImeAdapter_cancelComposition(env, obj);
}

void ImeAdapterAndroid::FocusedNodeChanged(bool is_editable_node) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ime_adapter_.get(env);
  if (!obj.is_null()) {
    Java_ImeAdapter_focusedNodeChanged(env, obj, is_editable_node);
  }
}

void ImeAdapterAndroid::SetEditableSelectionOffsets(
    JNIEnv*,
    const JavaParamRef<jobject>&,
    int start,
    int end) {
  RenderFrameHost* rfh = GetFocusedFrame();
  if (!rfh)
    return;

  rfh->GetFrameInputHandler()->SetEditableSelectionOffsets(start, end);
}

void ImeAdapterAndroid::SetCharacterBounds(
    const std::vector<gfx::RectF>& character_bounds) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_ime_adapter_.get(env);
  if (obj.is_null())
    return;

  const size_t coordinates_array_size = character_bounds.size() * 4;
  std::unique_ptr<float[]> coordinates_array(new float[coordinates_array_size]);
  for (size_t i = 0; i < character_bounds.size(); ++i) {
    const gfx::RectF& rect = character_bounds[i];
    const size_t coordinates_array_index = i * 4;
    coordinates_array[coordinates_array_index + 0] = rect.x();
    coordinates_array[coordinates_array_index + 1] = rect.y();
    coordinates_array[coordinates_array_index + 2] = rect.right();
    coordinates_array[coordinates_array_index + 3] = rect.bottom();
  }
  Java_ImeAdapter_setCharacterBounds(
      env, obj,
      base::android::ToJavaFloatArray(env, coordinates_array.get(),
                                      coordinates_array_size));
}

void ImeAdapterAndroid::SetComposingRegion(JNIEnv*,
                                           const JavaParamRef<jobject>&,
                                           int start,
                                           int end) {
  RenderFrameHost* rfh = GetFocusedFrame();
  if (!rfh)
    return;

  std::vector<ui::CompositionUnderline> underlines;
  underlines.push_back(ui::CompositionUnderline(0, end - start, SK_ColorBLACK,
                                                false, SK_ColorTRANSPARENT));

  rfh->GetFrameInputHandler()->SetCompositionFromExistingText(start, end,
                                                              underlines);
}

void ImeAdapterAndroid::DeleteSurroundingText(JNIEnv*,
                                              const JavaParamRef<jobject>&,
                                              int before,
                                              int after) {
  RenderFrameHostImpl* rfh =
      static_cast<RenderFrameHostImpl*>(GetFocusedFrame());
  if (rfh)
    rfh->GetFrameInputHandler()->DeleteSurroundingText(before, after);
}

void ImeAdapterAndroid::DeleteSurroundingTextInCodePoints(
    JNIEnv*,
    const JavaParamRef<jobject>&,
    int before,
    int after) {
  RenderFrameHostImpl* rfh =
      static_cast<RenderFrameHostImpl*>(GetFocusedFrame());
  if (rfh) {
    rfh->GetFrameInputHandler()->DeleteSurroundingTextInCodePoints(before,
                                                                   after);
  }
}

bool ImeAdapterAndroid::RequestTextInputStateUpdate(
    JNIEnv* env,
    const JavaParamRef<jobject>&) {
  RenderWidgetHostImpl* rwhi = GetFocusedWidget();
  if (!rwhi)
    return false;
  rwhi->Send(new InputMsg_RequestTextInputStateUpdate(rwhi->GetRoutingID()));
  return true;
}

void ImeAdapterAndroid::RequestCursorUpdate(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj,
    bool immediate_request,
    bool monitor_request) {
  RenderWidgetHostImpl* rwhi = GetFocusedWidget();
  if (!rwhi)
    return;
  rwhi->Send(new InputMsg_RequestCompositionUpdates(
      rwhi->GetRoutingID(), immediate_request, monitor_request));
}

RenderWidgetHostImpl* ImeAdapterAndroid::GetFocusedWidget() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return rwhva_ ? rwhva_->GetFocusedWidget() : nullptr;
}

RenderFrameHost* ImeAdapterAndroid::GetFocusedFrame() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // We get the focused frame from the WebContents of the page. Although
  // |rwhva_->GetFocusedWidget()| does a similar thing, there is no direct way
  // to get a RenderFrameHost from its RWH.
  if (!rwhva_)
    return nullptr;
  RenderWidgetHostImpl* rwh =
      RenderWidgetHostImpl::From(rwhva_->GetRenderWidgetHost());
  if (!rwh || !rwh->delegate())
    return nullptr;

  if (auto* contents = rwh->delegate()->GetAsWebContents())
    return contents->GetFocusedFrame();

  return nullptr;
}

std::vector<blink::WebCompositionUnderline>
ImeAdapterAndroid::GetUnderlinesFromSpans(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj,
    const base::android::JavaParamRef<jobject>& text,
    const base::string16& text16) {
  std::vector<blink::WebCompositionUnderline> underlines;
  // Iterate over spans in |text|, dispatch those that we care about (e.g.,
  // BackgroundColorSpan) to a matching callback (e.g.,
  // AppendBackgroundColorSpan()), and populate |underlines|.
  Java_ImeAdapter_populateUnderlinesFromSpans(
      env, obj, text, reinterpret_cast<jlong>(&underlines));

  // Sort spans by |.startOffset|.
  std::sort(underlines.begin(), underlines.end());

  return underlines;
}

}  // namespace content
