#include "Lan.h"
#include <csignal>
int main(int argc, char** argv) {
  if (argc != 2) {
    return 2;
  }
  signal(SIGPIPE, SIG_IGN);
  jd::root = argv[1];
  jd::Lan lan;
  lan.protectedPath = [](const jd::Str& s) { return s == "/books/active.txt"; };
  lan.start();
  printf("%u %s\n", WiFiServer::port, lan.pairing().c_str());
  fflush(stdout);
  for (;;) {
    lan.poll();
    usleep(200);
  }
}
