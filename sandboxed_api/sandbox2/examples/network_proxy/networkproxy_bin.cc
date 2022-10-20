// This file is an example of a network sandboxed binary inside a network
// namespace. It can't connect with the server directly, but the executor can
// establish a connection and pass the connected socket to the sandboxee.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syscall.h>

#include <cstring>

#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/network_proxy/client.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/status_macros.h"

ABSL_FLAG(bool, connect_with_handler, true, "Connect using automatic mode.");

namespace {

static sandbox2::NetworkProxyClient* g_proxy_client;

ssize_t ReadFromFd(int fd, uint8_t* buf, size_t size) {
  ssize_t received = 0;
  while (received < size) {
    ssize_t read_status =
        TEMP_FAILURE_RETRY(read(fd, &buf[received], size - received));
    if (read_status == 0) {
      break;
    }
    if (read_status < 0) {
      return -1;
    }
    received += read_status;
  }
  return received;
}

absl::Status CommunicationTest(int sock) {
  char received[1025] = {0};

  if (ReadFromFd(sock, reinterpret_cast<uint8_t*>(received),
                 sizeof(received) - 1) <= 0) {
    return absl::InternalError("Data receiving error");
  }
  absl::PrintF("Sandboxee received data from the server:\n\n%s\n", received);
  if (strcmp(received, "Hello World\n")) {
    return absl::InternalError("Data receiving error");
  }
  return absl::OkStatus();
}

absl::StatusOr<struct sockaddr_in6> CreateAddres(int port) {
  static struct sockaddr_in6 saddr {};
  saddr.sin6_family = AF_INET6;
  saddr.sin6_port = htons(port);

  if (int err = inet_pton(AF_INET6, "::1", &saddr.sin6_addr); err <= 0) {
    return absl::ErrnoToStatus(errno, "socket()");
  }
  return saddr;
}

absl::Status ConnectManually(int s, const struct sockaddr_in6& saddr) {
  return g_proxy_client->Connect(
      s, reinterpret_cast<const struct sockaddr*>(&saddr), sizeof(saddr));
}

absl::Status ConnectWithHandler(int s, const struct sockaddr_in6& saddr) {
  int err = connect(s, reinterpret_cast<const struct sockaddr*>(&saddr),
                    sizeof(saddr));
  if (err != 0) {
    return absl::InternalError("connect() failed");
  }

  return absl::OkStatus();
}

absl::StatusOr<int> ConnectToServer(int port) {
  SAPI_ASSIGN_OR_RETURN(struct sockaddr_in6 saddr, CreateAddres(port));

  sapi::file_util::fileops::FDCloser s(socket(AF_INET6, SOCK_STREAM, 0));
  if (s.get() < 0) {
    return absl::ErrnoToStatus(errno, "socket()");
  }

  if (absl::GetFlag(FLAGS_connect_with_handler)) {
    SAPI_RETURN_IF_ERROR(ConnectWithHandler(s.get(), saddr));
  } else {
    SAPI_RETURN_IF_ERROR(ConnectManually(s.get(), saddr));
  }

  LOG(INFO) << "Connected to the server";
  return s.Release();
}

}  // namespace

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  // Set-up the sandbox2::Client object, using a file descriptor (1023).
  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  sandbox2::Client sandbox2_client(&comms);

  // Enable sandboxing from here.
  sandbox2_client.SandboxMeHere();

  if (absl::GetFlag(FLAGS_connect_with_handler)) {
    if (auto status = sandbox2_client.InstallNetworkProxyHandler();
        !status.ok()) {
      LOG(ERROR) << "InstallNetworkProxyHandler() failed: " << status.message();
      return 1;
    }
  } else {
    g_proxy_client = sandbox2_client.GetNetworkProxyClient();
  }

  // Receive port number of the server
  int port;
  if (!comms.RecvInt32(&port)) {
    LOG(ERROR) << "Failed to receive port number";
    return 2;
  }

  absl::StatusOr<int> sock_s = ConnectToServer(port);
  if (!sock_s.ok()) {
    LOG(ERROR) << sock_s.status().message();
    return 3;
  }
  sapi::file_util::fileops::FDCloser client(sock_s.value());

  if (auto status = CommunicationTest(client.get()); !status.ok()) {
    LOG(ERROR) << status.message();
    return 4;
  }
  return 0;
}
