#include <gtest/gtest.h>

#include <set>

#include "core/domain/card_reference.h"

namespace {

using pokedex::CardReference;

CardReference mew151() { return {"MEW", "EN", "151/165"}; }

TEST(CardReferenceTest, EqualByValue) {
    EXPECT_EQ(mew151(), mew151());
}

TEST(CardReferenceTest, DiffersWhenAnyFieldDiffers) {
    CardReference a = mew151();
    CardReference b = mew151();
    b.collectorNumber = "150/165";
    EXPECT_NE(a, b);
}

// The printed card name is part of the reference (it's the only label a species-free
// card has), so it participates in value equality like the other fields.
TEST(CardReferenceTest, DiffersWhenTheNameDiffers) {
    CardReference a = mew151();
    CardReference b = mew151();
    a.name = "Mew";
    b.name = "Mewtwo";
    EXPECT_NE(a, b);
}

TEST(CardReferenceTest, GroupsCopiesByPrintingAsOrderedKey) {
    std::set<CardReference> printings;
    printings.insert(mew151());
    printings.insert(mew151());  // same printing, deduped
    printings.insert({"MEW", "EN", "150/165"});
    EXPECT_EQ(printings.size(), 2u);
}

}  // namespace
