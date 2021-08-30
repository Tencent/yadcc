// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this
// file except in compliance with the License. You may obtain a copy of the
// License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "yadcc/daemon/cloud/execution_engine.h"

#include <sys/types.h>
#include <sys/wait.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gflags/gflags.h"

#include "flare/base/internal/cpu.h"
#include "flare/base/logging.h"
#include "flare/base/never_destroyed.h"
#include "flare/base/string.h"
#include "flare/fiber/fiber.h"
#include "flare/fiber/this_fiber.h"
#include "flare/fiber/timer.h"

#include "yadcc/common/parse_size.h"
#include "yadcc/daemon/cloud/execute_command.h"
#include "yadcc/daemon/common_flags.h"
#include "yadcc/daemon/sysinfo.h"
#include "yadcc/daemon/temp_dir.h"

using namespace std::literals;

DEFINE_int32(
    max_remote_tasks, -1,
    "Maximum number of tasks (accepted from network) can be executed "
    "concurrently. If not specified, it's determined by `--servant_priority`.");
DEFINE_string(servant_priority, "user",
              "If set to `user`, up to 40% of the available system resources "
              "(CPUs) are utilized. If set to `dedicated`, we increase the "
              "utilization ratio to 95%.");
DEFINE_string(
    min_memory_for_starting_new_task, "2G",
    "If memory available is less than `min_memory_for_starting_new_task`, "
    "task will fail to start.");
DEFINE_int32(
    poor_machine_threshold_processors, 16,
    "If the system we're running on has no more than than so many processor "
    "cores, it's treated as a poor machine. Poor machine won't accept tasks "
    "from the network. (`--servant_priority=dedicated` overrides this "
    "option.)");

namespace yadcc::daemon::cloud {

constexpr auto kDefaultNiceLevel = 5;

namespace {

bool IsCGroupPresent() {
  std::ifstream ifs("/proc/self/cgroup");
  std::string s((std::istreambuf_iterator<char>(ifs)),
                std::istreambuf_iterator<char>());
  FLARE_CHECK(ifs, "Failed to open [/proc/self/cgroup].");

  auto splitted = flare::Split(s, "\n");
  for (auto&& e : splitted) {
    auto parts = flare::Split(e, ":");
    FLARE_CHECK_GE(parts.size(), 3, "Unexpected cgroup setting: {}", e);
    if ((parts[1] == "cpuacct,cpu" || parts[1] == "memory") &&
        (parts[2] != "/" && parts[2] != "/user.slice")) {
      return true;
    }
  }
  return false;
}

}  // namespace

ExecutionEngine* ExecutionEngine::Instance() {
  static flare::NeverDestroyed<ExecutionEngine> engine;
  return engine.Get();
}

ExecutionEngine::ExecutionEngine()
    : exposed_job_internals_("yadcc/execution_engine/jobs",
                             [this] { return DumpTasks(); }) {
  std::optional<std::size_t> memory =
      TryParseSize(FLAGS_min_memory_for_starting_new_task);
  FLARE_CHECK(memory);
  min_memory_for_starting_new_task_ = *memory;

  auto nprocs = flare::internal::GetNumberOfProcessorsAvailable();
  if (FLAGS_max_remote_tasks == -1) {
    if (FLAGS_servant_priority == "dedicated") {
      // Don't try to achieve 100% CPU usage. In our deployment environment
      // beforing achieving that high CPU utilization we'll likely use up all
      // the memory.
      //
      // TODO(luobogao): Introduce `FLAGS_expected_memory_usage_per_job` to
      // address this issue (to some degree).
      task_concurrency_limit_ = nprocs * 95 / 100;
    } else {
      FLARE_CHECK_EQ(FLAGS_servant_priority, "user");

      if (IsCGroupPresent()) {
        // In containerized environment, it's hard to tell the "real" capacity
        // of the node (e.g., cgroup quota, share, ...).
        task_concurrency_limit_ = 0;
        not_accepting_task_reason_ = NotAcceptingTaskReason::CGroupsPresent;
        FLARE_LOG_INFO(
            "CGroup is present. We won't dispatch compilation task to this "
            "node.");
      } else if (nprocs <= FLAGS_poor_machine_threshold_processors) {
        // Poor man... Show our mercy by not dispatching tasks to this
        // machine.
        task_concurrency_limit_ = 0;
        not_accepting_task_reason_ = NotAcceptingTaskReason::PoorMachine;
        FLARE_LOG_INFO(
            "Poor machine. Compilation tasks won't be dispatched to this "
            "node.");
      } else {
        task_concurrency_limit_ = nprocs * 40 / 100;
      }
    }
  } else if (FLAGS_max_remote_tasks == 0) {
    task_concurrency_limit_ = 0;
    not_accepting_task_reason_ = NotAcceptingTaskReason::UserInstructed;
  } else {
    FLARE_CHECK_GT(FLAGS_max_remote_tasks, 0);
    task_concurrency_limit_ = FLAGS_max_remote_tasks;
  }

  if (task_concurrency_limit_) {
    FLARE_LOG_INFO("We'll serve at most {} tasks simultaneously.",
                   task_concurrency_limit_);
    cpu_limiter_.Init();
    cpu_limiter_->StartWithMaxCpu(task_concurrency_limit_);
  }
  waitpid_worker_ = std::thread([this] { ProcessWaiterProc(); });
  cleanup_timer_ = flare::fiber::SetTimer(flare::ReadCoarseSteadyClock(), 1s,
                                          [this] { OnCleanupTimer(); });
}

flare::Expected<std::size_t, NotAcceptingTaskReason>
ExecutionEngine::GetMaximumTasks() const {
  if (task_concurrency_limit_) {
    return task_concurrency_limit_;
  } else {
    return not_accepting_task_reason_;
  }
}

// TODO(luobogao): In case we've just killed a lot of child, wait until they've
// fully exited (i.e., **freed memory**) before starting new job. Otherwise we
// risk OOM-ing the machine.
std::optional<std::uint64_t> ExecutionEngine::TryQueueTask(
    std::uint64_t grant_id, flare::RefPtr<ExecutionTask> user_task) {
  std::scoped_lock _(tasks_lock_);

  auto task_id = TryStartingNewTaskUnsafe();
  if (!task_id) {
    return {};
  }

  auto cmd = user_task->GetCommandLine();
  FLARE_VLOG(1, "Executing: [{}]", cmd);

  // Save the task state. The caller will be polling on the execution result
  // later (if the task takes sufficiently long.).
  auto&& task = tasks_[*task_id];
  task = flare::MakeRefCounted<TaskDesc>();

  // Prepare stdin / stdout / stderr.
  TemporaryFile temp_in(GetTemporaryDir());
  temp_in.Write(user_task->GetStandardInputOnce());
  task->stdout_file.emplace(GetTemporaryDir());
  task->stderr_file.emplace(GetTemporaryDir());

  // FIXME: Program started with lock held. Starting program can be slow.
  auto pid = StartProgram(cmd, kDefaultNiceLevel, temp_in.fd(),
                          task->stdout_file->fd(), task->stderr_file->fd(),
                          true /* Isolated process group. */);
  task->grant_id = grant_id;  // Well we don't check for duplicate here, it
                              // won't hurt us.
  task->client_ref_count = 1;
  // TODO(luobogao): Enable it (preferably unconditionally).
  task->limit_cpu = false;
  task->started_at = flare::ReadSteadyClock();
  task->process_id = pid;
  task->task = std::move(user_task);
  task->exposition_only.command = cmd;

  // The cpu limit is applied unconditionally.
  cpu_limiter_->Limit(pid);

  // Wake up pid waiter.
  waitpid_semaphore_.release();
  return task_id;
}

bool ExecutionEngine::TryReferenceTask(std::uint64_t task_id) {
  std::scoped_lock _(tasks_lock_);
  auto task = tasks_.find(task_id);
  if (task == tasks_.end()) {
    return false;
  }
  ++task->second->client_ref_count;
  return true;
}

flare::Expected<flare::RefPtr<ExecutionTask>, ExecutionStatus>
ExecutionEngine::WaitForTask(std::uint64_t task_id,
                             std::chrono::nanoseconds timeout) {
  flare::RefPtr<TaskDesc> task;
  {
    std::scoped_lock _(tasks_lock_);
    if (auto iter = tasks_.find(task_id); iter != tasks_.end()) {
      task = iter->second;
    }
  }
  if (!task) {
    return ExecutionStatus::NotFound;
  }

  if (!task->completion_latch.wait_for(timeout)) {
    return ExecutionStatus::Running;
  }
  // Keep it there in case we're called again (e.g. due to RPC retry.).
  return task->task;
}

void ExecutionEngine::FreeTask(std::uint64_t task_id) {
  flare::RefPtr<TaskDesc> task_free;
  {
    std::scoped_lock _(tasks_lock_);
    auto task = tasks_.find(task_id);

    if (task == tasks_.end()) {
      return;
    }

    // If more than one client wait for this task, we should only dereference
    // the task.
    if (--task->second->client_ref_count > 0) {
      return;
    }

    // No other client wait on the task, clean it.
    task_free = task->second;
    (void)tasks_.erase(task);
  }

  // If it's alive, kill it.
  KillTask(task_free.Get());
}

std::vector<ExecutionEngine::Task> ExecutionEngine::EnumerateTasks() const {
  std::vector<Task> result;
  std::scoped_lock _(tasks_lock_);
  for (auto&& [k, v] : tasks_) {
    if (v->task) {
      result.emplace_back(Task{k, v->grant_id, v->task});
    }
  }
  return result;
}

void ExecutionEngine::KillExpiredTasks(
    const std::unordered_set<std::uint64_t>& expired_grant_ids) {
  auto killed = 0;
  {
    std::scoped_lock _(tasks_lock_);
    for (auto&& [k, v] : tasks_) {
      if (v->is_running.load(std::memory_order_relaxed) &&
          expired_grant_ids.count(v->grant_id) != 0) {
        KillTask(v.Get());
        ++killed;
      }
    }
  }

  FLARE_LOG_WARNING_IF(killed > 0,
                       "Killed {} tasks that are reported as expired.", killed);
}

void ExecutionEngine::Stop() {
  exiting_.store(true, std::memory_order_relaxed);

  flare::fiber::KillTimer(cleanup_timer_);

  // Kill all pending tasks then.
  {
    std::scoped_lock _(tasks_lock_);
    for (auto&& [k, v] : tasks_) {
      KillTask(v.Get());
    }
  }

  waitpid_semaphore_.release();  // On extra `release` to wake up our
  // sub-process waiter (if it's sleeping.).
  if (cpu_limiter_) {
    cpu_limiter_->Stop();
  }
}

void ExecutionEngine::KillTask(TaskDesc* task) {
  // No we don't remove task from `tasks_` now, it's our child-exit callback's
  // job.
  if (task && task->is_running.load(std::memory_order_relaxed)) {
    // Forcibly kill the task then.
    //
    // Using a ID instead of a handle here is a horrible design (of Linux), we
    // risk killing some other process here in extreme situation.
    kill(-task->process_id /* Entire group is killed. */, SIGKILL);

    // We don't bother signal the task. So long as the task exit, either
    // because killed by us, or normally, our `OnProcessExitCallback` will
    // signal the task for us.
  }
}

void ExecutionEngine::Join() {
  waitpid_worker_.join();
  while (true) {
    flare::this_fiber::SleepFor(100ms);
    bool keep_sleep = false;
    std::scoped_lock _(tasks_lock_);
    for (auto&& [_, v] : tasks_) {
      if (v->is_running.load(std::memory_order_relaxed)) {
        keep_sleep = true;
        break;
      }
    }
    if (!keep_sleep) {
      break;
    }
  }
  if (cpu_limiter_) {
    cpu_limiter_->Join();
  }
}

std::optional<std::uint64_t> ExecutionEngine::TryStartingNewTaskUnsafe() {
  if (exiting_.load(std::memory_order_relaxed)) {
    // Well we're leaving.
    return std::nullopt;
  }

  auto task_id = next_task_id_++;

  // Let's see if there's enough resource for us to start a new child.
  if (running_tasks_.fetch_add(1, std::memory_order_relaxed) + 1 >
      task_concurrency_limit_) {
    FLARE_LOG_WARNING_EVERY_SECOND(
        "Actively rejecting task. We've running out of available processors.");
    running_tasks_.fetch_sub(1, std::memory_order_relaxed);
    // `task_id` is wasted. This doesn't matter.
    return std::nullopt;
  }

  if (GetMemoryAvailable() < min_memory_for_starting_new_task_) {
    FLARE_LOG_WARNING_EVERY_SECOND(
        "Actively rejecting task. We've running out of available memory.");
    running_tasks_.fetch_sub(1, std::memory_order_relaxed);
    return std::nullopt;
  }

  tasks_run_ever_.fetch_add(1, std::memory_order_relaxed);
  return task_id;
}

void ExecutionEngine::OnCleanupTimer() {
  auto now = flare::ReadCoarseSteadyClock();
  std::vector<flare::RefPtr<TaskDesc>> freeing;

  {
    std::scoped_lock _(tasks_lock_);
    for (auto iter = tasks_.begin(); iter != tasks_.end();) {
      if (!iter->second->is_running.load(std::memory_order_relaxed) &&
          iter->second->completed_at + 1min < now) {
        freeing.push_back(iter->second);
        iter = tasks_.erase(iter);
      } else {
        ++iter;
      }
    }
  }

  FLARE_LOG_WARNING_IF(
      !freeing.empty(),
      "Freeing {} completed tasks that seems no one is interested in.",
      freeing.size());
}

// Called in fiber environment.
void ExecutionEngine::OnProcessExitCallback(pid_t pid, int exit_code) {
  // FIXME: Holding this lock for too long does not seems a good idea
  std::unique_lock lk(tasks_lock_);

  // This map shouldn't be too large, traversal should be acceptable.
  flare::RefPtr<TaskDesc> task;
  for (auto&& [k, v] : tasks_) {
    if (v->process_id == pid) {
      task = v;
      break;
    }
  }

  // This is done even if we don't recognize the leaving process. This is
  // possible if the task is freed (via `FreeTask`) before its exit callback
  // fired (note that process exit callback can incur some delay).
  running_tasks_.fetch_sub(1, std::memory_order_relaxed);
  cpu_limiter_->Remove(pid);

  if (!task) {
    FLARE_LOG_WARNING_EVERY_SECOND(
        "Unexpected: Received an exit event for unknown process [{}].", pid);
    return;  // Ignore it then.
  }

  FLARE_LOG_WARNING_IF_EVERY_SECOND(exit_code == -1,
                                    "Command [{}] failed unexpectedly.",
                                    task->exposition_only.command);

  // Mark the task as finished.
  task->completed_at = flare::ReadCoarseSteadyClock();
  task->is_running.store(false, std::memory_order_relaxed);

  auto out = task->stdout_file->ReadAll(), err = task->stderr_file->ReadAll();
  // Hopefully unlock prior calling user's callback won't hurt.
  lk.unlock();
  task->task->OnCompletion(exit_code, std::move(out), std::move(err));

  task->completion_latch.count_down();
}

void ExecutionEngine::ProcessWaiterProc() {
  auto more_work_to_do = [&] {
    return !exiting_.load(std::memory_order_relaxed) ||
           // `Stop` kills all subprocesses. Therefore, if we're stopped when
           // outstanding tasks are running, waiting for all of them to exit
           // won't take long.
           running_tasks_.load(std::memory_order_relaxed) != 0;
  };
  while (more_work_to_do()) {
    waitpid_semaphore_.acquire();
    if (!more_work_to_do()) {
      break;
    }

    int status;
    auto pid = wait(&status);

    // This is a special case. If we're waken up due to quit signal, there's a
    // time window between we kill last subprocess and `running_tasks_` become
    // zero. In this time window, `wait` indeed can fail. We handle this case
    // here.
    if (pid == -1 && exiting_.load(std::memory_order_relaxed)) {
      break;
    }

    FLARE_PCHECK(pid != -1, "Failed to wait subprocess started by us.");
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    FLARE_LOG_WARNING_IF_EVERY_SECOND(
        exit_code == -1, "Process [{}] exited abnormally: {}.", pid, status);

    flare::StartFiberFromPthread(
        [this, pid, exit_code] { OnProcessExitCallback(pid, exit_code); });
  }
}

Json::Value ExecutionEngine::DumpTasks() {
  std::scoped_lock _(tasks_lock_);
  Json::Value jsv;
  jsv["max_tasks"] = static_cast<Json::UInt64>(task_concurrency_limit_);
  jsv["running_tasks"] =
      static_cast<Json::UInt64>(running_tasks_.load(std::memory_order_relaxed));
  jsv["alive_tasks"] = static_cast<Json::UInt64>(tasks_.size());
  jsv["tasks_run_ever"] = static_cast<Json::UInt64>(
      tasks_run_ever_.load(std::memory_order_relaxed));
  for (auto&& [k, v] : tasks_) {
    auto&& e = jsv[std::to_string(k)];
    e = v->task->DumpInternals();
    e["command"] = v->exposition_only.command;
    if (v->is_running.load(std::memory_order_relaxed)) {
      e["state"] = "RUNNING";
    } else {
      e["state"] = "DONE";
      auto sys_time =
          v->completed_at - flare::ReadSteadyClock() + flare::ReadSystemClock();
      e["completed_at"] = static_cast<Json::UInt64>(
          std::chrono::system_clock::to_time_t(sys_time));
      e["exit_code"] = v->exposition_only.exit_code;
      e["stdout_size"] =
          static_cast<Json::UInt64>(v->exposition_only.stdout_size);
      e["stderr_size"] =
          static_cast<Json::UInt64>(v->exposition_only.stderr_size);
    }
  }
  return jsv;
}

}  // namespace yadcc::daemon::cloud
