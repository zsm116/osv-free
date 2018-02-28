// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/compositor/gpu_vsync_begin_frame_source.h"

namespace content {

GpuVSyncBeginFrameSource::GpuVSyncBeginFrameSource(
    GpuVSyncControl* vsync_control)
    : cc::ExternalBeginFrameSource(this),
      vsync_control_(vsync_control),
      needs_begin_frames_(false),
      next_sequence_number_(cc::BeginFrameArgs::kStartingFrameNumber) {
  DCHECK(vsync_control);
}

GpuVSyncBeginFrameSource::~GpuVSyncBeginFrameSource() = default;

void GpuVSyncBeginFrameSource::OnVSync(base::TimeTicks timestamp,
                                       base::TimeDelta interval) {
  if (!needs_begin_frames_)
    return;

  base::TimeTicks now = Now();
  base::TimeTicks deadline = now.SnappedToNextTick(timestamp, interval);

  TRACE_EVENT1("cc", "GpuVSyncBeginFrameSource::OnVSync", "latency",
               (now - timestamp).ToInternalValue());

  next_sequence_number_++;
  OnBeginFrame(cc::BeginFrameArgs::Create(
      BEGINFRAME_FROM_HERE, source_id(), next_sequence_number_, timestamp,
      deadline, interval, cc::BeginFrameArgs::NORMAL));
}

void GpuVSyncBeginFrameSource::OnNeedsBeginFrames(bool needs_begin_frames) {
  needs_begin_frames_ = needs_begin_frames;
  vsync_control_->SetNeedsVSync(needs_begin_frames);
}

base::TimeTicks GpuVSyncBeginFrameSource::Now() const {
  return base::TimeTicks::Now();
}

cc::BeginFrameArgs GpuVSyncBeginFrameSource::GetMissedBeginFrameArgs(
    cc::BeginFrameObserver* obs) {
  if (!last_begin_frame_args_.IsValid())
    return cc::BeginFrameArgs();

  base::TimeTicks now = Now();
  base::TimeTicks estimated_next_timestamp = now.SnappedToNextTick(
      last_begin_frame_args_.frame_time, last_begin_frame_args_.interval);
  base::TimeTicks missed_timestamp =
      estimated_next_timestamp - last_begin_frame_args_.interval;

  if (missed_timestamp > last_begin_frame_args_.frame_time) {
    // The projected missed timestamp is newer than the last known timestamp.
    // In this case create BeginFrameArgs with a new sequence number.
    next_sequence_number_++;
    last_begin_frame_args_ = cc::BeginFrameArgs::Create(
        BEGINFRAME_FROM_HERE, source_id(), next_sequence_number_,
        missed_timestamp, estimated_next_timestamp,
        last_begin_frame_args_.interval, cc::BeginFrameArgs::MISSED);
    return last_begin_frame_args_;
  }

  // The last known args object is up-to-date. Skip sending notification
  // if the observer has already seen it.
  const cc::BeginFrameArgs& last_observer_args = obs->LastUsedBeginFrameArgs();
  if (last_observer_args.IsValid() &&
      last_begin_frame_args_.frame_time <= last_observer_args.frame_time) {
    return cc::BeginFrameArgs();
  }

  cc::BeginFrameArgs missed_args = last_begin_frame_args_;
  missed_args.type = cc::BeginFrameArgs::MISSED;
  return missed_args;
}

}  // namespace content
