// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/android/selection_popup_controller.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/android/scoped_java_ref.h"
#include "content/browser/renderer_host/render_widget_host_view_android.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/context_menu_params.h"
#include "jni/SelectionPopupController_jni.h"
#include "third_party/WebKit/public/web/WebContextMenuData.h"

using base::android::AttachCurrentThread;
using base::android::ConvertUTF16ToJavaString;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;
using blink::WebContextMenuData;

namespace content {

void Init(JNIEnv* env,
          const JavaParamRef<jobject>& obj,
          const JavaParamRef<jobject>& jweb_contents) {
  WebContents* web_contents = WebContents::FromJavaWebContents(jweb_contents);
  DCHECK(web_contents);

  // Owns itself and gets destroyed when |WebContentsDestroyed| is called.
  (new SelectionPopupController(env, obj, web_contents))->Initialize();
}

bool RegisterSelectionPopupController(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

SelectionPopupController::SelectionPopupController(
    JNIEnv* env,
    const JavaParamRef<jobject>& obj,
    WebContents* web_contents)
    : RenderWidgetHostConnector(web_contents) {
  java_obj_ = JavaObjectWeakGlobalRef(env, obj);
}

void SelectionPopupController::UpdateRenderProcessConnection(
    RenderWidgetHostViewAndroid* old_rwhva,
    RenderWidgetHostViewAndroid* new_rwhva) {
  if (old_rwhva)
    old_rwhva->set_selection_popup_controller(nullptr);
  if (new_rwhva)
    new_rwhva->set_selection_popup_controller(this);
}

void SelectionPopupController::OnSelectionEvent(
    ui::SelectionEventType event,
    const gfx::RectF& selection_rect) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_obj_.get(env);
  if (obj.is_null())
    return;

  Java_SelectionPopupController_onSelectionEvent(
      env, obj, event, selection_rect.x(), selection_rect.y(),
      selection_rect.right(), selection_rect.bottom());
}

void SelectionPopupController::OnSelectionChanged(const std::string& text) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_obj_.get(env);
  if (obj.is_null())
    return;
  ScopedJavaLocalRef<jstring> jtext = ConvertUTF8ToJavaString(env, text);
  Java_SelectionPopupController_onSelectionChanged(env, obj, jtext);
}

bool SelectionPopupController::ShowSelectionMenu(
    const ContextMenuParams& params,
    int handle_height) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_obj_.get(env);
  if (obj.is_null())
    return false;

  // Display paste pop-up only when selection is empty and editable.
  const bool from_touch = params.source_type == ui::MENU_SOURCE_TOUCH ||
                          params.source_type == ui::MENU_SOURCE_LONG_PRESS ||
                          params.source_type == ui::MENU_SOURCE_TOUCH_HANDLE ||
                          params.source_type == ui::MENU_SOURCE_STYLUS;
  if (!from_touch || (!params.is_editable && params.selection_text.empty()))
    return false;

  const bool can_select_all =
      !!(params.edit_flags & WebContextMenuData::kCanSelectAll);
  const bool can_edit_richly =
      !!(params.edit_flags & blink::WebContextMenuData::kCanEditRichly);
  const bool is_password_type =
      params.input_field_type ==
      blink::WebContextMenuData::kInputFieldTypePassword;
  const ScopedJavaLocalRef<jstring> jselected_text =
      ConvertUTF16ToJavaString(env, params.selection_text);
  const bool should_suggest = params.source_type == ui::MENU_SOURCE_TOUCH ||
                              params.source_type == ui::MENU_SOURCE_LONG_PRESS;

  Java_SelectionPopupController_showSelectionMenu(
      env, obj, params.selection_rect.x(), params.selection_rect.y(),
      params.selection_rect.right(),
      params.selection_rect.bottom() + handle_height, params.is_editable,
      is_password_type, jselected_text, can_select_all, can_edit_richly,
      should_suggest);
  return true;
}

void SelectionPopupController::OnShowUnhandledTapUIIfNeeded(int x_dip,
                                                            int y_dip,
                                                            float dip_scale) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_obj_.get(env);
  if (obj.is_null())
    return;
  Java_SelectionPopupController_onShowUnhandledTapUIIfNeeded(
      env, obj, static_cast<jint>(x_dip * dip_scale),
      static_cast<jint>(y_dip * dip_scale));
}

void SelectionPopupController::OnSelectWordAroundCaretAck(bool did_select,
                                                          int start_adjust,
                                                          int end_adjust) {
  JNIEnv* env = AttachCurrentThread();
  ScopedJavaLocalRef<jobject> obj = java_obj_.get(env);
  if (obj.is_null())
    return;
  Java_SelectionPopupController_onSelectWordAroundCaretAck(
      env, obj, did_select, start_adjust, end_adjust);
}

}  // namespace content
