// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/child/indexed_db/webidbcursor_impl.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/test/scoped_task_environment.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/values.h"
#include "content/child/indexed_db/indexed_db_key_builders.h"
#include "content/child/indexed_db/mock_webidbcallbacks.h"
#include "content/child/thread_safe_sender.h"
#include "content/common/indexed_db/indexed_db_key.h"
#include "mojo/public/cpp/bindings/associated_binding.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/public/platform/WebData.h"

using blink::WebBlobInfo;
using blink::WebData;
using blink::WebIDBCallbacks;
using blink::WebIDBKey;
using blink::kWebIDBKeyTypeNumber;
using blink::WebIDBValue;
using blink::WebVector;
using indexed_db::mojom::Cursor;
using testing::StrictMock;

namespace content {

namespace {

class MockCursorImpl : public Cursor {
 public:
  explicit MockCursorImpl(indexed_db::mojom::CursorAssociatedRequest request)
      : binding_(this, std::move(request)) {
    binding_.set_connection_error_handler(
        base::Bind(&MockCursorImpl::CursorDestroyed, base::Unretained(this)));
  }

  void Prefetch(
      int32_t count,
      ::indexed_db::mojom::CallbacksAssociatedPtrInfo callbacks) override {
    ++prefetch_calls_;
    last_prefetch_count_ = count;
  }

  void PrefetchReset(
      int32_t used_prefetches,
      int32_t unused_prefetches,
      const std::vector<std::string>& unused_blob_uuids) override {
    ++reset_calls_;
    last_used_count_ = used_prefetches;
  }

  void Advance(
      uint32_t count,
      ::indexed_db::mojom::CallbacksAssociatedPtrInfo callbacks) override {
    ++advance_calls_;
  }

  void Continue(
      const IndexedDBKey& key,
      const IndexedDBKey& primary_key,
      ::indexed_db::mojom::CallbacksAssociatedPtrInfo callbacks) override {
    ++continue_calls_;
  }

  void CursorDestroyed() { destroyed_ = true; }

  int prefetch_calls() { return prefetch_calls_; }
  int last_prefetch_count() { return last_prefetch_count_; }
  int reset_calls() { return reset_calls_; }
  int last_used_count() { return last_used_count_; }
  int advance_calls() { return advance_calls_; }
  int continue_calls() { return continue_calls_; }
  bool destroyed() { return destroyed_; }

 private:
  int prefetch_calls_ = 0;
  int last_prefetch_count_ = 0;
  int reset_calls_ = 0;
  int last_used_count_ = 0;
  int advance_calls_ = 0;
  int continue_calls_ = 0;
  bool destroyed_ = false;

  mojo::AssociatedBinding<Cursor> binding_;
};

class MockContinueCallbacks : public StrictMock<MockWebIDBCallbacks> {
 public:
  MockContinueCallbacks(IndexedDBKey* key = 0,
                        WebVector<WebBlobInfo>* webBlobInfo = 0)
      : key_(key), web_blob_info_(webBlobInfo) {}

  void OnSuccess(const WebIDBKey& key,
                 const WebIDBKey& primaryKey,
                 const WebIDBValue& value) override {
    if (key_)
      *key_ = IndexedDBKeyBuilder::Build(key);
    if (web_blob_info_)
      *web_blob_info_ = value.web_blob_info;
  }

 private:
  IndexedDBKey* key_;
  WebVector<WebBlobInfo>* web_blob_info_;
};

}  // namespace

class WebIDBCursorImplTest : public testing::Test {
 public:
  WebIDBCursorImplTest() {
    null_key_.AssignNull();
    indexed_db::mojom::CursorAssociatedPtr ptr;
    mock_cursor_ =
        base::MakeUnique<MockCursorImpl>(mojo::MakeIsolatedRequest(&ptr));
    cursor_ = base::MakeUnique<WebIDBCursorImpl>(
        ptr.PassInterface(), 1, base::ThreadTaskRunnerHandle::Get());
  }

 protected:
  base::test::ScopedTaskEnvironment scoped_task_environment_;
  WebIDBKey null_key_;
  std::unique_ptr<WebIDBCursorImpl> cursor_;
  std::unique_ptr<MockCursorImpl> mock_cursor_;

 private:
  DISALLOW_COPY_AND_ASSIGN(WebIDBCursorImplTest);
};

TEST_F(WebIDBCursorImplTest, PrefetchTest) {
  // Call continue() until prefetching should kick in.
  int continue_calls = 0;
  EXPECT_EQ(mock_cursor_->continue_calls(), 0);
  for (int i = 0; i < WebIDBCursorImpl::kPrefetchContinueThreshold; ++i) {
    cursor_->Continue(null_key_, null_key_, new MockContinueCallbacks());
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(++continue_calls, mock_cursor_->continue_calls());
    EXPECT_EQ(0, mock_cursor_->prefetch_calls());
  }

  // Do enough repetitions to verify that the count grows each time,
  // but not so many that the maximum limit is hit.
  const int kPrefetchRepetitions = 5;

  int expected_key = 0;
  int last_prefetch_count = 0;
  for (int repetitions = 0; repetitions < kPrefetchRepetitions; ++repetitions) {
    // Initiate the prefetch
    cursor_->Continue(null_key_, null_key_, new MockContinueCallbacks());
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(continue_calls, mock_cursor_->continue_calls());
    EXPECT_EQ(repetitions + 1, mock_cursor_->prefetch_calls());

    // Verify that the requested count has increased since last time.
    int prefetch_count = mock_cursor_->last_prefetch_count();
    EXPECT_GT(prefetch_count, last_prefetch_count);
    last_prefetch_count = prefetch_count;

    // Fill the prefetch cache as requested.
    std::vector<IndexedDBKey> keys;
    std::vector<IndexedDBKey> primary_keys(prefetch_count);
    std::vector<WebIDBValue> values(prefetch_count);
    for (int i = 0; i < prefetch_count; ++i) {
      keys.push_back(IndexedDBKey(expected_key + i, kWebIDBKeyTypeNumber));
      values[i].web_blob_info =
          WebVector<WebBlobInfo>(static_cast<size_t>(expected_key + i));
    }
    cursor_->SetPrefetchData(keys, primary_keys, values);

    // Note that the real dispatcher would call cursor->CachedContinue()
    // immediately after cursor->SetPrefetchData() to service the request
    // that initiated the prefetch.

    // Verify that the cache is used for subsequent continue() calls.
    for (int i = 0; i < prefetch_count; ++i) {
      IndexedDBKey key;
      WebVector<WebBlobInfo> web_blob_info;
      cursor_->Continue(null_key_, null_key_,
                        new MockContinueCallbacks(&key, &web_blob_info));
      base::RunLoop().RunUntilIdle();
      EXPECT_EQ(continue_calls, mock_cursor_->continue_calls());
      EXPECT_EQ(repetitions + 1, mock_cursor_->prefetch_calls());

      EXPECT_EQ(kWebIDBKeyTypeNumber, key.type());
      EXPECT_EQ(expected_key, static_cast<int>(web_blob_info.size()));
      EXPECT_EQ(expected_key++, key.number());
    }
  }

  cursor_.reset();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(mock_cursor_->destroyed());
}

TEST_F(WebIDBCursorImplTest, AdvancePrefetchTest) {
  // Call continue() until prefetching should kick in.
  EXPECT_EQ(0, mock_cursor_->continue_calls());
  for (int i = 0; i < WebIDBCursorImpl::kPrefetchContinueThreshold; ++i) {
    cursor_->Continue(null_key_, null_key_, new MockContinueCallbacks());
  }
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0, mock_cursor_->prefetch_calls());

  // Initiate the prefetch
  cursor_->Continue(null_key_, null_key_, new MockContinueCallbacks());

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, mock_cursor_->prefetch_calls());
  EXPECT_EQ(static_cast<int>(WebIDBCursorImpl::kPrefetchContinueThreshold),
            mock_cursor_->continue_calls());
  EXPECT_EQ(0, mock_cursor_->advance_calls());

  const int prefetch_count = mock_cursor_->last_prefetch_count();

  // Fill the prefetch cache as requested.
  int expected_key = 0;
  std::vector<IndexedDBKey> keys;
  std::vector<IndexedDBKey> primary_keys(prefetch_count);
  std::vector<WebIDBValue> values(prefetch_count);
  for (int i = 0; i < prefetch_count; ++i) {
    keys.push_back(IndexedDBKey(expected_key + i, kWebIDBKeyTypeNumber));
    values[i].web_blob_info =
        WebVector<WebBlobInfo>(static_cast<size_t>(expected_key + i));
  }
  cursor_->SetPrefetchData(keys, primary_keys, values);

  // Note that the real dispatcher would call cursor->CachedContinue()
  // immediately after cursor->SetPrefetchData() to service the request
  // that initiated the prefetch.

  // Need at least this many in the cache for the test steps.
  ASSERT_GE(prefetch_count, 5);

  // IDBCursor.continue()
  IndexedDBKey key;
  cursor_->Continue(null_key_, null_key_, new MockContinueCallbacks(&key));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0, key.number());

  // IDBCursor.advance(1)
  cursor_->Advance(1, new MockContinueCallbacks(&key));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, key.number());

  // IDBCursor.continue()
  cursor_->Continue(null_key_, null_key_, new MockContinueCallbacks(&key));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2, key.number());

  // IDBCursor.advance(2)
  cursor_->Advance(2, new MockContinueCallbacks(&key));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(4, key.number());

  EXPECT_EQ(0, mock_cursor_->advance_calls());

  // IDBCursor.advance(lots) - beyond the fetched amount
  cursor_->Advance(WebIDBCursorImpl::kMaxPrefetchAmount,
                   new MockContinueCallbacks(&key));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, mock_cursor_->advance_calls());
  EXPECT_EQ(1, mock_cursor_->prefetch_calls());
  EXPECT_EQ(static_cast<int>(WebIDBCursorImpl::kPrefetchContinueThreshold),
            mock_cursor_->continue_calls());

  cursor_.reset();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(mock_cursor_->destroyed());
}

TEST_F(WebIDBCursorImplTest, PrefetchReset) {
  // Call continue() until prefetching should kick in.
  int continue_calls = 0;
  EXPECT_EQ(mock_cursor_->continue_calls(), 0);
  for (int i = 0; i < WebIDBCursorImpl::kPrefetchContinueThreshold; ++i) {
    cursor_->Continue(null_key_, null_key_, new MockContinueCallbacks());
    base::RunLoop().RunUntilIdle();
    EXPECT_EQ(++continue_calls, mock_cursor_->continue_calls());
    EXPECT_EQ(0, mock_cursor_->prefetch_calls());
  }

  // Initiate the prefetch
  cursor_->Continue(null_key_, null_key_, new MockContinueCallbacks());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(continue_calls, mock_cursor_->continue_calls());
  EXPECT_EQ(1, mock_cursor_->prefetch_calls());
  EXPECT_EQ(0, mock_cursor_->reset_calls());

  // Now invalidate it
  cursor_->ResetPrefetchCache();

  // No reset should have been sent since nothing has been received yet.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0, mock_cursor_->reset_calls());

  // Fill the prefetch cache as requested.
  int prefetch_count = mock_cursor_->last_prefetch_count();
  std::vector<IndexedDBKey> keys(prefetch_count);
  std::vector<IndexedDBKey> primary_keys(prefetch_count);
  std::vector<WebIDBValue> values(prefetch_count);
  cursor_->SetPrefetchData(keys, primary_keys, values);

  // No reset should have been sent since prefetch data hasn't been used.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0, mock_cursor_->reset_calls());

  // The real dispatcher would call cursor->CachedContinue(), so do that:
  MockContinueCallbacks callbacks;
  cursor_->CachedContinue(&callbacks);

  // Now the cursor should have reset the rest of the cache.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, mock_cursor_->reset_calls());
  EXPECT_EQ(1, mock_cursor_->last_used_count());

  cursor_.reset();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(mock_cursor_->destroyed());
}

}  // namespace content
