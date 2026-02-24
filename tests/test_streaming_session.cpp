#include "streaming_session.hpp"
#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

namespace {

int count_events(const std::vector<BlockEvent> &events, BlockEvent::Action action,
                 NodeType type) {
  int count = 0;
  for (const auto &ev : events) {
    if (ev.action == action && ev.type == type) {
      count++;
    }
  }
  return count;
}

std::vector<BlockEvent> drain_events(StreamingSession &session) {
  std::vector<BlockEvent> events;
  while (session.has_events()) {
    events.push_back(session.pop_event());
  }
  return events;
}

} // namespace

TEST(StreamingSession, PollingModeEmitsOpenUpdateAndClose) {
  StreamingSession session;

  session.parse("hello");
  EXPECT_FALSE(session.has_events());

  session.parse("\n");
  session.finish();

  std::vector<BlockEvent> events = drain_events(session);
  EXPECT_FALSE(events.empty());

  EXPECT_EQ(count_events(events, BlockEvent::Open, NodeType::Document), 1);
  EXPECT_EQ(count_events(events, BlockEvent::Open, NodeType::Paragraph), 1);
  EXPECT_EQ(count_events(events, BlockEvent::Update, NodeType::Paragraph), 1);
  EXPECT_EQ(count_events(events, BlockEvent::Close, NodeType::Document), 1);
  EXPECT_EQ(count_events(events, BlockEvent::Close, NodeType::Paragraph), 1);
}

TEST(StreamingSession, FinishIsIdempotentAndNoDuplicateCloseEvents) {
  StreamingSession session;

  session.parse("x\n");
  session.finish();
  std::vector<BlockEvent> first_pass = drain_events(session);

  session.finish();
  std::vector<BlockEvent> second_pass = drain_events(session);

  EXPECT_TRUE(second_pass.empty());
  EXPECT_EQ(count_events(first_pass, BlockEvent::Close, NodeType::Document), 1);
  EXPECT_EQ(count_events(first_pass, BlockEvent::Close, NodeType::Paragraph), 1);
}

TEST(StreamingSession, EmitUpdatesToggleSuppressesUpdateEvents) {
  StreamingSession session;
  session.set_emit_updates(false);

  session.parse("hello\n");
  session.finish();

  std::vector<BlockEvent> events = drain_events(session);
  EXPECT_EQ(count_events(events, BlockEvent::Update, NodeType::Paragraph), 0);
  EXPECT_EQ(count_events(events, BlockEvent::Open, NodeType::Paragraph), 1);
  EXPECT_EQ(count_events(events, BlockEvent::Close, NodeType::Paragraph), 1);
}

TEST(StreamingSession, CallbackModeDispatchesWithoutQueueing) {
  std::vector<BlockEvent> callback_events;
  StreamingSession session([&callback_events](const BlockEvent &ev) {
    callback_events.push_back(ev);
  });

  session.parse("hello\n");
  session.finish();

  EXPECT_FALSE(callback_events.empty());
  EXPECT_FALSE(session.has_events());
}

TEST(StreamingSession, PopEventsRespectsMaxCountAndOrder) {
  StreamingSession session;

  session.parse("hello\n");
  session.finish();

  std::vector<BlockEvent> batch = session.pop_events(2);
  ASSERT_EQ(batch.size(), 2u);
  EXPECT_EQ(batch[0].action, BlockEvent::Open);
  EXPECT_EQ(batch[0].type, NodeType::Document);
  EXPECT_EQ(batch[1].action, BlockEvent::Open);
  EXPECT_EQ(batch[1].type, NodeType::Paragraph);

  std::vector<BlockEvent> rest = drain_events(session);
  EXPECT_FALSE(rest.empty());
}

TEST(StreamingSession, ResetClearsStateForReuse) {
  StreamingSession session;

  session.parse("first\n");
  session.finish();
  EXPECT_TRUE(session.is_finished());
  EXPECT_TRUE(session.has_events());

  session.reset();
  EXPECT_FALSE(session.is_finished());
  EXPECT_FALSE(session.has_events());

  session.parse("second\n");
  session.finish();
  std::vector<BlockEvent> events = drain_events(session);

  EXPECT_EQ(count_events(events, BlockEvent::Open, NodeType::Document), 1);
  EXPECT_EQ(count_events(events, BlockEvent::Close, NodeType::Document), 1);
}

TEST(StreamingSession, ParseAfterFinishThrows) {
  StreamingSession session;

  session.parse("done\n");
  session.finish();

  EXPECT_THROW(session.parse("more\n"), std::logic_error);
}

TEST(StreamingSession, PopEventOnEmptyQueueThrows) {
  StreamingSession session;
  EXPECT_THROW(session.pop_event(), std::out_of_range);
}
