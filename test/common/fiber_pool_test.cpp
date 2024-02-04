#include "common/fiber_thread_pool.h"
#include "gtest/gtest.h"

int Foo() { return 2; }

TEST(CommonTest, FiberThreadPoolTest) {
  const int num_worker = 2;
  FiberThreadPool p(num_worker);

  auto fut = p.Submit([] { return 5; });
  EXPECT_EQ(5, fut.get());
}