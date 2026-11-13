#include "ScreenReceiver.h"
#include <csignal>
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
  if (argc == 2) {
    // Independent reference using the same font as normal firmware UI labels.
    jd::Canvas canvas;
    canvas.clear();
    canvas.ui(16, 16, "静读无线墨屏", 15);
    canvas.ui(16, 40, "Ready 123 □", 15);
    if (!strcmp(argv[1], "reference-pbm")) {
      fputs("P4\n240 416\n", stdout);
      for (auto byte : canvas.bits) {
        fputc(uint8_t(~byte), stdout);
      }
    } else {
      printf("%08x\n", jd::checksum(canvas.bits, sizeof canvas.bits));
    }
    return 0;
  }
  signal(SIGPIPE, SIG_IGN);
  jd::ScreenReceiver receiver;
  receiver.listen();
  printf("%u\n", WiFiServer::port);
  fflush(stdout);
  while (true) {
    if (receiver.poll("TEST")) {
      const auto* c = receiver.image();
      printf("%08x\n", c ? jd::checksum(c->bits, sizeof c->bits) : 0);
      fflush(stdout);
    }
    usleep(200);
  }
}
