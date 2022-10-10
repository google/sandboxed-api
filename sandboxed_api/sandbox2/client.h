// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This class should be used in the client code, in a place where sandboxing
// should be engaged.

#ifndef SANDBOXED_API_SANDBOX2_CLIENT_H_
#define SANDBOXED_API_SANDBOX2_CLIENT_H_

#include <cstdint>
#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/logsink.h"
#include "sandboxed_api/sandbox2/network_proxy/client.h"

namespace sandbox2 {

class Client {
 public:
  // Client is ready to be sandboxed.
  static constexpr uint32_t kClient2SandboxReady = 0x0A0B0C01;
  // Sandbox is ready to monitor the sandboxee.
  static constexpr uint32_t kSandbox2ClientDone = 0x0A0B0C02;

  explicit Client(Comms* comms);

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  // Receives a sandbox policy over the comms channel and enables sandboxing.
  // Using this method allows to have a sandbox-aware sandboxee perform complex
  // initialization first and then enable sandboxing for actual processing.
  void SandboxMeHere();

  // Returns the file descriptor that was mapped to the sandboxee using
  // IPC::ReceiveFd(name).
  int GetMappedFD(const std::string& name);
  bool HasMappedFD(const std::string& name);

  // Registers a LogSink that forwards all logs to the supervisor.
  void SendLogsToSupervisor();

  // Returns the network proxy client and starts it if this function is called
  // for the first time.
  NetworkProxyClient* GetNetworkProxyClient();

  // Redirects the connect() syscall to the ConnectHandler() method in
  // the NetworkProxyClient class.
  absl::Status InstallNetworkProxyHandler();

 protected:
  // Comms used for synchronization with the monitor, not owned by the object.
  Comms* comms_;

 private:
  static constexpr const char* kFDMapEnvVar = "SB2_FD_MAPPINGS";

  friend class ForkServer;

  // Seccomp-bpf policy received from the monitor.
  std::vector<uint8_t> policy_;

  // LogSink that forwards all log messages to the supervisor.
  std::unique_ptr<LogSink> logsink_;

  // NetworkProxyClient that forwards network connection requests to the
  // supervisor.
  std::unique_ptr<NetworkProxyClient> proxy_client_;

  // In the pre-execve case, the sandboxee has to pass the information about
  // file descriptors to the new process. We set an environment variable for
  // this case that is parsed in the Client constructor if present.
  absl::flat_hash_map<std::string, int> fd_map_;

  std::string GetFdMapEnvVar() const;

  // Sets up communication channels with the sandbox.
  // preserve_fds contains file descriptors that should be kept open and alive.
  // The FD numbers might be changed if needed and are updated in the vector.
  // preserve_fds can be a nullptr, equivallent to an empty vector.
  void SetUpIPC(std::vector<int>* preserve_fds);

  // Sets up the current working directory.
  void SetUpCwd();

  // Receives seccomp-bpf policy from the monitor.
  void ReceivePolicy();

  // Applies sandbox-bpf policy, have limits applied on us, and become ptrace'd.
  void ApplyPolicyAndBecomeTracee();

  void PrepareEnvironment(std::vector<int>* preserve_fds = nullptr);
  void EnableSandbox();
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_CLIENT_H_
