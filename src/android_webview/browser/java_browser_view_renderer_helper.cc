// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/java_browser_view_renderer_helper.h"

#include <android/bitmap.h>

#include "android_webview/public/browser/draw_sw.h"
#include "base/android/scoped_java_ref.h"
#include "base/memory/ptr_util.h"
#include "base/trace_event/trace_event.h"
#include "jni/JavaBrowserViewRendererHelper_jni.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/utils/SkCanvasStateUtils.h"

using base::android::ScopedJavaLocalRef;

namespace android_webview {

namespace {

// Provides software rendering functions from the Android glue layer.
// Allows preventing extra copies of data when rendering.
AwDrawSWFunctionTable* g_sw_draw_functions = NULL;

class JavaCanvasHolder : public SoftwareCanvasHolder {
 public:
  JavaCanvasHolder(JNIEnv* env,
                   jobject java_canvas,
                   const gfx::Vector2d& scroll_correction);
  ~JavaCanvasHolder() override;

  SkCanvas* GetCanvas() override;

 private:
  AwPixelInfo* pixels_;
  std::unique_ptr<SkCanvas> canvas_;
  DISALLOW_COPY_AND_ASSIGN(JavaCanvasHolder);
};

JavaCanvasHolder::JavaCanvasHolder(JNIEnv* env,
                                   jobject java_canvas,
                                   const gfx::Vector2d& scroll)
    : pixels_(nullptr) {
  if (!g_sw_draw_functions)
    return;
  pixels_ = g_sw_draw_functions->access_pixels(env, java_canvas);
  if (!pixels_ || !pixels_->state)
    return;

  canvas_ = SkCanvasStateUtils::MakeFromCanvasState(pixels_->state);
  // Workarounds for http://crbug.com/271096: SW draw only supports
  // translate & scale transforms, and a simple rectangular clip.
  if (canvas_ && (!canvas_->isClipRect() ||
                  (canvas_->getTotalMatrix().getType() &
                   ~(SkMatrix::kTranslate_Mask | SkMatrix::kScale_Mask)))) {
    canvas_.reset();
  }
  if (canvas_) {
    canvas_->translate(scroll.x(), scroll.y());
  }
}

JavaCanvasHolder::~JavaCanvasHolder() {
  if (pixels_)
    g_sw_draw_functions->release_pixels(pixels_);
  pixels_ = nullptr;
}

SkCanvas* JavaCanvasHolder::GetCanvas() {
  return canvas_.get();
}

class AuxiliaryCanvasHolder : public SoftwareCanvasHolder {
 public:
  AuxiliaryCanvasHolder(JNIEnv* env,
                        jobject java_canvas,
                        const gfx::Vector2d& scroll_correction,
                        const gfx::Size size);
  ~AuxiliaryCanvasHolder() override;

  SkCanvas* GetCanvas() override;

 private:
  ScopedJavaLocalRef<jobject> jcanvas_;
  ScopedJavaLocalRef<jobject> jbitmap_;
  gfx::Vector2d scroll_;
  std::unique_ptr<SkBitmap> bitmap_;
  std::unique_ptr<SkCanvas> canvas_;
  DISALLOW_COPY_AND_ASSIGN(AuxiliaryCanvasHolder);
};

AuxiliaryCanvasHolder::AuxiliaryCanvasHolder(
    JNIEnv* env,
    jobject java_canvas,
    const gfx::Vector2d& scroll_correction,
    const gfx::Size size)
    : jcanvas_(env, java_canvas), scroll_(scroll_correction) {
  DCHECK(size.width() > 0);
  DCHECK(size.height() > 0);
  jbitmap_ = Java_JavaBrowserViewRendererHelper_createBitmap(
      env, size.width(), size.height(), jcanvas_);
  if (!jbitmap_.obj())
    return;

  AndroidBitmapInfo bitmap_info;
  if (AndroidBitmap_getInfo(env, jbitmap_.obj(), &bitmap_info) < 0) {
    LOG(ERROR) << "Error getting java bitmap info.";
    return;
  }

  void* pixels = nullptr;
  if (AndroidBitmap_lockPixels(env, jbitmap_.obj(), &pixels) < 0) {
    LOG(ERROR) << "Error locking java bitmap pixels.";
    return;
  }

  SkImageInfo info =
      SkImageInfo::MakeN32Premul(bitmap_info.width, bitmap_info.height);
  bitmap_.reset(new SkBitmap);
  bitmap_->installPixels(info, pixels, bitmap_info.stride);
  canvas_ = base::MakeUnique<SkCanvas>(*bitmap_);
}

AuxiliaryCanvasHolder::~AuxiliaryCanvasHolder() {
  bitmap_.reset();

  JNIEnv* env = base::android::AttachCurrentThread();
  if (AndroidBitmap_unlockPixels(env, jbitmap_.obj()) < 0) {
    LOG(ERROR) << "Error unlocking java bitmap pixels.";
    return;
  }

  Java_JavaBrowserViewRendererHelper_drawBitmapIntoCanvas(
      env, jbitmap_, jcanvas_, scroll_.x(), scroll_.y());
}

SkCanvas* AuxiliaryCanvasHolder::GetCanvas() {
  return canvas_.get();
}

}  // namespace

void RasterHelperSetAwDrawSWFunctionTable(AwDrawSWFunctionTable* table) {
  g_sw_draw_functions = table;
}

// static
std::unique_ptr<SoftwareCanvasHolder> SoftwareCanvasHolder::Create(
    jobject java_canvas,
    const gfx::Vector2d& scroll_correction,
    const gfx::Size& auxiliary_bitmap_size,
    bool force_auxiliary_bitmap) {
  JNIEnv* env = base::android::AttachCurrentThread();
  std::unique_ptr<SoftwareCanvasHolder> holder;
  if (!force_auxiliary_bitmap) {
    holder.reset(new JavaCanvasHolder(env, java_canvas, scroll_correction));
  }
  if (!holder.get() || !holder->GetCanvas()) {
    holder.reset();
    holder.reset(new AuxiliaryCanvasHolder(env, java_canvas, scroll_correction,
                                           auxiliary_bitmap_size));
  }
  if (!holder->GetCanvas()) {
    holder.reset();
  }
  return holder;
}

}  // namespace android_webview
