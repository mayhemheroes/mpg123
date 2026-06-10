#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "mpg123.h"

#include <csetjmp>
#include <csignal>
#include <sys/time.h>

// Per-input watchdog: libmpg123 frame resync (mpg123_decode -> get_next_frame, libmpg123.c:621) can
// livelock on a malformed feed stream so a single mpg123_decode() never returns; the frame/stream
// caps below run only BETWEEN calls and -max_total_time cannot interrupt one call, hanging an
// un-timeout-ed run (local fuzz-smoke) at 0 new edges. A SIGVTALRM CPU-time watchdog bounds each
// input: on expiry we siglongjmp out and delete the per-input handle cleanly. ASan/UBSan still catch
// real memory bugs first (they halt before the timer); this only rescues genuine livelocks.
static sigjmp_buf g_wd_jmp;
static volatile sig_atomic_t g_wd_armed = 0;
static void wd_handler(int) { if (g_wd_armed) { g_wd_armed = 0; siglongjmp(g_wd_jmp, 1); } }
static void wd_install() { struct sigaction sa; sigemptyset(&sa.sa_mask); sa.sa_flags = 0; sa.sa_handler = wd_handler; sigaction(SIGVTALRM, &sa, nullptr); }
static void wd_set(unsigned s) { struct itimerval it = {}; it.it_value.tv_sec = s; g_wd_armed = (s != 0); setitimer(ITIMER_VIRTUAL, &it, nullptr); }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static bool initialized = false;
  if (!initialized) {
    mpg123_init();
    wd_install();
    initialized = true;
  }
  int ret;
  mpg123_handle* handle = mpg123_new(nullptr, &ret);
  if (handle == nullptr) {
    return 0;
  }

  ret = mpg123_param(handle, MPG123_ADD_FLAGS, MPG123_QUIET, 0.);
  if(ret == MPG123_OK)
    ret = mpg123_open_feed(handle);
  if (ret != MPG123_OK) {
    mpg123_delete(handle);
    return 0;
  }

  // Raw (not std::vector) buffers: the watchdog siglongjmp bypasses C++ destructors, so any heap
  // vector live across it would leak (LSan false positive). Use malloc + a stack feed buffer and
  // free on the single post-loop path (reached on both normal exit and watchdog recovery).
  const size_t output_buffer_size = mpg123_outblock(handle);
  unsigned char* output_buffer = (unsigned char*)malloc(output_buffer_size ? output_buffer_size : 1);
  if (output_buffer == nullptr) { mpg123_delete(handle); return 0; }

  size_t output_written = 0;
  // Initially, start by feeding the decoder more data.
  int decode_ret = MPG123_NEED_MORE;
  FuzzedDataProvider provider(data, size);
  if (sigsetjmp(g_wd_jmp, 1) == 0) {
  wd_set(10);  /* 10s CPU per input >> any legitimate <=1MB decode */
  while ((decode_ret != MPG123_ERR)) {
    if (decode_ret == MPG123_NEED_MORE) {
      if (provider.remaining_bytes() == 0
          || mpg123_tellframe(handle) > 10000
          || mpg123_tell_stream(handle) > 1<<20) {
        break;
      }
      // Stack feed buffer (not a std::vector) so the watchdog siglongjmp cannot leak it.
      unsigned char next_input[4096];
      const size_t want = provider.ConsumeIntegralInRange<size_t>(0, provider.remaining_bytes());
      const size_t next_size = provider.ConsumeData(next_input, want < sizeof(next_input) ? want : sizeof(next_input));
      decode_ret = mpg123_decode(handle, next_input, next_size,
                                 output_buffer, output_buffer_size,
                                 &output_written);
    } else if (decode_ret != MPG123_ERR && decode_ret != MPG123_NEED_MORE) {
      decode_ret = mpg123_decode(handle, nullptr, 0, output_buffer,
                                 output_buffer_size, &output_written);
    } else {
      // Unhandled mpg123_decode return value.
      abort();
    }
  }

  }
  wd_set(0);  /* disarm */
  free(output_buffer);

  mpg123_delete(handle);

  return 0;
}
