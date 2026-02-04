#include <gmock/gmock.h>

#include "stack_trace_print_on_kill.h"

int main(int argc, char **argv) {
  utils::install_backtrace_handler_on_kill();

  testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
