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

#include "absl/strings/str_format.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/network_proxy/client.h"
#include "sandboxed_api/sandbox2/util/fileops.h"

static ssize_t ReadFromFd(int fd, uint8_t* buf, size_t size) {
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

static bool CommunicationTest(int sock) {
  char received[1025] = {0};

  if (ReadFromFd(sock, reinterpret_cast<uint8_t*>(received),
                 sizeof(received) - 1) <= 0) {
    LOG(ERROR) << "Data receiving error";
    return false;
  }
  absl::PrintF("Sandboxee received data from the server:\n\n%s\n", received);
  if (strcmp(received, "Hello World\n")) {
    LOG(ERROR) << "Data receiving error";
    return false;
  }

  return true;
}

static int ConnectToServer(int port) {
  int s = socket(AF_INET6, SOCK_STREAM, 0);
  if (s < 0) {
    PLOG(ERROR) << "socket() failed";
    return -1;
  }

  struct sockaddr_in6 saddr {};
  saddr.sin6_family = AF_INET6;
  saddr.sin6_port = htons(port);

  int err = inet_pton(AF_INET6, "::1", &saddr.sin6_addr);
  if (err == 0) {
    LOG(ERROR) << "inet_pton() failed";
    close(s);
    return -1;
  } else if (err == -1) {
    PLOG(ERROR) << "inet_pton() failed";
    close(s);
    return -1;
  }

  err = connect(s, reinterpret_cast<const struct sockaddr*>(&saddr),
                sizeof(saddr));
  if (err != 0) {
    LOG(ERROR) << "connect() failed";
    close(s);
    return -1;
  }

  LOG(INFO) << "Connected to the server";
  return s;
}

int main(int argc, char** argv) {
  // Set-up the sandbox2::Client object, using a file descriptor (1023).
  sandbox2::Comms comms(sandbox2::Comms::kSandbox2ClientCommsFD);
  sandbox2::Client sandbox2_client(&comms);

  // Enable sandboxing from here.
  sandbox2_client.SandboxMeHere();

  absl::Status status = sandbox2_client.InstallNetworkProxyHandler();
  if (!status.ok()) {
    LOG(ERROR) << "InstallNetworkProxyHandler() failed: " << status.message();
    return 1;
  }

  // Receive port number of the server
  int port;
  if (!comms.RecvInt32(&port)) {
    LOG(ERROR) << "sandboxee_comms->RecvUint32(&crc4) failed";
    return 2;
  }

  sandbox2::file_util::fileops::FDCloser client{ConnectToServer(port)};
  if (client.get() == -1) {
    return 3;
  }

  if (!CommunicationTest(client.get())) {
    return 4;
  }

  return 0;
}
