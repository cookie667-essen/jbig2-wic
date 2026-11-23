#pragma once
#include <stdexcept>
#include <string>

namespace jbig2 {
struct DecodeAbort : public std::runtime_error {
    explicit DecodeAbort(const char* msg) : std::runtime_error(msg) {}
};
} // namespace jbig2

#define CHECK(cond)     do { if (!(cond)) throw jbig2::DecodeAbort("CHECK failed: " #cond); } while (0)
#define DCHECK(cond)    CHECK(cond)
#define CHECK_EQ(a, b)  CHECK((a) == (b))
#define CHECK_NE(a, b)  CHECK((a) != (b))
#define CHECK_LT(a, b)  CHECK((a) <  (b))
#define CHECK_LE(a, b)  CHECK((a) <= (b))
#define CHECK_GT(a, b)  CHECK((a) >  (b))
#define CHECK_GE(a, b)  CHECK((a) >= (b))
#define DCHECK_EQ(a, b) CHECK_EQ(a, b)
#define DCHECK_NE(a, b) CHECK_NE(a, b)
#define DCHECK_LT(a, b) CHECK_LT(a, b)
#define DCHECK_LE(a, b) CHECK_LE(a, b)
#define DCHECK_GT(a, b) CHECK_GT(a, b)
#define DCHECK_GE(a, b) CHECK_GE(a, b)
#define NOTREACHED()    throw jbig2::DecodeAbort("NOTREACHED")