// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/compositor/test/no_transport_image_transport_factory.h"

#include <utility>

#include "base/memory/ptr_util.h"
#include "build/build_config.h"
#include "cc/output/context_provider.h"
#include "cc/surfaces/surface_manager.h"
#include "components/viz/service/display_compositor/gl_helper.h"
#include "content/browser/compositor/surface_utils.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "ui/compositor/compositor.h"

namespace content {

NoTransportImageTransportFactory::NoTransportImageTransportFactory()
    : frame_sink_manager_(false /* use surface references */, nullptr),
      context_factory_(&frame_sink_manager_host_,
                       frame_sink_manager_.surface_manager()) {
  surface_utils::ConnectWithInProcessFrameSinkManager(&frame_sink_manager_host_,
                                                      &frame_sink_manager_);

  // The context factory created here is for unit tests, thus using a higher
  // refresh rate to spend less time waiting for BeginFrames.
  context_factory_.SetUseFastRefreshRateForTests();
}

NoTransportImageTransportFactory::~NoTransportImageTransportFactory() {
  std::unique_ptr<viz::GLHelper> lost_gl_helper = std::move(gl_helper_);
  context_factory_.SendOnLostResources();
}

ui::ContextFactory* NoTransportImageTransportFactory::GetContextFactory() {
  return &context_factory_;
}

ui::ContextFactoryPrivate*
NoTransportImageTransportFactory::GetContextFactoryPrivate() {
  return &context_factory_;
}

viz::GLHelper* NoTransportImageTransportFactory::GetGLHelper() {
  if (!gl_helper_) {
    context_provider_ = context_factory_.SharedMainThreadContextProvider();
    gl_helper_.reset(new viz::GLHelper(context_provider_->ContextGL(),
                                       context_provider_->ContextSupport()));
  }
  return gl_helper_.get();
}

void NoTransportImageTransportFactory::SetGpuChannelEstablishFactory(
    gpu::GpuChannelEstablishFactory* factory) {}

}  // namespace content
