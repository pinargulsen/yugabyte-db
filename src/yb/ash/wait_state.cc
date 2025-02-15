// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/ash/wait_state.h"

#include <arpa/inet.h>

#include "yb/util/debug-util.h"
#include "yb/util/tostring.h"
#include "yb/util/trace.h"

DEFINE_test_flag(bool, yb_enable_ash, false,
    "True to enable Active Session History");
DEFINE_test_flag(bool, export_wait_state_names, yb::IsDebug(),
    "Exports wait-state name as a human understandable string.");
DEFINE_test_flag(bool, trace_ash_wait_code_updates, yb::IsDebug(),
    "Add a trace line whenever the wait state code is updated.");
DEFINE_test_flag(uint32, yb_ash_sleep_at_wait_state_ms, 0,
    "How long to sleep/delay when entering a particular wait state.");
DEFINE_test_flag(uint32, yb_ash_wait_code_to_sleep_at, 0,
    "If enabled, add a sleep/delay when we enter the specified wait state.");
DEFINE_test_flag(bool, export_ash_uuids_as_hex_strings, yb::IsDebug(),
    "Exports wait-state name as a human understandable string.");

namespace yb::ash {

namespace {

// The current wait_state_ for this thread.
thread_local WaitStateInfoPtr threadlocal_wait_state_;
std::atomic_bool TEST_entered_wait_state_code_for_sleep{false};

void MaybeSleepForTests(WaitStateInfo* state, WaitStateCode c) {
  if (GetAtomicFlag(&FLAGS_TEST_yb_ash_wait_code_to_sleep_at) != to_underlying(c)) {
    return;
  }

  TEST_entered_wait_state_code_for_sleep.store(true, std::memory_order_release);
  auto sleep_time_ms = GetAtomicFlag(&FLAGS_TEST_yb_ash_sleep_at_wait_state_ms);
  if (sleep_time_ms <= 0) {
    return;
  }

  if (!state) {
    YB_LOG_EVERY_N_SECS(ERROR, 5) << __func__ << " skipping sleep because WaitStateInfo is null";
    return;
  }

  state->TEST_SleepForTests(sleep_time_ms);
}

}  // namespace

void WaitStateInfo::VTraceTo(Trace* trace, int level, GStringPiece data) {
  VTRACE_TO(level, trace ? trace : Trace::CurrentTrace(), data);
}

void WaitStateInfo::TEST_SleepForTests(uint32_t sleep_time_ms) {
  // An RPC may enter a specific wait state multiple times. In order to limit the total
  // delay caused to an RPC, we will reduce sleep time by a factor of 2 for each sleep.
  // 1 + 1/2 + 1/4 + ... < 2. Thus, the total delay incurred by any RPC will be bounded by
  // 2 * sleep_time_ms.
  auto decayed_sleep_time_ms = sleep_time_ms >> TEST_num_sleeps_;
  if (decayed_sleep_time_ms == 0) {
    return;
  }
  SleepFor(MonoDelta::FromMilliseconds(decayed_sleep_time_ms));
  if (GetAtomicFlag(&FLAGS_TEST_trace_ash_wait_code_updates)) {
    VTrace(0, yb::Format("Slept for $0 ms.", decayed_sleep_time_ms));
  }
  VLOG_IF(1, TEST_num_sleeps_ == 0) << "Sleeping at " << yb::GetStackTrace();
  TEST_num_sleeps_++;
}

bool WaitStateInfo::TEST_EnteredSleep() {
  return TEST_entered_wait_state_code_for_sleep.load(std::memory_order_acquire);
}

void AshMetadata::set_client_host_port(const HostPort &host_port) {
  client_host_port = host_port;
}

std::string AshMetadata::ToString() const {
  return YB_STRUCT_TO_STRING(
      yql_endpoint_tserver_uuid, root_request_id, query_id, session_id, rpc_request_id,
      client_host_port);
}

std::string AshAuxInfo::ToString() const {
  return YB_STRUCT_TO_STRING(table_id, tablet_id, method);
}

void AshAuxInfo::UpdateFrom(const AshAuxInfo &other) {
  if (!other.tablet_id.empty()) {
    tablet_id = other.tablet_id;
  }
  if (!other.table_id.empty()) {
    table_id = other.table_id;
  }
  if (!other.method.empty()) {
    method = other.method;
  }
}

WaitStateInfo::WaitStateInfo(AshMetadata &&meta)
    : metadata_(std::move(meta)) {}

void WaitStateInfo::set_code(WaitStateCode c) {
  if (GetAtomicFlag(&FLAGS_TEST_trace_ash_wait_code_updates)) {
    VTrace(1, __func__);
    VTrace(0, yb::Format("$0", ash::ToString(c)));
  }
  code_ = c;
  MaybeSleepForTests(this, c);
}

WaitStateCode WaitStateInfo::code() const {
  return code_;
}

std::atomic<WaitStateCode>& WaitStateInfo::mutable_code() {
  return code_;
}

std::string WaitStateInfo::ToString() const {
  std::lock_guard lock(mutex_);
  return YB_CLASS_TO_STRING(metadata, code, aux_info);
}

void WaitStateInfo::set_rpc_request_id(int64_t rpc_request_id) {
  std::lock_guard lock(mutex_);
  metadata_.rpc_request_id = rpc_request_id;
}

void WaitStateInfo::set_root_request_id(const Uuid &root_request_id) {
  std::lock_guard lock(mutex_);
  metadata_.root_request_id = root_request_id;
}

void WaitStateInfo::set_query_id(uint64_t query_id) {
  std::lock_guard lock(mutex_);
  metadata_.query_id = query_id;
}

uint64_t WaitStateInfo::query_id() {
  std::lock_guard lock(mutex_);
  return metadata_.query_id;
}

void WaitStateInfo::set_session_id(uint64_t session_id) {
  std::lock_guard lock(mutex_);
  metadata_.session_id = session_id;
}

uint64_t WaitStateInfo::session_id() {
  std::lock_guard lock(mutex_);
  return metadata_.session_id;
}

void WaitStateInfo::set_client_host_port(const HostPort &host_port) {
  std::lock_guard lock(mutex_);
  metadata_.set_client_host_port(host_port);
}

void WaitStateInfo::set_yql_endpoint_tserver_uuid(const Uuid &yql_endpoint_tserver_uuid) {
  std::lock_guard lock(mutex_);
  metadata_.yql_endpoint_tserver_uuid = yql_endpoint_tserver_uuid;
}

void WaitStateInfo::UpdateMetadata(const AshMetadata &meta) {
  std::lock_guard lock(mutex_);
  metadata_.UpdateFrom(meta);
}

void WaitStateInfo::UpdateAuxInfo(const AshAuxInfo &aux) {
  std::lock_guard lock(mutex_);
  aux_info_.UpdateFrom(aux);
}

void WaitStateInfo::SetCurrentWaitState(WaitStateInfoPtr wait_state) {
  threadlocal_wait_state_ = std::move(wait_state);
}

const WaitStateInfoPtr& WaitStateInfo::CurrentWaitState() {
  if (!threadlocal_wait_state_) {
    VLOG_WITH_FUNC(3) << "returning nullptr";
  }
  return threadlocal_wait_state_;
}

WaitStateInfoPtr WaitStateInfo::CreateIfAshIsEnabled() {
  return yb::GetAtomicFlag(&FLAGS_TEST_yb_enable_ash)
             ? std::make_shared<yb::ash::WaitStateInfo>(yb::ash::AshMetadata{})
             : nullptr;
}

//
// ScopedAdoptWaitState
//
ScopedAdoptWaitState::ScopedAdoptWaitState(WaitStateInfoPtr wait_state)
    : prev_state_(WaitStateInfo::CurrentWaitState()) {
  WaitStateInfo::SetCurrentWaitState(std::move(wait_state));
}

ScopedAdoptWaitState::~ScopedAdoptWaitState() {
  WaitStateInfo::SetCurrentWaitState(std::move(prev_state_));
}

//
// ScopedWaitStatus
//
ScopedWaitStatus::ScopedWaitStatus(WaitStateCode code)
    : code_(code),
      prev_code_(
          WaitStateInfo::CurrentWaitState()
              ? WaitStateInfo::CurrentWaitState()->mutable_code().exchange(code_)
              : code_) {
  if (const auto& wait_state = WaitStateInfo::CurrentWaitState()) {
    if (GetAtomicFlag(&FLAGS_TEST_trace_ash_wait_code_updates)) {
      wait_state->VTrace(1, __func__);
      wait_state->VTrace(0, yb::Format("$0", ash::ToString(code)));
    }
  }
  MaybeSleepForTests(WaitStateInfo::CurrentWaitState().get(), code);
}

ScopedWaitStatus::~ScopedWaitStatus() {
  const auto& wait_state = WaitStateInfo::CurrentWaitState();
  if (!wait_state) {
    return;
  }

  auto expected = code_;
  if (!wait_state->mutable_code().compare_exchange_strong(expected, prev_code_)) {
    VLOG_WITH_FUNC(3) << "not reverting to prev_code_: " << prev_code_ << " since "
                      << " current_code: " << expected << " is not " << code_;
    return;
  }

  if (GetAtomicFlag(&FLAGS_TEST_trace_ash_wait_code_updates)) {
    wait_state->VTrace(1, __func__);
    wait_state->VTrace(0, yb::Format("$0", ash::ToString(prev_code_)));
  }
  MaybeSleepForTests(wait_state.get(), prev_code_);
}

void WaitStateTracker::Track(const yb::ash::WaitStateInfoPtr& wait_state_ptr) {
  std::lock_guard lock(mutex_);
  entries_.emplace(wait_state_ptr);
}

void WaitStateTracker::Untrack(const yb::ash::WaitStateInfoPtr& ptr) {
  std::lock_guard lock(mutex_);
  entries_.erase(ptr);
}

std::vector<yb::ash::WaitStateInfoPtr> WaitStateTracker::GetWaitStates() const {
  std::lock_guard lock(mutex_);
  return {entries_.begin(), entries_.end()};
}

namespace {

WaitStateTracker flush_and_compaction_wait_states_tracker;
WaitStateTracker raft_log_appender_wait_states_tracker;

}  // namespace

WaitStateTracker& FlushAndCompactionWaitStatesTracker() {
  return flush_and_compaction_wait_states_tracker;
}

WaitStateTracker& RaftLogAppenderWaitStatesTracker() {
  return raft_log_appender_wait_states_tracker;
}

}  // namespace yb::ash
