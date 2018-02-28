// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_BACKGROUND_FETCH_BACKGROUND_FETCH_JOB_CONTROLLER_H_
#define CONTENT_BROWSER_BACKGROUND_FETCH_BACKGROUND_FETCH_JOB_CONTROLLER_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "content/browser/background_fetch/background_fetch_registration_id.h"
#include "content/browser/background_fetch/background_fetch_request_info.h"
#include "content/common/background_fetch/background_fetch_types.h"
#include "content/common/content_export.h"
#include "content/public/browser/browser_thread.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {
class URLRequestContextGetter;
}

namespace content {

class BackgroundFetchDataManager;
class BrowserContext;

// The JobController will be responsible for coordinating communication with the
// DownloadManager. It will get requests from the DataManager and dispatch them
// to the DownloadManager. It lives entirely on the IO thread.
class CONTENT_EXPORT BackgroundFetchJobController {
 public:
  enum class State { INITIALIZED, FETCHING, ABORTED, COMPLETED };

  using CompletedCallback =
      base::OnceCallback<void(BackgroundFetchJobController*)>;

  BackgroundFetchJobController(
      const BackgroundFetchRegistrationId& registration_id,
      const BackgroundFetchOptions& options,
      BackgroundFetchDataManager* data_manager,
      BrowserContext* browser_context,
      scoped_refptr<net::URLRequestContextGetter> request_context,
      CompletedCallback completed_callback);
  ~BackgroundFetchJobController();

  // Starts fetching the |initial_fetches|. The controller will continue to
  // fetch new content until all requests have been handled.
  void Start(
      std::vector<scoped_refptr<BackgroundFetchRequestInfo>> initial_requests,
      const net::NetworkTrafficAnnotationTag& traffic_annotation);

  // Updates the representation of this Background Fetch in the user interface
  // to match the given |title|.
  void UpdateUI(const std::string& title);

  // Immediately aborts this Background Fetch by request of the developer.
  void Abort();

  // Returns the current state of this Job Controller.
  State state() const { return state_; }

  // Returns the registration id for which this job is fetching data.
  const BackgroundFetchRegistrationId& registration_id() const {
    return registration_id_;
  }

  // Returns the options with which this job is fetching data.
  const BackgroundFetchOptions& options() const { return options_; }

 private:
  class Core;

  // Requests the download manager to start fetching |request|.
  void StartRequest(scoped_refptr<BackgroundFetchRequestInfo> request,
                    const net::NetworkTrafficAnnotationTag& traffic_annotation);

  // Called when the given |request| has started fetching, after having been
  // assigned the |download_guid| by the download system.
  void DidStartRequest(scoped_refptr<BackgroundFetchRequestInfo> request,
                       const std::string& download_guid);

  // Called when the given |request| has been completed.
  void DidCompleteRequest(scoped_refptr<BackgroundFetchRequestInfo> request);

  // Called when a completed download has been marked as such in the DataManager
  // and the next request, if any, has been read from storage.
  void DidGetNextRequest(scoped_refptr<BackgroundFetchRequestInfo> request);

  // The registration id on behalf of which this controller is fetching data.
  BackgroundFetchRegistrationId registration_id_;

  // Options for the represented background fetch registration.
  BackgroundFetchOptions options_;

  // The current state of this Job Controller.
  State state_ = State::INITIALIZED;

  // Inner core of this job controller which lives on the UI thread.
  std::unique_ptr<Core, BrowserThread::DeleteOnUIThread> ui_core_;
  base::WeakPtr<Core> ui_core_ptr_;

  // The DataManager's lifetime is controlled by the BackgroundFetchContext and
  // will be kept alive until after the JobController is destroyed.
  BackgroundFetchDataManager* data_manager_;

  // Number of outstanding acknowledgements we still expect to receive.
  int pending_completed_file_acknowledgements_ = 0;

  // Callback for when all fetches have been completed.
  CompletedCallback completed_callback_;

  base::WeakPtrFactory<BackgroundFetchJobController> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(BackgroundFetchJobController);
};

}  // namespace content

#endif  // CONTENT_BROWSER_BACKGROUND_FETCH_BACKGROUND_FETCH_JOB_CONTROLLER_H_
