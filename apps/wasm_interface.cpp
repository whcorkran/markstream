#include "html_renderer.hpp"
#include "streaming_session.hpp"
#include <cstddef>
#include <cstdint>

static StreamingSession session;
static HtmlRenderer renderer;
static size_t event_threshold = 1;

extern "C" {

// Initialize with event threshold (how many events before re-rendering)
void ms_init(size_t threshold) {
  event_threshold = threshold;
  session.set_emit_updates(false);
}

// Feed a chunk of markdown. Returns 1 if HTML was re-rendered, 0 otherwise.
int ms_parse(const char *input, size_t length) {
  session.parse(std::string_view(input, length));
  if (session.num_events() >= event_threshold) {
    session.pop_events(session.num_events());
    renderer.render(session.parser().get_root(),
                    &session.parser().link_definitions());
    return 1;
  }
  return 0;
}

// Signal end of input. Always re-renders. Returns 1.
int ms_finish() {
  session.finish();
  session.pop_events(session.num_events());
  renderer.render(session.parser().get_root(),
                  &session.parser().link_definitions());
  return 1;
}

// Get pointer to HTML output buffer (in WASM linear memory)
const char *ms_get_html_ptr() { return renderer.view().data(); }

// Get length of HTML output buffer
size_t ms_get_html_len() { return renderer.view().size(); }

// Reset for a new document
void ms_reset() { session.reset(); }

} // extern "C"
