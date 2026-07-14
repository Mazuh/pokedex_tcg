#include <gtest/gtest.h>

#include "greeting.h"

TEST(GreetingTest, IsNotEmpty) {
    EXPECT_FALSE(pokedex::greeting().empty());
}

TEST(GreetingTest, ContainsHelloWorld) {
    EXPECT_NE(pokedex::greeting().find("Hello, World!"), std::string::npos);
}
